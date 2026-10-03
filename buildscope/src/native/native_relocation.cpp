#include "native_relocation.hpp"
#include "native_command.hpp"
#include "native_error.hpp"
#include "native_normalize.hpp"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <algorithm>

namespace buildscope::native {
QList<RootMapping> parseRootMappings(const QStringList &specifications) {
    if (specifications.size() > 32)
        throw SnapshotError(QStringLiteral("at most 32 relocation roots are supported"));
    QList<RootMapping> result;
    QSet<QString> seen;
    for (const auto &spec : specifications) {
        const auto split = spec.indexOf('=');
        if (split <= 0 || split == spec.size() - 1)
            throw SnapshotError(QStringLiteral("root mapping must be OLD=NEW"));
        const auto from = QDir::cleanPath(spec.left(split)),
                   to = QDir::cleanPath(spec.mid(split + 1));
        if (!QFileInfo(from).isAbsolute() || !QFileInfo(to).isAbsolute() ||
            from.contains(QChar::Null) || to.contains(QChar::Null) || seen.contains(from))
            throw SnapshotError(QStringLiteral("mapping roots must be unique absolute paths"));
        seen.insert(from);
        result.append({from, to});
    }
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.from.size() > b.from.size(); });
    return result;
}
QString relocatePath(const QString &path, const QList<RootMapping> &mappings) {
    if (!QFileInfo(path).isAbsolute())
        return path;
    const auto clean = QDir::cleanPath(path);
    for (const auto &mapping : mappings) {
        if (clean == mapping.from)
            return mapping.to;
        const auto prefix = mapping.from.endsWith('/') ? mapping.from : mapping.from + '/';
        if (clean.startsWith(prefix))
            return QDir(mapping.to).filePath(clean.mid(prefix.size()));
    }
    return path;
}
QJsonObject relocateInvocation(const QJsonObject &raw, const QList<RootMapping> &mappings) {
    if (mappings.isEmpty())
        return raw;
    auto result = raw;
    const auto invocation = parseInvocation(raw, 0);
    auto argv = invocation.argv;
    const QSet<QString> pathOptions = {
        "-I",        "-F",       "-isystem", "-iquote",       "-idirafter", "-isysroot",
        "--sysroot", "-include", "-imacros", "-resource-dir", "-o",         "--output"};
    const QStringList joined = {
        "--sysroot=", "--output=", "-resource-dir=", "-isystem", "-iquote", "-idirafter",
        "-isysroot",  "-include",  "-imacros",       "-I",       "-F",      "-o"};
    bool valuePath = false;
    for (auto &arg : argv) {
        if (valuePath) {
            arg = relocatePath(arg, mappings);
            valuePath = false;
            continue;
        }
        if (pathOptions.contains(arg)) {
            valuePath = true;
            continue;
        }
        if (arg.startsWith('-')) {
            for (const auto &prefix : joined)
                if (arg.startsWith(prefix) && arg.size() > prefix.size()) {
                    arg = prefix + relocatePath(arg.mid(prefix.size()), mappings);
                    break;
                }
        } else
            arg = relocatePath(arg, mappings);
    }
    result.insert("arguments", QJsonArray::fromStringList(argv));
    result.insert("command", QJsonValue::Null);
    for (auto key : {"file", "directory", "output"})
        if (result.value(key).isString())
            result.insert(key, relocatePath(result.value(key).toString(), mappings));
    return result;
}
QJsonObject relocateSnapshot(const QJsonObject &snapshot, const QString &projectRoot,
                             const QList<RootMapping> &mappings) {
    auto result = snapshot;
    QJsonArray entries;
    const auto source = snapshot.value("source").toObject();
    const auto parent =
        relocatePath(QFileInfo(source.value("path").toString()).absolutePath(), mappings);
    int index = 0;
    for (const auto &value : snapshot.value("entries").toArray()) {
        auto raw = relocateInvocation(value.toObject(), mappings);
        auto entry = normalizeEntry(raw, index++, projectRoot, parent);
        auto diagnostics = entry.value("diagnostics").toArray();
        for (const auto &mapping : mappings)
            diagnostics.append(QJsonObject{{"code", "explicit-root-relocation"},
                                           {"severity", "info"},
                                           {"message", mapping.from + " → " + mapping.to}});
        entry.insert("diagnostics", diagnostics);
        entries.append(entry);
    }
    annotateEntrySets(entries);
    result.insert("entries", entries);
    result.insert("schema_version", "buildscope.snapshot/v2");
    result.remove("analysis_run");
    auto changed = source;
    changed.insert("project_root", projectRoot);
    result.insert("source", changed);
    return result;
}
} // namespace buildscope::native
