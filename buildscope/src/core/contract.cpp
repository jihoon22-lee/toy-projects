#include "buildscope/contract.hpp"

#include "contract_json_guard.hpp"
#include "contract_parser.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace buildscope {
namespace {

qint64 openSnapshot(const QString &path, QFile &file) {
#if defined(Q_OS_UNIX)
    auto flags = O_RDONLY;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NONBLOCK)
    flags |= O_NONBLOCK;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#elif defined(Q_OS_UNIX)
    if (QFileInfo(path).isSymLink()) {
        throw ContractError("snapshot final symbolic links are forbidden");
    }
#endif
    const auto encodedPath = QFile::encodeName(path);
    const auto descriptor = ::open(encodedPath.constData(), flags);
    if (descriptor < 0) {
#if defined(ELOOP)
        if (errno == ELOOP) {
            throw ContractError("snapshot final symbolic links are forbidden");
        }
#endif
        throw ContractError("cannot open snapshot: " +
                            QString::fromLocal8Bit(std::strerror(errno)));
    }
    struct stat metadata {};
    if (::fstat(descriptor, &metadata) != 0) {
        const auto message = QString::fromLocal8Bit(std::strerror(errno));
        ::close(descriptor);
        throw ContractError("cannot inspect snapshot: " + message);
    }
    if (!S_ISREG(metadata.st_mode)) {
        ::close(descriptor);
        throw ContractError("snapshot must be a regular file");
    }
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        const auto message = file.errorString();
        ::close(descriptor);
        throw ContractError("cannot open snapshot: " + message);
    }
    return metadata.st_size;
#else
    if (QFileInfo(path).isSymLink()) {
        throw ContractError("snapshot final symbolic links are forbidden");
    }
    file.setFileName(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw ContractError("cannot open snapshot: " + file.errorString());
    }
    if (!QFileInfo(file).isFile()) {
        throw ContractError("snapshot must be a regular file");
    }
    return file.size();
#endif
}

}  // namespace

ContractError::ContractError(const QString &message) : std::runtime_error(message.toStdString()) {}

Snapshot loadSnapshotFile(const QString &path) {
    constexpr qint64 kMaxSnapshotBytes = 256LL * 1024LL * 1024LL;
    QFile file;
    if (openSnapshot(path, file) > kMaxSnapshotBytes) {
        throw ContractError("snapshot exceeds 268435456 byte limit");
    }
    const auto payload = file.read(kMaxSnapshotBytes + 1);
    if (payload.size() > kMaxSnapshotBytes) {
        throw ContractError("snapshot exceeds 268435456 byte limit");
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        throw ContractError("invalid JSON at byte " + QString::number(parseError.offset) + ": " +
                            parseError.errorString());
    }
    detail::rejectDuplicateJsonKeys(payload);
    return detail::parseSnapshotDocument(document);
}

QString invocationText(const SnapshotEntry &entry) {
    if (!entry.arguments.isEmpty()) {
        return entry.arguments.join(QLatin1Char(' '));
    }
    return entry.command;
}

}  // namespace buildscope
