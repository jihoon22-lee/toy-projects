#include "buildscope/contract.hpp"

#include "contract_json_guard.hpp"
#include "contract_loader.hpp"
#include "contract_parser.hpp"

#include <QDateTime>
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

struct OpenedContract {
    qint64 size = 0;
#if defined(Q_OS_UNIX)
    struct stat identity {};
#else
    QString canonicalPath;
    QDateTime modified;
#endif
};

#if defined(Q_OS_UNIX)
bool sameTimestamp(const struct timespec &left, const struct timespec &right) {
    return left.tv_sec == right.tv_sec && left.tv_nsec == right.tv_nsec;
}

bool sameFileIdentity(const struct stat &left, const struct stat &right) {
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

bool sameFileState(const struct stat &left, const struct stat &right) {
#if defined(Q_OS_DARWIN)
    const auto modificationUnchanged =
        sameTimestamp(left.st_mtimespec, right.st_mtimespec);
    const auto changeUnchanged = sameTimestamp(left.st_ctimespec, right.st_ctimespec);
#else
    const auto modificationUnchanged = sameTimestamp(left.st_mtim, right.st_mtim);
    const auto changeUnchanged = sameTimestamp(left.st_ctim, right.st_ctim);
#endif
    return sameFileIdentity(left, right) && left.st_size == right.st_size &&
           modificationUnchanged && changeUnchanged;
}
#endif

OpenedContract openContract(const QString &path, const QString &kind, QFile &file) {
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
        throw ContractError(kind + " final symbolic links are forbidden");
    }
#endif
    const auto encodedPath = QFile::encodeName(path);
    const auto descriptor = ::open(encodedPath.constData(), flags);
    if (descriptor < 0) {
#if defined(ELOOP)
        if (errno == ELOOP) {
            throw ContractError(kind + " final symbolic links are forbidden");
        }
#endif
        throw ContractError("cannot open " + kind + ": " +
                            QString::fromLocal8Bit(std::strerror(errno)));
    }
    struct stat metadata {};
    if (::fstat(descriptor, &metadata) != 0) {
        const auto message = QString::fromLocal8Bit(std::strerror(errno));
        ::close(descriptor);
        throw ContractError("cannot inspect " + kind + ": " + message);
    }
    if (!S_ISREG(metadata.st_mode)) {
        ::close(descriptor);
        throw ContractError(kind + " must be a regular file");
    }
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        const auto message = file.errorString();
        ::close(descriptor);
        throw ContractError("cannot open " + kind + ": " + message);
    }
    return OpenedContract{metadata.st_size, metadata};
#else
    if (QFileInfo(path).isSymLink()) {
        throw ContractError(kind + " final symbolic links are forbidden");
    }
    file.setFileName(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw ContractError("cannot open " + kind + ": " + file.errorString());
    }
    if (!QFileInfo(file).isFile()) {
        throw ContractError(kind + " must be a regular file");
    }
    const QFileInfo metadata(file);
    return OpenedContract{metadata.size(), metadata.canonicalFilePath(),
                          metadata.lastModified()};
#endif
}

void verifyContractUnchanged(const QString &path, const QString &kind, QFile &file,
                             const OpenedContract &opened) {
#if defined(Q_OS_UNIX)
    struct stat descriptorMetadata {};
    if (::fstat(file.handle(), &descriptorMetadata) != 0) {
        throw ContractError("cannot re-inspect open " + kind + ": " +
                            QString::fromLocal8Bit(std::strerror(errno)));
    }
    struct stat pathMetadata {};
    const auto encodedPath = QFile::encodeName(path);
    if (::lstat(encodedPath.constData(), &pathMetadata) != 0) {
        throw ContractError(kind + " path changed while reading: " +
                            QString::fromLocal8Bit(std::strerror(errno)));
    }
    if (!S_ISREG(pathMetadata.st_mode) || !sameFileIdentity(opened.identity, pathMetadata)) {
        throw ContractError(kind + " path identity changed while reading");
    }
    if (!sameFileState(opened.identity, descriptorMetadata)) {
        throw ContractError(kind + " content changed while reading");
    }
#else
    const QFileInfo descriptorMetadata(file);
    const QFileInfo pathMetadata(path);
    if (!pathMetadata.isFile() || pathMetadata.isSymLink() ||
        descriptorMetadata.canonicalFilePath() != opened.canonicalPath ||
        pathMetadata.canonicalFilePath() != opened.canonicalPath) {
        throw ContractError(kind + " path identity changed while reading");
    }
    if (descriptorMetadata.size() != opened.size || pathMetadata.size() != opened.size ||
        descriptorMetadata.lastModified() != opened.modified ||
        pathMetadata.lastModified() != opened.modified) {
        throw ContractError(kind + " content changed while reading");
    }
#endif
}

}  // namespace

ContractError::ContractError(const QString &message) : std::runtime_error(message.toStdString()) {}

QJsonDocument detail::loadJsonContractFile(
    const QString &path, const QString &kind, const SnapshotPostReadHook &postReadHook) {
    constexpr qint64 kMaxContractBytes = 256LL * 1024LL * 1024LL;
    QFile file;
    const auto opened = openContract(path, kind, file);
    if (opened.size > kMaxContractBytes) {
        throw ContractError(kind + " exceeds 268435456 byte limit");
    }
    const auto payload = file.read(kMaxContractBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        throw ContractError("cannot read " + kind + ": " + file.errorString());
    }
    if (payload.size() > kMaxContractBytes) {
        throw ContractError(kind + " exceeds 268435456 byte limit");
    }
    if (postReadHook) {
        postReadHook(file);
    }
    verifyContractUnchanged(path, kind, file, opened);
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        throw ContractError("invalid JSON at byte " + QString::number(parseError.offset) + ": " +
                            parseError.errorString());
    }
    detail::rejectDuplicateJsonKeys(payload);
    return document;
}

Snapshot detail::loadSnapshotFileWithPostReadHook(
    const QString &path, const SnapshotPostReadHook &postReadHook) {
    return detail::parseSnapshotDocument(
        loadJsonContractFile(path, QStringLiteral("snapshot"), postReadHook));
}

Snapshot loadSnapshotFile(const QString &path) {
    return detail::loadSnapshotFileWithPostReadHook(path, {});
}

QString invocationText(const SnapshotEntry &entry) {
    if (!entry.arguments.isEmpty()) {
        return entry.arguments.join(QLatin1Char(' '));
    }
    return entry.command;
}

}  // namespace buildscope
