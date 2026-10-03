#include "native_normalize.hpp"

#include "canonical_json.hpp"
#include "native_command.hpp"
#include "native_error.hpp"
#include "native_metadata.hpp"
#include "native_paths.hpp"

#include <QJsonArray>
#include <QSet>

#include <optional>

namespace buildscope::native {
namespace {

struct NormalizedPaths {
    QString absoluteDirectory;
    QJsonObject directory;
    QJsonObject source;
};

NormalizedPaths normalizedPaths(const QJsonObject &entry, const QString &style,
                                const QString &projectRoot, const QString &databaseParent) {
    const QString base = normalizeLexical(databaseParent, databaseParent, style);
    const QString directoryValue = entry.value(QStringLiteral("directory")).toString();
    const QString absoluteDirectory = normalizeLexical(directoryValue, base, style);
    NormalizedPaths paths;
    paths.absoluteDirectory = absoluteDirectory;
    paths.directory = pathRecord(directoryValue, base, projectRoot, style,
                                 QStringLiteral("directory"));
    paths.source = pathRecord(entry.value(QStringLiteral("file")).toString(),
                              absoluteDirectory, projectRoot, style, QStringLiteral("file"));
    return paths;
}

QJsonArray includePaths(const QJsonArray &rawIncludes, const QString &base,
                        const QString &projectRoot, const QString &style,
                        QJsonArray &diagnostics) {
    QJsonArray includes;
    int order = 0;
    for (const QJsonValue &rawValue : rawIncludes) {
        const QJsonObject rawInclude = rawValue.toObject();
        QJsonObject record = pathRecord(rawInclude.value(QStringLiteral("value")).toString(),
                                      base, projectRoot, style, QStringLiteral("directory"));
        record.insert(QStringLiteral("kind"), rawInclude.value(QStringLiteral("kind")));
        record.insert(QStringLiteral("order"), order++);
        const QJsonValue exists = record.value(QStringLiteral("exists"));
        if (exists.isBool() && !exists.toBool()) {
            diagnostics.append(diagnostic(QStringLiteral("missing-include"),
                                          QStringLiteral("Compiler include directory is "
                                                         "missing.")));
        }
        includes.append(record);
    }
    return includes;
}

QString sourceStatus(const QJsonObject &entry, const QString &absoluteDirectory,
                     const QJsonObject &source, const QJsonObject &output,
                     const QString &outputValue, const QString &style) {
    const QJsonValue exists = source.value(QStringLiteral("exists"));
    if (exists.isBool() && !exists.toBool()) {
        return QStringLiteral("missing");
    }
    if (exists.isNull()) {
        return QStringLiteral("unknown");
    }
    if (output.isEmpty()) {
        return QStringLiteral("present");
    }
    const QJsonValue outputExists = output.value(QStringLiteral("exists"));
    if (outputExists.isBool() && !outputExists.toBool()) {
        return QStringLiteral("stale");
    }
    const auto sourceMtime =
        nativeMtime(entry.value(QStringLiteral("file")).toString(), absoluteDirectory, style);
    const auto outputMtime = nativeMtime(outputValue, absoluteDirectory, style);
    if (sourceMtime.has_value() && outputMtime.has_value() &&
        sourceMtime.value() > outputMtime.value()) {
        return QStringLiteral("stale");
    }
    return QStringLiteral("present");
}

QJsonObject outputRecord(const QString &value, const QString &base, const QString &projectRoot,
                         const QString &style) {
    if (value.isEmpty()) {
        return {};
    }
    return pathRecord(value, base, projectRoot, style, QStringLiteral("path"));
}

struct OutputContext {
    std::optional<QString> declaredOutput;
    QString outputValue;
    QJsonObject record;
};

OutputContext outputContext(const QJsonObject &entry, const QStringList &argv,
                            const QString &base, const QString &projectRoot,
                            const QString &style, QJsonArray &diagnostics) {
    const QJsonValue declared = entry.value(QStringLiteral("output"));
    const QString declaredOutput = declared.isString() ? declared.toString() : QString();
    const QString argvOutput = outputFromArgv(argv);
    const QJsonObject declaredRecord = outputRecord(declaredOutput, base, projectRoot, style);
    const QJsonObject argvRecord = outputRecord(argvOutput, base, projectRoot, style);
    const QString declaredPath =
        declaredRecord.value(QStringLiteral("path")).toString();
    const QString argvPath = argvRecord.value(QStringLiteral("path")).toString();
    const bool pathsDiffer = style == QLatin1String("windows")
                                 ? declaredPath.compare(argvPath, Qt::CaseInsensitive) != 0
                                 : declaredPath != argvPath;
    if (!declaredRecord.isEmpty() && !argvRecord.isEmpty() && pathsDiffer) {
        diagnostics.append(diagnostic(QStringLiteral("output-mismatch"),
                                      QStringLiteral("Declared and compiler output paths "
                                                     "differ.")));
    }
    OutputContext context;
    if (declared.isString()) {
        context.declaredOutput = declared.toString();
    }
    context.outputValue = !declaredOutput.isEmpty() ? declaredOutput : argvOutput;
    context.record = !declaredRecord.isEmpty() ? declaredRecord : argvRecord;
    return context;
}

void appendStatusDiagnostics(QJsonArray &diagnostics, const QJsonObject &directory,
                             const QString &status) {
    const QJsonValue exists = directory.value(QStringLiteral("exists"));
    if (exists.isBool() && !exists.toBool()) {
        diagnostics.append(diagnostic(QStringLiteral("missing-directory"),
                                      QStringLiteral("Compilation directory is missing.")));
    }
    if (status == QLatin1String("missing")) {
        diagnostics.append(diagnostic(QStringLiteral("missing-source"),
                                      QStringLiteral("Compilation source is missing.")));
    } else if (status == QLatin1String("stale")) {
        diagnostics.append(diagnostic(QStringLiteral("stale-output"),
                                      QStringLiteral("Compilation output is missing or "
                                                     "stale.")));
    }
}

}  // namespace

QString entrySourceKey(const QJsonObject &entry) {
    const QJsonObject normalized = entry.value(QStringLiteral("normalized")).toObject();
    const QString style = normalized.value(QStringLiteral("command_style")).toString();
    QString key = normalized.value(QStringLiteral("source")).toObject()
                      .value(QStringLiteral("path"))
                      .toString();
    if (style == QLatin1String("windows")) {
        key = key.toLower();
    }
    return style + QLatin1Char('\0') + key;
}

QJsonObject normalizeEntry(const QJsonObject &entry, qsizetype index,
                           const QString &projectRoot, const QString &databaseParent) {
    Invocation invocation;
    try {
        invocation = parseInvocation(entry, index);
    } catch (const CommandError &error) {
        throw NormalizationError(QString::fromStdString(error.what()));
    }
    const QStringList &argv = invocation.argv;
    const QString &style = invocation.style;
    const NormalizedPaths paths = normalizedPaths(entry, style, projectRoot, databaseParent);
    const QString sourcePath = paths.source.value(QStringLiteral("path")).toString();
    ExtractedMetadata metadata = extractMetadata(argv, sourcePath);
    QJsonArray diagnostics = metadata.diagnostics;
    const QJsonArray includes = includePaths(metadata.includePaths, paths.absoluteDirectory,
                                           projectRoot, style, diagnostics);
    OutputContext output = outputContext(entry, argv, paths.absoluteDirectory, projectRoot,
                                         style, diagnostics);
    QJsonObject sysroot;
    if (!metadata.sysroot.isEmpty()) {
        sysroot = pathRecord(metadata.sysroot, paths.absoluteDirectory, projectRoot, style,
                             QStringLiteral("directory"));
    }
    const QString status = sourceStatus(entry, paths.absoluteDirectory, paths.source,
                                        output.record, output.outputValue, style);
    appendStatusDiagnostics(diagnostics, paths.directory, status);
    const QString configuration = canonicalDigest(QJsonObject{
        {QStringLiteral("argv"), QJsonArray::fromStringList(argv)},
        {QStringLiteral("directory"), paths.directory.value(QStringLiteral("path"))},
        {QStringLiteral("output"), output.record.value(QStringLiteral("path")).toString()},
    });
    QJsonObject normalized{
        {QStringLiteral("argv"), QJsonArray::fromStringList(argv)},
        {QStringLiteral("command_style"), style},
        {QStringLiteral("compiler"), compilerRecord(argv)},
        {QStringLiteral("configuration"), configuration},
        {QStringLiteral("defines"), metadata.defines},
        {QStringLiteral("directory"), paths.directory},
        {QStringLiteral("include_paths"), includes},
        {QStringLiteral("invocation_source"),
         invocation.arguments.has_value() ? QStringLiteral("arguments")
                                          : QStringLiteral("command")},
        {QStringLiteral("language"), metadata.language},
        {QStringLiteral("output"),
         output.record.isEmpty() ? QJsonValue(QJsonValue::Null)
                                 : QJsonValue(output.record)},
        {QStringLiteral("source"), paths.source},
        {QStringLiteral("standard"), metadata.standard},
        {QStringLiteral("sysroot"),
         sysroot.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(sysroot)},
        {QStringLiteral("target"),
         QJsonObject{
             {QStringLiteral("build_target"),
              cmakeTarget(output.record.value(QStringLiteral("path")).toString())},
             {QStringLiteral("triple"), metadata.targetTriple},
         }},
    };
    return QJsonObject{
        {QStringLiteral("arguments"),
         invocation.arguments.has_value()
             ? QJsonValue(QJsonArray::fromStringList(invocation.arguments.value()))
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("command"),
         invocation.command.has_value() ? QJsonValue(invocation.command.value())
                                        : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("diagnostics"), diagnostics},
        {QStringLiteral("directory"), entry.value(QStringLiteral("directory"))},
        {QStringLiteral("file"), entry.value(QStringLiteral("file"))},
        {QStringLiteral("normalized"), normalized},
        {QStringLiteral("output"),
         output.declaredOutput.has_value() ? QJsonValue(output.declaredOutput.value())
                                           : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("state"),
         QJsonObject{
             {QStringLiteral("duplicate"), false},
             {QStringLiteral("entry_index"), static_cast<double>(index)},
             {QStringLiteral("source_configuration_count"), 1},
             {QStringLiteral("source_status"), status},
         }},
    };
}

void annotateEntrySets(QJsonArray &entries) {
    QMap<QPair<QString, QString>, int> duplicateCounts;
    QMap<QString, QSet<QString>> sourceConfigurations;
    QList<QPair<QString, QString>> keys;
    keys.reserve(entries.size());
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const QString key = entrySourceKey(entry);
        const QString configuration = entry.value(QStringLiteral("normalized"))
                                          .toObject()
                                          .value(QStringLiteral("configuration"))
                                          .toString();
        keys.append({key, configuration});
        duplicateCounts[{key, configuration}] += 1;
        sourceConfigurations[key].insert(configuration);
    }
    for (qsizetype index = 0; index < entries.size(); ++index) {
        QJsonObject entry = entries.at(index).toObject();
        QJsonObject state = entry.value(QStringLiteral("state")).toObject();
        const QString key = keys.at(index).first;
        const QString configuration = keys.at(index).second;
        const bool duplicate = duplicateCounts.value({key, configuration}) > 1;
        state.insert(QStringLiteral("duplicate"), duplicate);
        state.insert(QStringLiteral("source_configuration_count"),
                     static_cast<double>(sourceConfigurations.value(key).size()));
        entry.insert(QStringLiteral("state"), state);
        if (duplicate) {
            QJsonArray diagnostics = entry.value(QStringLiteral("diagnostics")).toArray();
            diagnostics.append(diagnostic(
                QStringLiteral("duplicate-entry"),
                QStringLiteral("Compilation entry duplicates another configuration.")));
            entry.insert(QStringLiteral("diagnostics"), diagnostics);
        }
        entries.replace(index, entry);
    }
}

}  // namespace buildscope::native
