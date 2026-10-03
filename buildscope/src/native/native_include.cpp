#include "native_include.hpp"

#include "native_error.hpp"
#include "native_replay.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace buildscope::native {
namespace {

const QRegularExpression kTraceLine(QStringLiteral("^(\\.+) (.+)$"));
const QRegularExpression kIncludeLine(
    QStringLiteral("^\\s*#\\s*include\\s*([<\"])([^>\"\\r\\n]+)[>\"]"));
const QRegularExpression kGccMissingLine(
    QStringLiteral("^(.+?):([0-9]+)(?::[0-9]+)?: (?:fatal )?error: "
                   "([^:\\r\\n]+): No such file or directory$"));
const QRegularExpression kClangMissingLine(
    QStringLiteral("^(.+?):([0-9]+)(?::[0-9]+)?: (?:fatal )?error: "
                   "['<](.+?)[>'] file not found$"));
const QSet<QString> kVendorParts = {
    QStringLiteral("_deps"),       QStringLiteral("deps"),
    QStringLiteral("external"),    QStringLiteral("externals"),
    QStringLiteral("third-party"), QStringLiteral("third_party"),
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
        if (kVendorParts.contains(
                QString::fromStdString(part.string()).toLower())) {
            return QStringLiteral("vendor");
        }
    }
    return QStringLiteral("project");
}

struct FileState {
    dev_t device = 0;
    ino_t inode = 0;
    off_t size = 0;
    timespec mtime {};
    timespec ctime {};
};

FileState fileState(const struct stat &metadata) {
    return {metadata.st_dev, metadata.st_ino, metadata.st_size,
            metadata.st_mtim, metadata.st_ctim};
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
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return found;
    }
    struct stat before {};
    if (::fstat(descriptor, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_size > kMaxSourceBytes) {
        ::close(descriptor);
        return found;
    }
    QByteArray payload;
    payload.resize(before.st_size + 1);
    qint64 total = 0;
    while (total <= kMaxSourceBytes) {
        const ssize_t count = ::read(descriptor, payload.data() + total,
                                     kMaxSourceBytes + 1 - total);
        if (count <= 0) {
            break;
        }
        total += count;
    }
    struct stat after {};
    ::fstat(descriptor, &after);
    ::close(descriptor);
    struct stat namedAfter {};
    if (::stat(path.c_str(), &namedAfter) != 0 || total > kMaxSourceBytes ||
        !sameState(fileState(before), fileState(after)) ||
        before.st_dev != namedAfter.st_dev || before.st_ino != namedAfter.st_ino) {
        return {};
    }
    payload.resize(static_cast<int>(total));
    const QString text = QString::fromUtf8(payload);
    int lineNumber = 0;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        ++lineNumber;
        const auto match = kIncludeLine.match(line);
        if (match.hasMatch()) {
            found.append({lineNumber, match.captured(2),
                          match.captured(1) == QLatin1String("\"")
                              ? QStringLiteral("quote")
                              : QStringLiteral("angle")});
        }
    }
    return found;
}

using IncludeRoots = QList<QPair<QString, std::filesystem::path>>;

IncludeRoots includeRoots(const QJsonObject &entry,
                          const std::filesystem::path &projectRoot,
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
        if (record.value(QStringLiteral("scope")).toString() ==
            QLatin1String("project")) {
            root = projectRoot / raw;
        } else {
            root = raw.is_absolute() ? raw : cwd / raw;
        }
        roots.append({record.value(QStringLiteral("kind")).toString(),
                      resolveSoft(root)});
    }
    return roots;
}

IncludeRoots orderedSearchRoots(const std::filesystem::path &parent,
                                const QString &delimiter, const IncludeRoots &roots) {
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

MatchedDirective matchDirective(
    const std::filesystem::path &parent, const std::filesystem::path &child,
    const IncludeRoots &roots, QMap<std::filesystem::path, int> &cursors,
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
    for (int offset = 0; offset < found.size(); ++offset) {
        const int index = (start + offset) % found.size();
        const Directive &directive = found.at(index);
        const SearchResult searches =
            searchRecords(parent, directive.requested, directive.delimiter, roots, child,
                          projectRoot);
        bool selected = false;
        for (const QJsonValue &record : searches.records) {
            if (record.toObject().value(QStringLiteral("selected")).toBool()) {
                selected = true;
                break;
            }
        }
        if (selected || posixOf(child).endsWith(directive.requested)) {
            cursors.insert(parent, index + 1);
            return {directive.line, directive.requested, directive.delimiter};
        }
    }
    return {0, QString::fromStdString(child.filename().string()),
            QStringLiteral("unknown")};
}

QJsonObject edgeRecord(const std::filesystem::path &parent,
                       const std::optional<std::filesystem::path> &child, int line,
                       const QString &requested, const QString &delimiter,
                       const IncludeRoots &roots,
                       const std::filesystem::path &projectRoot,
                       const std::filesystem::path &buildRoot, const QString &evidence,
                       const QString &locationEvidence = QString()) {
    const SearchResult searches =
        searchRecords(parent, requested, delimiter, roots, child, projectRoot);
    return QJsonObject{
        {QStringLiteral("alternatives"),
         QJsonArray::fromStringList(searches.alternatives)},
        {QStringLiteral("classification"), classification(child, projectRoot, buildRoot)},
        {QStringLiteral("delimiter"), delimiter},
        {QStringLiteral("evidence"), evidence},
        {QStringLiteral("line"), line},
        {QStringLiteral("location_evidence"),
         locationEvidence.isEmpty()
             ? (line > 0 ? QStringLiteral("source-scan")
                         : QStringLiteral("unavailable"))
             : locationEvidence},
        {QStringLiteral("parent"), displayPath(parent, projectRoot)},
        {QStringLiteral("requested"), requested},
        {QStringLiteral("resolved"),
         child.has_value() ? QJsonValue(displayPath(child.value(), projectRoot))
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
    const std::filesystem::path child = std::filesystem::canonical(
        lexical.is_absolute() ? lexical : cwd / lexical, error);
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
            throw IncludeAnalysisError(
                QStringLiteral("compiler include trace exceeds the edge limit"));
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
        const TraceEdgeResult edge = traceEdge(match, stack, cwd);
        if (edge.hasEdge) {
            result.edges.append({edge.parent, edge.child});
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
                                  projectRoot, buildRoot,
                                  QStringLiteral("compiler-measured"),
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
    };
}

QJsonObject budgetResult(int index, int maxUnits, qint64 elapsed,
                         int budgetSeconds) {
    if (index >= maxUnits) {
        return unavailable(QStringLiteral("include-analysis-unit-limit"),
                           QStringLiteral("Include analysis stopped at the configured %1 "
                                          "unit limit.")
                               .arg(maxUnits));
    }
    if (elapsed >= static_cast<qint64>(budgetSeconds) * 1000) {
        return unavailable(QStringLiteral("include-analysis-time-budget"),
                           QStringLiteral("Include analysis stopped at the configured %1 "
                                          "second budget.")
                               .arg(budgetSeconds));
    }
    return {};
}

}  // namespace

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
    while (!pending.isEmpty()) {
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
                selected = resolveSoft(candidate.is_absolute()
                                           ? candidate
                                           : projectRoot / candidate);
                break;
            }
            QJsonObject record =
                edgeRecord(parent, selected, directive.line, directive.requested,
                           directive.delimiter, roots, projectRoot, cwd,
                           QStringLiteral("estimated"));
            if (!selected.has_value()) {
                record.insert(QStringLiteral("classification"),
                              QStringLiteral("unresolved"));
            }
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
    return QJsonObject{
        {QStringLiteral("command"), QJsonArray()},
        {QStringLiteral("diagnostics"), QJsonArray()},
        {QStringLiteral("duration_ms"), 0},
        {QStringLiteral("edges"), edges},
        {QStringLiteral("evidence"), QStringLiteral("estimated")},
    };
}

QJsonObject analyzeEntry(const QJsonObject &entry, const QString &projectRootValue) {
    const std::filesystem::path projectRoot =
        std::filesystem::canonical(projectRootValue.toStdString());
    const auto [command, cwdValue, sourceValue] = buildTraceCommand(entry, projectRootValue);
    const std::filesystem::path cwd = cwdValue.toStdString();
    const std::filesystem::path source = sourceValue.toStdString();
    const IncludeRoots roots = includeRoots(entry, projectRoot, cwd);
    const auto [returncode, stderrText, durationMs] = runTrace(command, cwdValue);
    const TraceEdges traced = traceEdges(stderrText, source, cwd);
    QMap<std::filesystem::path, int> cursors;
    QMap<std::filesystem::path, QList<Directive>> directiveCache;
    QJsonArray edges;
    for (const auto &[parent, child] : traced.edges) {
        const MatchedDirective matched =
            matchDirective(parent, child, roots, cursors, projectRoot, directiveCache);
        edges.append(edgeRecord(parent, child, matched.line, matched.requested,
                                matched.delimiter, roots, projectRoot, cwd,
                                QStringLiteral("compiler-measured")));
    }
    for (const QJsonValue &edge :
         missingEdges(stderrText, roots, projectRoot, cwd, cwd)) {
        edges.append(edge);
    }
    QJsonArray diagnostics;
    if (returncode != 0) {
        diagnostics.append(QJsonObject{
            {QStringLiteral("code"), QStringLiteral("compiler-trace-failed")},
            {QStringLiteral("message"),
             QStringLiteral("Compiler include trace exited with status %1.").arg(returncode)},
            {QStringLiteral("severity"), QStringLiteral("warning")},
        });
    } else if (!traced.unexpected.isEmpty()) {
        diagnostics.append(QJsonObject{
            {QStringLiteral("code"), QStringLiteral("compiler-trace-diagnostics")},
            {QStringLiteral("message"),
             QStringLiteral("Compiler emitted non-trace diagnostics; measured edges were "
                            "retained.")},
            {QStringLiteral("severity"), QStringLiteral("warning")},
        });
    }
    return QJsonObject{
        {QStringLiteral("command"), QJsonArray::fromStringList(command)},
        {QStringLiteral("diagnostics"), diagnostics},
        {QStringLiteral("duration_ms"), durationMs},
        {QStringLiteral("edges"), edges},
        {QStringLiteral("evidence"), QStringLiteral("compiler-measured")},
    };
}

void annotateSnapshot(QJsonObject &snapshot, const QString &projectRoot, const QString &mode,
                      int maxUnits, int budgetSeconds) {
    if (maxUnits < 1 || maxUnits > kMaxAnalysisUnits) {
        throw IncludeAnalysisError(
            QStringLiteral("include analysis unit limit must be between 1 and %1")
                .arg(kMaxAnalysisUnits));
    }
    if (budgetSeconds < 1 || budgetSeconds > kMaxAnalysisBudgetSeconds) {
        throw IncludeAnalysisError(
            QStringLiteral("include analysis time budget must be between 1 and %1 seconds")
                .arg(kMaxAnalysisBudgetSeconds));
    }
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    QJsonArray entries = snapshot.value(QStringLiteral("entries")).toArray();
    for (qsizetype index = 0; index < entries.size(); ++index) {
        const QJsonObject limited =
            budgetResult(static_cast<int>(index), maxUnits,
                         QDateTime::currentMSecsSinceEpoch() - started, budgetSeconds);
        QJsonObject entry = entries.at(index).toObject();
        QJsonObject analysis;
        if (!limited.isEmpty()) {
            analysis = limited;
        } else {
            try {
                if (mode == QLatin1String("compiler")) {
                    analysis = analyzeEntry(entry, projectRoot);
                } else if (mode == QLatin1String("estimate")) {
                    analysis = estimateEntry(entry, projectRoot);
                } else {
                    throw IncludeAnalysisError(
                        QStringLiteral("unsupported include analysis mode: %1").arg(mode));
                }
            } catch (const IncludeAnalysisError &error) {
                analysis = unavailable(QStringLiteral("include-analysis-unavailable"),
                                       QString::fromStdString(error.what()));
            }
        }
        entry.insert(QStringLiteral("include_analysis"), analysis);
        entries.replace(index, entry);
    }
    snapshot.insert(QStringLiteral("entries"), entries);
    snapshot.insert(QStringLiteral("schema_version"),
                    QStringLiteral("buildscope.snapshot/v3"));
}

}  // namespace buildscope::native
