#include "native_io.hpp"

#include "native_error.hpp"

#include <QFileInfo>
#include <QRandomGenerator>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace buildscope::native {
namespace {

struct FileIdentity {
    dev_t device = 0;
    ino_t inode = 0;
    mode_t type = 0;
};

FileIdentity objectIdentity(const struct stat &metadata) {
    return {metadata.st_dev, metadata.st_ino, metadata.st_mode & S_IFMT};
}

bool sameObject(const FileIdentity &first, const FileIdentity &second) {
    return first.device == second.device && first.inode == second.inode &&
           first.type == second.type;
}

struct StableIdentity {
    FileIdentity object;
    off_t size = 0;
    long mtime = 0;
    long ctime = 0;
    time_t mtimeSec = 0;
    time_t ctimeSec = 0;
};

StableIdentity stableIdentity(const struct stat &metadata) {
    return {objectIdentity(metadata),
            metadata.st_size,
            metadata.st_mtim.tv_nsec,
            metadata.st_ctim.tv_nsec,
            metadata.st_mtim.tv_sec,
            metadata.st_ctim.tv_sec};
}

bool sameStable(const StableIdentity &first, const StableIdentity &second) {
    return sameObject(first.object, second.object) && first.size == second.size &&
           first.mtime == second.mtime && first.ctime == second.ctime &&
           first.mtimeSec == second.mtimeSec && first.ctimeSec == second.ctimeSec;
}

QString absoluteOf(const QString &path) {
    QFileInfo info(path);
    return info.absoluteFilePath();
}

QString ioMessage(const QString &prefix) {
    return QStringLiteral("%1: %2").arg(prefix, QString::fromLocal8Bit(::strerror(errno)));
}

std::optional<FileIdentity> metadataAt(int parentDescriptor, const QByteArray &name) {
    struct stat metadata {};
    if (::fstatat(parentDescriptor, name.constData(), &metadata, 0) != 0) {
        return std::nullopt;
    }
    return objectIdentity(metadata);
}

FileIdentity sourceIdentity(const QString &source) {
    struct stat metadata {};
    if (::stat(source.toUtf8().constData(), &metadata) != 0) {
        throw SnapshotIoError(
            QStringLiteral("cannot inspect protected compilation database: %1")
                .arg(QString::fromLocal8Bit(::strerror(errno))));
    }
    return objectIdentity(metadata);
}

void rejectProtectedAliasAt(int parentDescriptor, const QByteArray &destinationName,
                            const QStringList &sources) {
    const auto destination = metadataAt(parentDescriptor, destinationName);
    if (!destination.has_value()) {
        return;
    }
    for (const QString &source : sources) {
        if (sameObject(destination.value(), sourceIdentity(source))) {
            throw SnapshotIoError(QStringLiteral(
                "snapshot output must not overwrite the compilation database"));
        }
    }
}

int openParentNoFollow(const QString &parent) {
    const QByteArray absolute = QFileInfo(parent).absoluteFilePath().toUtf8();
    const int flags = O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW;
    int current = ::open("/", flags);
    if (current < 0) {
        return -1;
    }
    const QList<QByteArray> components = absolute.split('/');
    for (const QByteArray &component : components) {
        if (component.isEmpty()) {
            continue;
        }
        const int next = ::openat(current, component.constData(), flags);
        ::close(current);
        if (next < 0) {
            return -1;
        }
        current = next;
    }
    return current;
}

QByteArray temporaryName(const QByteArray &destinationName) {
    QByteArray suffix;
    suffix.reserve(24);
    static const char digits[] = "0123456789abcdef";
    for (int index = 0; index < 24; ++index) {
        suffix += digits[QRandomGenerator::global()->bounded(16)];
    }
    return "." + destinationName + "." + suffix + ".tmp";
}

int allocateTemporary(int parentDescriptor, const QByteArray &destinationName,
                      QByteArray &name) {
    const int flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC;
    for (int attempt = 0; attempt < 128; ++attempt) {
        name = temporaryName(destinationName);
        const int descriptor =
            ::openat(parentDescriptor, name.constData(), flags, 0600);
        if (descriptor >= 0) {
            return descriptor;
        }
        if (errno != EEXIST) {
            break;
        }
    }
    throw SnapshotIoError(QStringLiteral("cannot allocate a unique snapshot temporary file"));
}

void writeAll(int descriptor, const QByteArray &payload) {
    qint64 written = 0;
    while (written < payload.size()) {
        const ssize_t count =
            ::write(descriptor, payload.constData() + written, payload.size() - written);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw SnapshotIoError(
                QStringLiteral("cannot write snapshot safely: %1")
                    .arg(QString::fromLocal8Bit(::strerror(errno))));
        }
        written += count;
    }
    if (::fsync(descriptor) != 0) {
        throw SnapshotIoError(
            QStringLiteral("cannot write snapshot safely: %1")
                .arg(QString::fromLocal8Bit(::strerror(errno))));
    }
}

}  // namespace

QByteArray readBoundedRegular(const QString &path, qint64 limit, std::atomic_bool *cancel) {
    const QString candidate = absoluteOf(path);
    const QByteArray encoded = candidate.toUtf8();
    struct stat namedBefore {};
    if (::lstat(encoded.constData(), &namedBefore) != 0) {
        throw SnapshotIoError(
            QStringLiteral("cannot open compilation database safely: %1")
                .arg(QString::fromLocal8Bit(::strerror(errno))));
    }
    if (S_ISLNK(namedBefore.st_mode)) {
        throw SnapshotIoError(
            QStringLiteral("cannot open compilation database safely: final symbolic links "
                           "are forbidden"));
    }
    const int descriptor =
        ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (descriptor < 0) {
        throw SnapshotIoError(
            QStringLiteral("cannot open compilation database safely: %1")
                .arg(QString::fromLocal8Bit(::strerror(errno))));
    }
    QByteArray payload;
    try {
        struct stat before {};
        if (::fstat(descriptor, &before) != 0) {
            throw SnapshotIoError(ioMessage(QStringLiteral(
                "cannot read compilation database safely")));
        }
        if (!sameObject(objectIdentity(namedBefore), objectIdentity(before))) {
            throw SnapshotIoError(QStringLiteral(
                "compilation database changed while it was being opened"));
        }
        if (!S_ISREG(before.st_mode)) {
            throw SnapshotIoError(
                QStringLiteral("compilation database must be a regular file"));
        }
        if (before.st_size > limit) {
            throw SnapshotIoError(
                QStringLiteral("compilation database exceeds %1 byte limit").arg(limit));
        }
        qint64 size = 0;
        while (size <= limit) {
            if(cancel && cancel->load()) throw SnapshotIoError(QStringLiteral("input read cancelled"));
            QByteArray chunk;
            chunk.resize(static_cast<int>(qMin<qint64>(1024 * 1024, limit + 1 - size)));
            const ssize_t count = ::read(descriptor, chunk.data(), chunk.size());
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw SnapshotIoError(ioMessage(
                    QStringLiteral("cannot read compilation database safely")));
            }
            if (count == 0) {
                break;
            }
            chunk.resize(static_cast<int>(count));
            payload += chunk;
            size += count;
        }
        if (size > limit) {
            throw SnapshotIoError(
                QStringLiteral("compilation database exceeds %1 byte limit").arg(limit));
        }
        struct stat after {};
        if (::fstat(descriptor, &after) != 0) {
            throw SnapshotIoError(ioMessage(
                QStringLiteral("cannot read compilation database safely")));
        }
        struct stat namedAfter {};
        if (::lstat(encoded.constData(), &namedAfter) != 0) {
            throw SnapshotIoError(QStringLiteral(
                "compilation database changed while it was being read"));
        }
        if (!sameStable(stableIdentity(before), stableIdentity(after)) ||
            !sameObject(objectIdentity(after), objectIdentity(namedAfter)) ||
            size != after.st_size) {
            throw SnapshotIoError(QStringLiteral(
                "compilation database changed while it was being read"));
        }
    } catch (...) {
        ::close(descriptor);
        throw;
    }
    ::close(descriptor);
    return payload;
}

void writeAtomicText(const QString &target, const QString &text,
                     const QStringList &protectedPaths) {
    if (protectedPaths.isEmpty()) {
        throw SnapshotIoError(QStringLiteral("at least one protected input is required"));
    }
    const QString destination = absoluteOf(target);
    QStringList sources;
    for (const QString &path : protectedPaths) {
        sources.append(absoluteOf(path));
    }
    if (sources.contains(destination)) {
        throw SnapshotIoError(QStringLiteral(
            "snapshot output must not overwrite the compilation database"));
    }
    const QFileInfo info(destination);
    const int parentDescriptor = openParentNoFollow(info.absolutePath());
    if (parentDescriptor < 0) {
        throw SnapshotIoError(
            QStringLiteral("cannot write snapshot safely: %1")
                .arg(QStringLiteral("cannot anchor the output directory")));
    }
    const QByteArray destinationName = info.fileName().toUtf8();
    QByteArray temporary;
    int descriptor = -1;
    try {
        rejectProtectedAliasAt(parentDescriptor, destinationName, sources);
        descriptor = allocateTemporary(parentDescriptor, destinationName, temporary);
        writeAll(descriptor, text.toUtf8());
        ::close(descriptor);
        descriptor = -1;
        rejectProtectedAliasAt(parentDescriptor, destinationName, sources);
        if (::renameat(parentDescriptor, temporary.constData(), parentDescriptor,
                       destinationName.constData()) != 0) {
            throw SnapshotIoError(
                QStringLiteral("cannot write snapshot safely: %1")
                    .arg(QString::fromLocal8Bit(::strerror(errno))));
        }
        temporary.clear();
    } catch (...) {
        if (descriptor >= 0) {
            ::close(descriptor);
        }
        if (!temporary.isEmpty()) {
            ::unlinkat(parentDescriptor, temporary.constData(), 0);
        }
        ::close(parentDescriptor);
        throw;
    }
    ::close(parentDescriptor);
}

}  // namespace buildscope::native
