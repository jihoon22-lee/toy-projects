#include "native_paths.hpp"

#include "native_error.hpp"

#include <QRegularExpression>
#include <QStringList>

#include <filesystem>
#include <sys/stat.h>

namespace buildscope::native {
namespace {

constexpr qsizetype kMaxPathChars = 1024 * 1024;
const QRegularExpression kWindowsAbsolute(QStringLiteral("^(?:[A-Za-z]:[\\\\/]|\\\\\\\\)"));

QStringList vendorComponents() {
    return {QStringLiteral("_deps"),       QStringLiteral("deps"),
            QStringLiteral("external"),    QStringLiteral("externals"),
            QStringLiteral("third-party"), QStringLiteral("third_party"),
            QStringLiteral("vendor")};
}

QStringList splitSlashes(const QString &path) {
    return path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

// posixpath.normpath: collapse duplicate separators, ".", and resolvable ".."
// while preserving exactly two leading slashes.
QString posixNormalize(const QString &value) {
    qsizetype leading = 0;
    while (leading < value.size() && value.at(leading) == QLatin1Char('/')) {
        ++leading;
    }
    int initial = leading > 0 ? 1 : 0;
    if (leading == 2) {
        initial = 2;
    }
    QStringList kept;
    for (const QString &part : splitSlashes(value)) {
        if (part == QLatin1String(".")) {
            continue;
        }
        if (part == QLatin1String("..")) {
            if (!kept.isEmpty() && kept.last() != QLatin1String("..")) {
                kept.removeLast();
                continue;
            }
            if (initial > 0) {
                continue;
            }
        }
        kept.append(part);
    }
    QString result = QString(initial, QLatin1Char('/')) + kept.join(QLatin1Char('/'));
    if (result.isEmpty()) {
        result = QStringLiteral(".");
    }
    return result;
}

// ntpath.splitdrive: return the drive/UNC prefix or an empty string.
QString windowsDrive(const QString &path, QString &rest) {
    if (path.size() >= 2) {
        if (path.startsWith(QLatin1String("\\\\")) &&
            path.at(2) != QLatin1Char('\\')) {
            const qsizetype first = path.indexOf(QLatin1Char('\\'), 2);
            if (first < 0) {
                rest = path;
                return QString();
            }
            qsizetype second = path.indexOf(QLatin1Char('\\'), first + 1);
            if (second == first + 1) {
                rest = path;
                return QString();
            }
            if (second < 0) {
                second = path.size();
            }
            rest = path.mid(second);
            return path.left(second);
        }
        if (path.at(1) == QLatin1Char(':') && path.at(0).isLetter()) {
            rest = path.mid(2);
            return path.left(2);
        }
    }
    rest = path;
    return QString();
}

// ntpath.normpath rendered with forward separators.
QString windowsNormalize(const QString &value) {
    QString replaced = value;
    replaced.replace(QLatin1Char('/'), QLatin1Char('\\'));
    QString rest;
    const QString prefix = windowsDrive(replaced, rest);
    const bool absolute = rest.startsWith(QLatin1Char('\\'));
    QStringList kept;
    for (const QString &part : rest.split(QLatin1Char('\\'), Qt::SkipEmptyParts)) {
        if (part == QLatin1String(".")) {
            continue;
        }
        if (part == QLatin1String("..")) {
            if (!kept.isEmpty() && kept.last() != QLatin1String("..")) {
                kept.removeLast();
                continue;
            }
            if (absolute) {
                continue;
            }
        }
        kept.append(part);
    }
    const QString body =
        (absolute ? QStringLiteral("\\") : QString()) + kept.join(QLatin1Char('\\'));
    QString result = prefix + body;
    if (result.isEmpty()) {
        result = QStringLiteral(".");
    }
    return result.replace(QLatin1Char('\\'), QLatin1Char('/'));
}

bool windowsAbsolute(const QString &value) {
    // PureWindowsPath.is_absolute(): a drive or UNC anchor with a root. A bare
    // UNC share carries its own implied root.
    QString rest;
    const QString drive = windowsDrive(value, rest);
    return !drive.isEmpty() &&
           (rest.startsWith(QLatin1Char('\\')) || drive.startsWith(QLatin1String("\\\\")));
}

bool posixAbsolute(const QString &value) {
    return value.startsWith(QLatin1Char('/'));
}

bool isAbsolute(const QString &value, const QString &style) {
    return style == QLatin1String("windows") ? windowsAbsolute(value) : posixAbsolute(value);
}

QString joinLexical(const QString &base, const QString &value, const QString &style) {
    if (base.isEmpty()) {
        return value;
    }
    if (style == QLatin1String("windows")) {
        // ntpath.join semantics: a drive-qualified path replaces the base
        // (unless the same drive, which keeps the base and appends), a rooted
        // path keeps the base drive, and anything else appends.
        QString valueRest;
        const QString valueDrive = windowsDrive(value, valueRest);
        QString baseRest;
        const QString baseDrive = windowsDrive(base, baseRest);
        const bool valueRooted = valueRest.startsWith(QLatin1Char('\\')) ||
                                 valueRest.startsWith(QLatin1Char('/'));
        if (valueRooted) {
            return (valueDrive.isEmpty() ? baseDrive : valueDrive) + valueRest;
        }
        if (!valueDrive.isEmpty()) {
            if (valueDrive.compare(baseDrive, Qt::CaseInsensitive) != 0) {
                return value;
            }
            const QString separator =
                base.endsWith(QLatin1Char('/')) || base.endsWith(QLatin1Char('\\'))
                    ? QString()
                    : QStringLiteral("\\");
            return base + separator + valueRest;
        }
    }
    const QString separator = style == QLatin1String("windows") ? QStringLiteral("\\")
                                                                : QStringLiteral("/");
    if (base.endsWith(QLatin1Char('/')) || base.endsWith(QLatin1Char('\\'))) {
        return base + value;
    }
    return base + separator + value;
}

QString normalizeForStyle(const QString &value, const QString &style) {
    return style == QLatin1String("windows") ? windowsNormalize(value) : posixNormalize(value);
}

// PurePath.parts with the anchor kept as the first component so relative
// checks cannot cross root boundaries.
QStringList anchorParts(const QString &path, const QString &style) {
    QStringList parts;
    if (style == QLatin1String("windows")) {
        if (path.startsWith(QLatin1String("//"))) {
            const qsizetype share = path.indexOf(QLatin1Char('/'), 2);
            if (share < 0) {
                parts.append(path);
                return parts;
            }
            const qsizetype tail = path.indexOf(QLatin1Char('/'), share + 1);
            if (tail < 0) {
                parts.append(path + QLatin1Char('/'));
                return parts;
            }
            parts.append(path.left(tail + 1));
            parts.append(splitSlashes(path.mid(tail + 1)));
            return parts;
        }
        if (path.size() >= 2 && path.at(1) == QLatin1Char(':')) {
            if (path.size() >= 3 && path.at(2) == QLatin1Char('/')) {
                parts.append(path.left(3));
                parts.append(splitSlashes(path.mid(3)));
                return parts;
            }
            parts.append(path.left(2));
            parts.append(splitSlashes(path.mid(2)));
            return parts;
        }
        if (path.startsWith(QLatin1Char('/'))) {
            parts.append(QStringLiteral("/"));
            parts.append(splitSlashes(path.mid(1)));
            return parts;
        }
        parts.append(splitSlashes(path));
        return parts;
    }
    if (path.startsWith(QLatin1String("//")) && !path.startsWith(QLatin1String("///"))) {
        const qsizetype cut = path.indexOf(QLatin1Char('/'), 2);
        parts.append(cut < 0 ? path : path.left(cut + 1));
        parts.append(splitSlashes(path.mid(cut < 0 ? path.size() : cut + 1)));
        return parts;
    }
    if (path.startsWith(QLatin1Char('/'))) {
        parts.append(QStringLiteral("/"));
        parts.append(splitSlashes(path.mid(1)));
        return parts;
    }
    parts.append(splitSlashes(path));
    return parts;
}

std::optional<QString> relativeLexical(const QString &normalized, const QString &root,
                                       const QString &style) {
    QStringList parts = anchorParts(normalized, style);
    const QStringList rootParts = anchorParts(root, style);
    if (style == QLatin1String("windows")) {
        for (QString &part : parts) {
            part = part.toLower();
        }
    }
    QStringList foldedRoot = rootParts;
    if (style == QLatin1String("windows")) {
        for (QString &part : foldedRoot) {
            part = part.toLower();
        }
    }
    if (parts.size() < foldedRoot.size()) {
        return std::nullopt;
    }
    for (qsizetype index = 0; index < foldedRoot.size(); ++index) {
        if (parts.at(index) != foldedRoot.at(index)) {
            return std::nullopt;
        }
    }
    const QStringList remainder = parts.mid(foldedRoot.size());
    if (remainder.isEmpty()) {
        return QStringLiteral(".");
    }
    return remainder.join(QLatin1Char('/'));
}

bool hasVendorComponent(const QString &displayed) {
    static const QStringList vendor = vendorComponents();
    for (const QString &part : splitSlashes(displayed)) {
        if (vendor.contains(part.toLower())) {
            return true;
        }
    }
    return false;
}

std::filesystem::path toFs(const QString &value) {
    return std::filesystem::path(value.toStdString());
}

QString fromFs(const std::filesystem::path &value) {
    return QString::fromStdString(value.generic_string());
}

struct NativeRecord {
    QString displayed;
    std::optional<QString> relative;
    std::optional<bool> exists;
};

NativeRecord nativeRecord(const QString &normalized, const QString &normalizedRoot,
                          const QString &expected) {
    const std::filesystem::path candidate = toFs(normalized);
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, error);
    NativeRecord record;
    const std::filesystem::path resolvedRoot =
        std::filesystem::weakly_canonical(toFs(normalizedRoot), error);
    if (error) {
        record.displayed = normalized;
        return record;
    }
    const QString resolvedText = fromFs(resolved);
    const QString rootText = fromFs(resolvedRoot);
    record.relative = relativeLexical(resolvedText, rootText, QStringLiteral("posix"));
    if (expected == QLatin1String("file")) {
        record.exists = std::filesystem::is_regular_file(candidate, error) && !error;
    } else if (expected == QLatin1String("directory")) {
        record.exists = std::filesystem::is_directory(candidate, error) && !error;
    } else if (expected == QLatin1String("path")) {
        record.exists = std::filesystem::exists(candidate, error) && !error;
    }
    record.displayed = record.relative.has_value() ? record.relative.value() : resolvedText;
    return record;
}

}  // namespace

bool looksWindowsPath(const QString &raw) {
    return kWindowsAbsolute.match(raw).hasMatch() ||
           (raw.contains(QLatin1Char('\\')) && !raw.contains(QLatin1Char('/')));
}

bool isNativeStyle(const QString &style) {
    return style == QLatin1String("posix");
}

QString normalizeLexical(const QString &value, const QString &base, const QString &style) {
    const QString combined = isAbsolute(value, style) ? value : joinLexical(base, value, style);
    return normalizeForStyle(combined, style);
}

QString projectRelativeLexical(const QString &value, const QString &base,
                               const QString &projectRoot, const QString &style) {
    const QString normalized = normalizeLexical(value, base, style);
    const QString rootStyle =
        looksWindowsPath(projectRoot) ? QStringLiteral("windows") : QStringLiteral("posix");
    if (rootStyle != style) {
        return normalized;
    }
    const QString normalizedRoot = normalizeLexical(projectRoot, projectRoot, rootStyle);
    const auto relative = relativeLexical(normalized, normalizedRoot, style);
    return relative.has_value() ? relative.value() : normalized;
}

QJsonObject pathRecord(const QString &value, const QString &base, const QString &projectRoot,
                       const QString &style, const QString &expected) {
    const QString normalized = normalizeLexical(value, base, style);
    const QString rootStyle =
        looksWindowsPath(projectRoot) ? QStringLiteral("windows") : QStringLiteral("posix");
    const QString normalizedRoot = normalizeLexical(projectRoot, projectRoot, rootStyle);
    QString displayed;
    std::optional<QString> relative;
    std::optional<bool> exists;
    if (rootStyle != style) {
        displayed = normalized;
    } else if (isNativeStyle(style)) {
        const NativeRecord record = nativeRecord(normalized, normalizedRoot, expected);
        displayed = record.displayed;
        relative = record.relative;
        exists = record.exists;
    } else {
        relative = relativeLexical(normalized, normalizedRoot, style);
        displayed = relative.has_value() ? relative.value() : normalized;
    }
    QString scope;
    if (hasVendorComponent(displayed)) {
        scope = QStringLiteral("vendor");
    } else if (relative.has_value()) {
        scope = QStringLiteral("project");
    } else {
        scope = QStringLiteral("system");
    }
    if (displayed.size() > kMaxPathChars) {
        throw PathNormalizationError(
            QStringLiteral("normalized path exceeds the character limit"));
    }
    QJsonObject record{
        {QStringLiteral("exists"), exists.has_value() ? QJsonValue(exists.value())
                                                      : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("path"), displayed},
        {QStringLiteral("scope"), scope},
        {QStringLiteral("style"), style},
    };
    return record;
}

std::optional<double> nativeMtime(const QString &value, const QString &base,
                                  const QString &style) {
    if (!isNativeStyle(style)) {
        return std::nullopt;
    }
    const std::filesystem::path candidate = toFs(normalizeLexical(value, base, style));
    std::error_code error;
    if (!std::filesystem::is_regular_file(candidate, error) || error) {
        return std::nullopt;
    }
    struct stat metadata {};
    if (::stat(candidate.c_str(), &metadata) != 0) {
        return std::nullopt;
    }
    return static_cast<double>(metadata.st_mtim.tv_sec) +
           static_cast<double>(metadata.st_mtim.tv_nsec) / 1e9;
}

}  // namespace buildscope::native
