#include "native_include.hpp"

#include "native_error.hpp"
#include "native_glob.hpp"
#include "native_replay.hpp"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <memory>

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace buildscope::native {
namespace {

const QRegularExpression kTraceLine(QStringLiteral("^(\\.+) (.+)$"));
const QRegularExpression
    kIncludeLine(QStringLiteral("^\\s*#\\s*include\\s*([<\"])([^>\"\\r\\n]+)[>\"]"));
const QRegularExpression
    kGccMissingLine(QStringLiteral("^(.+?):([0-9]+)(?::[0-9]+)?: (?:fatal )?error: "
                                   "([^:\\r\\n]+): No such file or directory$"));
const QRegularExpression
    kClangMissingLine(QStringLiteral("^(.+?):([0-9]+)(?::[0-9]+)?: (?:fatal )?error: "
                                     "['<](.+?)[>'] file not found$"));
const QSet<QString> kVendorParts = {
    QStringLiteral("_deps"),     QStringLiteral("deps"),        QStringLiteral("external"),
    QStringLiteral("externals"), QStringLiteral("third-party"), QStringLiteral("third_party"),
    QStringLiteral("vendor"),
};

std::filesystem::path resolveSoft(const std::filesystem::path &path) {
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : resolved;
}

bool isWithin(const std::filesystem::path &path, const std::filesystem::path &root) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

QString posixOf(const std::filesystem::path &path) {
    return QString::fromStdString(path.generic_string());
}

QString displayPath(const std::filesystem::path &path,
                    const std::filesystem::path &projectRoot) {
    const std::filesystem::path resolved = resolveSoft(path);
    const auto relative = resolved.lexically_relative(projectRoot);
    if (!relative.empty() && *relative.begin() != "..") {
        return posixOf(relative);
    }
    return posixOf(resolved);
}

QString classification(const std::optional<std::filesystem::path> &path,
                       const std::filesystem::path &projectRoot,
                       const std::filesystem::path &buildRoot) {
    if (!path.has_value()) {
        return QStringLiteral("missing");
    }
    const std::filesystem::path resolved = resolveSoft(path.value());
    if (!isWithin(resolved, projectRoot)) {
        return QStringLiteral("system");
    }
    const auto relative = resolved.lexically_relative(projectRoot);
    const auto buildRelative = isWithin(buildRoot, projectRoot)
                                   ? buildRoot.lexically_relative(projectRoot)
                                   : std::filesystem::path();
    if (!buildRelative.empty()) {
        const QString first = QString::fromStdString(buildRelative.begin()->string());
        if ((first == QLatin1String("build") || first == QLatin1String("out") ||
             first == QLatin1String(".build") ||
             first.startsWith(QLatin1String("cmake-build-"))) &&
            isWithin(resolved, buildRoot)) {
            return QStringLiteral("generated");
        }
    }
    for (const auto &part : relative) {
        if (kVendorParts.contains(QString::fromStdString(part.string()).toLower())) {
            return QStringLiteral("vendor");
        }
    }
    return QStringLiteral("project");
}

struct FileState {
    dev_t device = 0;
    ino_t inode = 0;
    off_t size = 0;
    timespec mtime{};
    timespec ctime{};
};

FileState fileState(const struct stat &metadata) {
    return {metadata.st_dev, metadata.st_ino, metadata.st_size, metadata.st_mtim,
            metadata.st_ctim};
}

bool sameState(const FileState &first, const FileState &second) {
    return first.device == second.device && first.inode == second.inode &&
           first.size == second.size && first.mtime.tv_sec == second.mtime.tv_sec &&
           first.mtime.tv_nsec == second.mtime.tv_nsec &&
           first.ctime.tv_sec == second.ctime.tv_sec &&
           first.ctime.tv_nsec == second.ctime.tv_nsec;
}

struct Directive {
    int line = 0;
    QString requested;
    QString delimiter;
};

QList<Directive> directives(const std::filesystem::path &path) {
    QList<Directive> found;
    analysisCheckpoint();
    auto *control = activeAnalysisControl();
    if (control)
        control->readFile();
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0) {
        throw IncludeAnalysisError(
            QStringLiteral("cannot read include source: %1").arg(posixOf(path)));
    }
    const auto closer = [](int *fd) {
        ::close(*fd);
        delete fd;
    };
    std::unique_ptr<int, decltype(closer)> handle(new int(descriptor), closer);
    struct stat before{};
    if (::fstat(descriptor, &before) != 0 || !S_ISREG(before.st_mode) || before.st_size < 0 ||
        before.st_size > kMaxSourceBytes) {
        throw IncludeAnalysisError(
            QStringLiteral("include source is not a bounded regular file: %1")
                .arg(posixOf(path)));
    }
    QByteArray payload;
    std::array<char, 65536> buffer;
    bool failed = false;
    while (payload.size() < before.st_size) {
        analysisCheckpoint();
        auto wanted = std::min(qint64(buffer.size()), qint64(before.st_size) - payload.size());
        if (control) {
            if (control->remainingSourceBytes() == 0)
                control->readBytes(1); // raises the precise exhausted-budget diagnostic
            wanted = std::min(wanted, control->remainingSourceBytes());
        }
        const ssize_t count = ::read(descriptor, buffer.data(), size_t(wanted));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            failed = true;
        if (count <= 0)
            break;
        if (control)
            control->readBytes(count);
        payload.append(buffer.data(), static_cast<qsizetype>(count));
        if (control && control->sourceReadObserver)
            control->sourceReadObserver(posixOf(path), payload.size());
    }
    struct stat after{};
    const bool inspected = ::fstat(descriptor, &after) == 0;
    struct stat namedAfter{};
    if (failed || !inspected || ::stat(path.c_str(), &namedAfter) != 0 ||
        payload.size() != before.st_size || payload.size() > kMaxSourceBytes ||
        !sameState(fileState(before), fileState(after)) ||
        !sameState(fileState(after), fileState(namedAfter))) {
        throw IncludeAnalysisError(
            QStringLiteral("include source changed or could not be read completely: %1")
                .arg(posixOf(path)));
    }
    const QString text = QString::fromUtf8(payload);
    int lineNumber = 0;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        ++lineNumber;
        if ((lineNumber & 255) == 0)
            analysisCheckpoint();
        const auto match = kIncludeLine.match(line);
        if (match.hasMatch()) {
            found.append({lineNumber, match.captured(2),
                          match.captured(1) == QLatin1String("\"") ? QStringLiteral("quote")
                                                                   : QStringLiteral("angle")});
        }
    }
    return found;
}

using IncludeRoots = QList<QPair<QString, std::filesystem::path>>;

IncludeRoots includeRoots(const QJsonObject &entry, const std::filesystem::path &projectRoot,
                          const std::filesystem::path &cwd) {
    IncludeRoots roots;
    for (const QJsonValue &value : entry.value(QStringLiteral("normalized"))
                                       .toObject()
                                       .value(QStringLiteral("include_paths"))
                                       .toArray()) {
        const QJsonObject record = value.toObject();
        const std::filesystem::path raw =
            record.value(QStringLiteral("path")).toString().toStdString();
        std::filesystem::path root;
        if (record.value(QStringLiteral("scope")).toString() == QLatin1String("project")) {
            root = projectRoot / raw;
        } else {
            root = raw.is_absolute() ? raw : cwd / raw;
        }
        roots.append({record.value(QStringLiteral("kind")).toString(), resolveSoft(root)});
    }
    return roots;
}

IncludeRoots orderedSearchRoots(const std::filesystem::path &parent, const QString &delimiter,
                                const IncludeRoots &roots) {
    IncludeRoots ordered;
    if (delimiter == QLatin1String("quote")) {
        ordered.append({QStringLiteral("current"), parent.parent_path()});
        for (const auto &root : roots) {
            if (root.first == QLatin1String("quote")) {
                ordered.append(root);
            }
        }
    }
    for (const QString &kind : {QStringLiteral("include"), QStringLiteral("framework"),
                                QStringLiteral("system"), QStringLiteral("after")}) {
        for (const auto &root : roots) {
            if (root.first == kind) {
                ordered.append(root);
            }
        }
    }
    return ordered;
}

struct SearchResult {
    QJsonArray records;
    QStringList alternatives;
};

bool isRegularFile(const std::filesystem::path &path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

SearchResult searchRecords(const std::filesystem::path &parent, const QString &requested,
                           const QString &delimiter, const IncludeRoots &roots,
                           const std::optional<std::filesystem::path> &selected,
                           const std::filesystem::path &projectRoot) {
    const IncludeRoots ordered = orderedSearchRoots(parent, delimiter, roots);
    SearchResult result;
    std::optional<std::filesystem::path> selectedResolved;
    if (selected.has_value()) {
        selectedResolved = resolveSoft(selected.value());
    }
    bool selectedSeen = false;
    int order = 0;
    for (const auto &[kind, directory] : ordered) {
        analysisCheckpoint();
        const std::filesystem::path candidate =
            resolveSoft(directory / requested.toStdString());
        const bool exists = isRegularFile(candidate);
        const bool matchesSelected =
            exists && selectedResolved.has_value() && candidate == selectedResolved.value();
        const bool chosen = matchesSelected && !selectedSeen;
        selectedSeen = selectedSeen || chosen;
        if (exists && !matchesSelected) {
            result.alternatives.append(displayPath(candidate, projectRoot));
        }
        result.records.append(QJsonObject{
            {QStringLiteral("candidate"), displayPath(candidate, projectRoot)},
            {QStringLiteral("exists"), exists},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("order"), order++},
            {QStringLiteral("selected"), chosen},
        });
    }
    if (selectedResolved.has_value() && !selectedSeen) {
        result.records.append(QJsonObject{
            {QStringLiteral("candidate"), displayPath(selectedResolved.value(), projectRoot)},
            {QStringLiteral("exists"), isRegularFile(selectedResolved.value())},
            {QStringLiteral("kind"), QStringLiteral("compiler")},
            {QStringLiteral("order"), order},
            {QStringLiteral("selected"), true},
        });
    }
    result.alternatives.removeDuplicates();
    result.alternatives.sort();
    return result;
}

struct MatchedDirective {
    int line = 0;
    QString requested;
    QString delimiter;
};

MatchedDirective matchDirective(const std::filesystem::path &parent,
                                const std::filesystem::path &child, const IncludeRoots &roots,
                                QMap<std::filesystem::path, int> &cursors,
                                const std::filesystem::path &projectRoot,
                                QMap<std::filesystem::path, QList<Directive>> &directiveCache) {
    if (!directiveCache.contains(parent)) {
        directiveCache.insert(parent, directives(parent));
    }
    const QList<Directive> &found = directiveCache[parent];
    if (found.isEmpty()) {
        return {0, QString::fromStdString(child.filename().string()),
                QStringLiteral("unknown")};
    }
    const int start = cursors.value(parent, 0);
    std::optional<MatchedDirective> unique;
    for (int offset = 0; offset < found.size(); ++offset) {
        const int index = (start + offset) % found.size();
        const Directive &directive = found.at(index);
        const SearchResult searches = searchRecords(
            parent, directive.requested, directive.delimiter, roots, child, projectRoot);
        bool selected = false;
        for (const QJsonValue &record : searches.records) {
            if (record.toObject().value(QStringLiteral("selected")).toBool() &&
                record.toObject().value(QStringLiteral("kind")).toString() !=
                    QLatin1String("compiler")) {
                selected = true;
                break;
            }
        }
        if (selected || posixOf(child).endsWith(QLatin1Char('/') + directive.requested)) {
            // -H has no directive line. Multiple lexical matches (including
            // inactive #if branches) cannot justify choosing one source line.
            if (unique.has_value())
                return {0, QString::fromStdString(child.filename().string()),
                        QStringLiteral("unknown")};
            unique = MatchedDirective{directive.line, directive.requested, directive.delimiter};
            cursors.insert(parent, index + 1);
        }
    }
    return unique.value_or(MatchedDirective{
        0, QString::fromStdString(child.filename().string()), QStringLiteral("unknown")});
}

QJsonObject edgeRecord(const std::filesystem::path &parent,
                       const std::optional<std::filesystem::path> &child, int line,
                       const QString &requested, const QString &delimiter,
                       const IncludeRoots &roots, const std::filesystem::path &projectRoot,
                       const std::filesystem::path &buildRoot, const QString &evidence,
                       const QString &locationEvidence = QString()) {
    const SearchResult searches =
        searchRecords(parent, requested, delimiter, roots, child, projectRoot);
    return QJsonObject{
        {QStringLiteral("alternatives"), QJsonArray::fromStringList(searches.alternatives)},
        {QStringLiteral("classification"), classification(child, projectRoot, buildRoot)},
        {QStringLiteral("delimiter"), delimiter},
        {QStringLiteral("evidence"), evidence},
        {QStringLiteral("line"), line},
        {QStringLiteral("location_evidence"),
         locationEvidence.isEmpty()
             ? (line > 0 ? QStringLiteral("source-scan") : QStringLiteral("unavailable"))
             : locationEvidence},
        {QStringLiteral("parent"), displayPath(parent, projectRoot)},
        {QStringLiteral("requested"), requested},
        {QStringLiteral("resolved"), child.has_value()
                                         ? QJsonValue(displayPath(child.value(), projectRoot))
                                         : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("search"), searches.records},
    };
}

struct TraceEdgeResult {
    bool hasEdge = false;
    std::filesystem::path parent;
    std::filesystem::path child;
};

TraceEdgeResult traceEdge(const QRegularExpressionMatch &match,
                          QList<std::optional<std::filesystem::path>> &stack,
                          const std::filesystem::path &cwd) {
    const int depth = static_cast<int>(match.captured(1).size());
    if (depth > 4096 || depth > stack.size() || depth < 1) {
        throw IncludeAnalysisError(
            QStringLiteral("compiler include trace has an invalid depth"));
    }
    const QString rawPath = match.captured(2);
    if (rawPath.startsWith(QLatin1Char('<')) && rawPath.endsWith(QLatin1Char('>'))) {
        while (stack.size() > depth) {
            stack.removeLast();
        }
        while (stack.size() < depth + 1) {
            stack.append(std::nullopt);
        }
        stack[depth] = std::nullopt;
        return {};
    }
    const std::filesystem::path lexical = rawPath.toStdString();
    std::error_code error;
    const std::filesystem::path child =
        std::filesystem::canonical(lexical.is_absolute() ? lexical : cwd / lexical, error);
    if (error) {
        throw IncludeAnalysisError(
            QStringLiteral("compiler include trace references a stale path"));
    }
    const auto parent = stack.at(depth - 1);
    while (stack.size() > depth) {
        stack.removeLast();
    }
    while (stack.size() < depth + 1) {
        stack.append(std::nullopt);
    }
    stack[depth] = child;
    TraceEdgeResult result;
    if (parent.has_value()) {
        result.hasEdge = true;
        result.parent = parent.value();
        result.child = child;
    }
    return result;
}

struct TraceEdges {
    QList<QPair<std::filesystem::path, std::filesystem::path>> edges;
    QStringList unexpected;
};

TraceEdges traceEdges(const QString &stderrText, const std::filesystem::path &source,
                      const std::filesystem::path &cwd) {
    QList<std::optional<std::filesystem::path>> stack{source};
    TraceEdges result;
    bool trailer = false;
    int entries = 0;
    for (QString line : stderrText.split(QLatin1Char('\n'))) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }
        if (line == QLatin1String("Multiple include guards may be useful for:")) {
            if (trailer) {
                result.unexpected.append(line);
            }
            trailer = true;
            continue;
        }
        ++entries;
        if (entries > kMaxEdges) {
            result.unexpected.append(
                QStringLiteral("compiler include trace exceeds the edge limit"));
            break;
        }
        if (trailer) {
            std::filesystem::path candidate = line.toStdString();
            candidate = candidate.is_absolute() ? candidate : cwd / candidate;
            if (!isRegularFile(candidate)) {
                result.unexpected.append(line);
            }
            continue;
        }
        const auto match = kTraceLine.match(line);
        if (!match.hasMatch()) {
            result.unexpected.append(line);
            continue;
        }
        try {
            const TraceEdgeResult edge = traceEdge(match, stack, cwd);
            if (edge.hasEdge)
                result.edges.append({edge.parent, edge.child});
        } catch (const IncludeAnalysisError &error) {
            result.unexpected.append(QString::fromUtf8(error.what()));
            break;
        }
    }
    return result;
}

QJsonArray missingEdges(const QString &stderrText, const IncludeRoots &roots,
                        const std::filesystem::path &projectRoot,
                        const std::filesystem::path &buildRoot,
                        const std::filesystem::path &cwd) {
    QJsonArray records;
    for (const QString &raw : stderrText.split(QLatin1Char('\n'))) {
        auto match = kGccMissingLine.match(raw);
        if (!match.hasMatch()) {
            match = kClangMissingLine.match(raw);
        }
        if (!match.hasMatch()) {
            continue;
        }
        const std::filesystem::path lexical = match.captured(1).toStdString();
        const std::filesystem::path parent =
            resolveSoft(lexical.is_absolute() ? lexical : cwd / lexical);
        records.append(edgeRecord(parent, std::nullopt, match.captured(2).toInt(),
                                  match.captured(3), QStringLiteral("unknown"), roots,
                                  projectRoot, buildRoot, QStringLiteral("compiler-measured"),
                                  QStringLiteral("compiler-diagnostic")));
    }
    return records;
}

QJsonObject unavailable(const QString &code, const QString &message) {
    return QJsonObject{
        {QStringLiteral("command"), QJsonArray()},
        {QStringLiteral("diagnostics"),
         QJsonArray{QJsonObject{
             {QStringLiteral("code"), code},
             {QStringLiteral("message"), message},
             {QStringLiteral("severity"), QStringLiteral("warning")},
         }}},
        {QStringLiteral("duration_ms"), 0},
        {QStringLiteral("edges"), QJsonArray()},
        {QStringLiteral("evidence"), QStringLiteral("unavailable")},
        {QStringLiteral("complete"), false},
        {QStringLiteral("stop_reason"), message},
        {QStringLiteral("fallback"), QJsonValue::Null},
    };
}

} // namespace

QJsonObject estimateEntry(const QJsonObject &entry, const QString &projectRootValue) {
    const std::filesystem::path projectRoot =
        std::filesystem::canonical(projectRootValue.toStdString());
    const auto [cwdValue, sourceValue] = nativeEntryPaths(entry, projectRootValue);
    const std::filesystem::path cwd = cwdValue.toStdString();
    const std::filesystem::path source = sourceValue.toStdString();
    const IncludeRoots roots = includeRoots(entry, projectRoot, cwd);
    QList<std::filesystem::path> pending{source};
    QSet<std::filesystem::path> visited;
    QJsonArray edges;
    QString stopReason;
    try {
        while (!pending.isEmpty()) {
            analysisCheckpoint();
            const std::filesystem::path parent = pending.takeLast();
            if (visited.contains(parent)) {
                continue;
            }
            visited.insert(parent);
            for (const Directive &directive : directives(parent)) {
                const SearchResult search =
                    searchRecords(parent, directive.requested, directive.delimiter, roots,
                                  std::nullopt, projectRoot);
                std::optional<std::filesystem::path> selected;
                for (const QJsonValue &record : search.records) {
                    const QJsonObject item = record.toObject();
                    if (!item.value(QStringLiteral("exists")).toBool()) {
                        continue;
                    }
                    const std::filesystem::path candidate =
                        item.value(QStringLiteral("candidate")).toString().toStdString();
                    selected = resolveSoft(candidate.is_absolute() ? candidate
                                                                   : projectRoot / candidate);
                    break;
                }
                QJsonObject record = edgeRecord(parent, selected, directive.line,
                                                directive.requested, directive.delimiter, roots,
                                                projectRoot, cwd, QStringLiteral("estimated"));
                if (!selected.has_value()) {
                    record.insert(QStringLiteral("classification"),
                                  QStringLiteral("unresolved"));
                }
                if (auto *control = activeAnalysisControl())
                    control->addEdge();
                edges.append(record);
                if (edges.size() > kMaxEdges) {
                    throw IncludeAnalysisError(
                        QStringLiteral("estimated include graph exceeds the edge limit"));
                }
                if (selected.has_value() && isWithin(selected.value(), projectRoot)) {
                    pending.append(selected.value());
                }
            }
        }
    } catch (const IncludeAnalysisError &error) {
        stopReason = QString::fromUtf8(error.what());
    }
    QJsonArray diagnostics;
    if (!stopReason.isEmpty())
        diagnostics.append(QJsonObject{{"code", "include-estimate-partial"},
                                       {"message", stopReason},
                                       {"severity", "warning"}});
    return QJsonObject{
        {QStringLiteral("command"), QJsonArray()},
        {QStringLiteral("diagnostics"), diagnostics},
        {QStringLiteral("complete"), stopReason.isEmpty()},
        {QStringLiteral("stop_reason"), stopReason},
        {QStringLiteral("fallback"), QJsonValue::Null},
        {QStringLiteral("duration_ms"), 0},
        {QStringLiteral("edges"), edges},
        {QStringLiteral("evidence"), QStringLiteral("estimated")},
    };
}

QJsonObject analyzeEntry(const QJsonObject &entry, const QString &projectRootValue) {
    const std::filesystem::path projectRoot =
        std::filesystem::canonical(projectRootValue.toStdString());
    const auto [command, cwdValue, sourceValue] = buildTraceCommand(entry, projectRootValue);
    const std::filesystem::path cwd = cwdValue.toStdString(),
                                source = sourceValue.toStdString();
    const IncludeRoots roots = includeRoots(entry, projectRoot, cwd);
    auto trace = runTraceControlled(command, cwdValue);
    const TraceEdges traced = traceEdges(trace.text, source, cwd);
    QMap<std::filesystem::path, int> cursors;
    QMap<std::filesystem::path, QList<Directive>> directiveCache;
    QJsonArray edges, diagnostics;
    auto *control = activeAnalysisControl();
    QString stopReason = trace.stopReason;
    for (const auto &[parent, child] : traced.edges) {
        MatchedDirective matched{0, QString::fromStdString(child.filename().string()),
                                 QStringLiteral("unknown")};
        bool counted = false;
        try {
            if (control)
                control->addEdge();
            counted = true;
            matched =
                matchDirective(parent, child, roots, cursors, projectRoot, directiveCache);
            edges.append(edgeRecord(parent, child, matched.line, matched.requested,
                                    matched.delimiter, roots, projectRoot, cwd,
                                    QStringLiteral("compiler-measured")));
        } catch (const IncludeAnalysisError &error) {
            if (stopReason.isEmpty())
                stopReason = QString::fromUtf8(error.what());
            // Actual -H evidence survives unavailable source scans/time limits.
            // It has no guessed source line or synthesized search-order claim.
            if (!counted || edges.size() >= (control ? control->limits.unitEdges : kMaxEdges))
                break;
            edges.append(QJsonObject{
                {"alternatives", QJsonArray()},
                {"classification", classification(child, projectRoot, cwd)},
                {"delimiter", "unknown"},
                {"evidence", "compiler-measured"},
                {"line", 0},
                {"location_evidence", "unavailable"},
                {"parent", displayPath(parent, projectRoot)},
                {"requested", QString::fromStdString(child.filename().string())},
                {"resolved", displayPath(child, projectRoot)},
                {"search",
                 QJsonArray{QJsonObject{{"candidate", displayPath(child, projectRoot)},
                                        {"exists", true},
                                        {"kind", "compiler"},
                                        {"order", 0},
                                        {"selected", true}}}}});
        }
    }
    // Compiler-diagnostic missing edges are useful on nonzero exit, but only
    // while search work still fits the analysis budget.
    try {
        for (const auto &edge : missingEdges(trace.text, roots, projectRoot, cwd, cwd)) {
            if (control)
                control->addEdge();
            edges.append(edge);
        }
    } catch (const IncludeAnalysisError &error) {
        if (stopReason.isEmpty())
            stopReason = QString::fromUtf8(error.what());
    }
    if (trace.exitCode != 0) {
        if (stopReason.isEmpty())
            stopReason = QStringLiteral("compiler exited with status %1").arg(trace.exitCode);
        diagnostics.append(QJsonObject{{"code", "compiler-trace-failed"},
                                       {"message", stopReason},
                                       {"severity", "warning"}});
    }
    if (!traced.unexpected.isEmpty()) {
        diagnostics.append(QJsonObject{
            {"code", "compiler-trace-diagnostics"},
            {"message", traced.unexpected.mid(0, 8).join(QLatin1Char('\n')).left(8192)},
            {"severity", "warning"}});
        if (stopReason.isEmpty())
            stopReason = QStringLiteral("compiler emitted non-trace or malformed diagnostics");
    }
    if (!stopReason.isEmpty() && diagnostics.isEmpty())
        diagnostics.append(QJsonObject{{"code", "compiler-trace-partial"},
                                       {"message", stopReason},
                                       {"severity", "warning"}});
    return QJsonObject{{"command", QJsonArray::fromStringList(command)},
                       {"diagnostics", diagnostics},
                       {"duration_ms", trace.durationMs},
                       {"edges", edges},
                       {"evidence", "compiler-measured"},
                       {"complete", trace.complete && stopReason.isEmpty()},
                       {"stop_reason", stopReason},
                       {"fallback", QJsonValue::Null}};
}

bool matchesUnitGlob(const QJsonObject &entry, const QStringList &unitGlobs) {
    const QJsonObject normalized = entry.value(QStringLiteral("normalized")).toObject();
    const QString file = normalized.value(QStringLiteral("source"))
                             .toObject()
                             .value(QStringLiteral("path"))
                             .toString();
    const bool windows = normalized.value(QStringLiteral("command_style")).toString() ==
                         QLatin1String("windows");
    for (const QString &pattern : unitGlobs) {
        if (pattern.startsWith("exact:")) {
            if (file.compare(pattern.mid(6),
                             windows ? Qt::CaseInsensitive : Qt::CaseSensitive) == 0)
                return true;
            continue;
        }
        if (globMatches(file, pattern, windows)) {
            return true;
        }
    }
    return false;
}

void appendDiagnostic(QJsonObject &analysis, const QString &code, const QString &message) {
    QJsonArray diagnostics = analysis.value(QStringLiteral("diagnostics")).toArray();
    diagnostics.append(QJsonObject{
        {QStringLiteral("code"), code},
        {QStringLiteral("message"), message},
        {QStringLiteral("severity"), QStringLiteral("warning")},
    });
    analysis.insert(QStringLiteral("diagnostics"), diagnostics);
}

void annotateSnapshotControlled(QJsonObject &snapshot, const QString &projectRoot,
                                const QString &mode, AnalysisControl &control,
                                const QStringList &unitGlobs) {
    if (mode != "estimate" && mode != "compiler" && mode != "delayed")
        throw IncludeAnalysisError(
            QStringLiteral("unsupported include analysis mode: %1").arg(mode));
    AnalysisScope scope(&control);
    auto entries = snapshot.value("entries").toArray();
    for (qsizetype index = 0; index < entries.size(); ++index) {
        auto entry = entries[index].toObject();
        QJsonObject analysis;
        try {
            control.beginUnit();
            // Even compiler mode obtains a separate lexical fallback first.
            // Its provenance never becomes compiler-measured on replay failure.
            auto estimate = estimateEntry(entry, projectRoot);
            const bool replay =
                mode == "compiler" || (mode == "delayed" && matchesUnitGlob(entry, unitGlobs));
            if (!replay)
                analysis = estimate;
            else {
                try {
                    analysis = analyzeEntry(entry, projectRoot);
                    if (!analysis.value("complete").toBool())
                        analysis.insert("fallback", estimate);
                } catch (const IncludeAnalysisError &error) {
                    analysis = unavailable("include-analysis-replay-failed",
                                           QString::fromUtf8(error.what()));
                    analysis.insert("fallback", estimate);
                }
            }
        } catch (const IncludeAnalysisError &error) {
            analysis = unavailable("include-analysis-budget", QString::fromUtf8(error.what()));
        } catch (const std::filesystem::filesystem_error &error) {
            analysis = unavailable("include-analysis-source", QString::fromUtf8(error.what()));
        }
        entry.insert("include_analysis", analysis);
        entries[index] = entry;
        if (control.progress)
            control.progress(int(index + 1), int(entries.size()));
    }
    snapshot.insert("entries", entries);
    snapshot.insert("schema_version", "buildscope.snapshot/v4");
    snapshot.insert("analysis_run", control.metadata(mode));
}

void annotateSnapshot(QJsonObject &snapshot, const QString &projectRoot, const QString &mode,
                      int maxUnits, int budgetSeconds, const QStringList &unitGlobs) {
    if (budgetSeconds < 1 || budgetSeconds > kMaxAnalysisBudgetSeconds)
        throw IncludeAnalysisError(
            QStringLiteral("analysis time budget must be 1..600 seconds"));
    AnalysisLimits limits;
    limits.maxUnits = maxUnits;
    limits.totalMilliseconds = budgetSeconds * 1000;
    AnalysisControl control(limits);
    annotateSnapshotControlled(snapshot, projectRoot, mode, control, unitGlobs);
    // Legacy v3 has no composite evidence model. Its export remains strict and
    // keeps the estimate when replay was unavailable, with an explicit warning.
    auto entries = snapshot.value("entries").toArray();
    for (qsizetype i = 0; i < entries.size(); ++i) {
        auto entry = entries[i].toObject();
        auto analysis = entry.value("include_analysis").toObject();
        if (analysis.value("evidence") == "unavailable" &&
            analysis.value("fallback").isObject()) {
            auto diagnostics = analysis.value("diagnostics").toArray();
            analysis = analysis.value("fallback").toObject();
            auto combined = analysis.value("diagnostics").toArray();
            for (auto d : diagnostics)
                combined.append(d);
            analysis.insert("diagnostics", combined);
        }
        if (!analysis.value("fallback").isNull())
            appendDiagnostic(analysis, "legacy-fallback-omitted",
                             "v3 export cannot carry a separate estimate; use v4 to retain "
                             "both evidence sets.");
        analysis.remove("complete");
        analysis.remove("stop_reason");
        analysis.remove("fallback");
        entry.insert("include_analysis", analysis);
        entries[i] = entry;
    }
    snapshot.insert("entries", entries);
    snapshot.remove("analysis_run");
    snapshot.insert("schema_version", "buildscope.snapshot/v3");
}

} // namespace buildscope::native
