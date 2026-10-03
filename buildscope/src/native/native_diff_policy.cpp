#include "native_diff_policy.hpp"

#include "canonical_json.hpp"
#include "native_error.hpp"
#include "native_glob.hpp"
#include "native_paths.hpp"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace buildscope::native {
namespace {

const QString kPolicyVersion = QStringLiteral("buildscope.diff-policy/v1");
constexpr int kMaxSuppressions = 256;
constexpr qsizetype kMaxSuppressionChars = 1024;
const QSet<QString> kChangeCategories = {
    QStringLiteral("added"),    QStringLiteral("compiler"),  QStringLiteral("define"),
    QStringLiteral("flag"),     QStringLiteral("include"),   QStringLiteral("language"),
    QStringLiteral("launcher"), QStringLiteral("moved"),     QStringLiteral("removed"),
    QStringLiteral("standard"), QStringLiteral("sysroot"),   QStringLiteral("target"),
};
const QRegularExpression kAssignment(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*=.*$"));
const QSet<QString> kNoValueOptions = {
    QStringLiteral("-c"),           QStringLiteral("-MD"), QStringLiteral("-MMD"),
    QStringLiteral("-MP"),          QStringLiteral("-MG"), QStringLiteral("/c"),
    QStringLiteral("/showIncludes"),
};
const QSet<QString> kValueOptions = {
    QStringLiteral("-MF"), QStringLiteral("-MT"), QStringLiteral("-MQ"),
    QStringLiteral("/sourceDependencies"),
};
const QMap<QString, QString> kPathValueOptions = {
    {QStringLiteral("--gcc-toolchain"), QStringLiteral("--gcc-toolchain")},
    {QStringLiteral("-B"), QStringLiteral("-B")},
    {QStringLiteral("-include"), QStringLiteral("-include")},
    {QStringLiteral("-include-pch"), QStringLiteral("-include-pch")},
    {QStringLiteral("-imacros"), QStringLiteral("-imacros")},
    {QStringLiteral("-resource-dir"), QStringLiteral("-resource-dir")},
    {QStringLiteral("/FI"), QStringLiteral("/FI")},
};
const QMap<QString, QString> kPathEqualsOptions = {
    {QStringLiteral("--gcc-toolchain="), QStringLiteral("--gcc-toolchain")},
    {QStringLiteral("-fsanitize-blacklist="), QStringLiteral("-fsanitize-blacklist")},
    {QStringLiteral("-fsanitize-ignorelist="), QStringLiteral("-fsanitize-ignorelist")},
    {QStringLiteral("-resource-dir="), QStringLiteral("-resource-dir")},
};
const QSet<QString> kModeledSeparated = {
    QStringLiteral("-D"),          QStringLiteral("-U"),    QStringLiteral("-F"),
    QStringLiteral("-I"),          QStringLiteral("-idirafter"), QStringLiteral("-iframework"),
    QStringLiteral("-iquote"),     QStringLiteral("-isystem"),   QStringLiteral("-isysroot"),
    QStringLiteral("-std"),        QStringLiteral("-target"),    QStringLiteral("--sysroot"),
    QStringLiteral("--target"),    QStringLiteral("-x"),         QStringLiteral("/D"),
    QStringLiteral("/I"),          QStringLiteral("/Tc"),        QStringLiteral("/Tp"),
    QStringLiteral("/U"),          QStringLiteral("/external:I"), QStringLiteral("/imsvc"),
};
const QStringList kModeledPrefixes = {
    QStringLiteral("/external:I"), QStringLiteral("--sysroot="), QStringLiteral("--target="),
    QStringLiteral("-idirafter"),  QStringLiteral("-iframework"), QStringLiteral("-iquote"),
    QStringLiteral("-isystem"),    QStringLiteral("-isysroot"),
    QStringLiteral("/clang:--target="), QStringLiteral("/clang:-target="),
    QStringLiteral("-target="),    QStringLiteral("-std="),       QStringLiteral("/std:"),
    QStringLiteral("/imsvc"),      QStringLiteral("-D"),          QStringLiteral("-U"),
    QStringLiteral("-F"),          QStringLiteral("-I"),          QStringLiteral("-x"),
    QStringLiteral("/D"),          QStringLiteral("/I"),          QStringLiteral("/Tc"),
    QStringLiteral("/Tp"),         QStringLiteral("/U"),
};

QString pathValue(const QJsonValue &record) {
    if (record.isNull() || record.isUndefined()) {
        return {};
    }
    const QJsonObject object = record.toObject();
    const QString path = object.value(QStringLiteral("path")).toString();
    return object.value(QStringLiteral("style")).toString() == QLatin1String("windows")
               ? path.toLower()
               : path;
}

int compilerIndex(const QStringList &argv, const QString &compilerPath) {
    for (int index = 0; index < argv.size(); ++index) {
        if (argv.at(index) == compilerPath) {
            return index;
        }
    }
    throw DiffPolicyError(QStringLiteral("normalized compiler path is not present in argv"));
}

QString contextPath(const QString &value, const QJsonObject &entry,
                    const QString &projectRoot, const QString &databaseParent) {
    const QString style = entry.value(QStringLiteral("normalized"))
                              .toObject()
                              .value(QStringLiteral("command_style"))
                              .toString();
    const QString directory = normalizeLexical(
        entry.value(QStringLiteral("directory")).toString(), databaseParent, style);
    const QString path = projectRelativeLexical(value, directory, projectRoot, style);
    return style == QLatin1String("windows") ? path.toLower() : path;
}

QString launcherToken(const QString &token, const QJsonObject &entry,
                      const QString &projectRoot, const QString &databaseParent) {
    if (token.startsWith(QLatin1Char('-')) || kAssignment.match(token).hasMatch()) {
        return token;
    }
    if (token.contains(QLatin1Char('/')) || token.contains(QLatin1Char('\\')) ||
        token.startsWith(QLatin1Char('.'))) {
        return contextPath(token, entry, projectRoot, databaseParent);
    }
    return entry.value(QStringLiteral("normalized"))
                   .toObject()
                   .value(QStringLiteral("command_style"))
                   .toString() == QLatin1String("windows")
               ? token.toLower()
               : token;
}

QSet<QString> sourceSpellings(const QJsonObject &entry, const QString &projectRoot,
                              const QString &databaseParent) {
    const QJsonObject normalized = entry.value(QStringLiteral("normalized")).toObject();
    QSet<QString> values = {
        normalized.value(QStringLiteral("source")).toObject()
            .value(QStringLiteral("path"))
            .toString(),
        contextPath(entry.value(QStringLiteral("file")).toString(), entry, projectRoot,
                    databaseParent),
    };
    if (normalized.value(QStringLiteral("command_style")).toString() ==
        QLatin1String("windows")) {
        const QSet<QString> original = values;
        for (const QString &value : original) {
            values.insert(value.toLower());
        }
    }
    return values;
}

bool isSourceOperand(const QString &token, const QJsonObject &entry,
                     const QString &projectRoot, const QString &databaseParent) {
    return sourceSpellings(entry, projectRoot, databaseParent)
        .contains(contextPath(token, entry, projectRoot, databaseParent));
}

std::optional<int> modeledOption(const QStringList &argv, int index) {
    const QString &token = argv.at(index);
    if (kModeledSeparated.contains(token)) {
        if (index + 1 >= argv.size()) {
            throw DiffPolicyError(
                QStringLiteral("modeled compiler option %1 has no value").arg(token));
        }
        return index + 2;
    }
    for (const QString &prefix : kModeledPrefixes) {
        if (token.startsWith(prefix) && token != prefix) {
            return index + 1;
        }
    }
    return std::nullopt;
}

std::optional<int> outputOptionIndex(const QStringList &argv, int index) {
    const QString &token = argv.at(index);
    if (token == QLatin1String("-o") || token == QLatin1String("/Fo") ||
        token == QLatin1String("/Fo:")) {
        if (index + 1 >= argv.size()) {
            throw DiffPolicyError(
                QStringLiteral("compiler output option %1 has no value").arg(token));
        }
        return index + 2;
    }
    if ((token.startsWith(QLatin1String("/Fo:")) || token.startsWith(QLatin1String("/Fo"))) &&
        token != QLatin1String("/Fo") && token != QLatin1String("/Fo:")) {
        return index + 1;
    }
    return std::nullopt;
}

struct ResidualOption {
    QStringList values;
    int next = 0;
};

std::optional<ResidualOption> pathResidualOption(const QStringList &argv, int index,
                                                 const QJsonObject &entry,
                                                 const QString &projectRoot,
                                                 const QString &databaseParent) {
    const QString &token = argv.at(index);
    const auto exact = kPathValueOptions.constFind(token);
    if (exact != kPathValueOptions.constEnd()) {
        if (index + 1 >= argv.size()) {
            throw DiffPolicyError(QStringLiteral(
                "path-bearing compiler option %1 has no value").arg(token));
        }
        return ResidualOption{
            {exact.value(), contextPath(argv.at(index + 1), entry, projectRoot,
                                        databaseParent)},
            index + 2,
        };
    }
    QStringList sorted = kPathEqualsOptions.keys();
    std::sort(sorted.begin(), sorted.end(),
              [](const QString &first, const QString &second) {
                  return first.size() > second.size();
              });
    for (const QString &prefix : sorted) {
        if (token.startsWith(prefix)) {
            const QString rawPath = token.mid(prefix.size());
            if (rawPath.isEmpty()) {
                throw DiffPolicyError(QStringLiteral(
                                          "path-bearing compiler option %1 has no value")
                                          .arg(kPathEqualsOptions.value(prefix)));
            }
            return ResidualOption{
                {kPathEqualsOptions.value(prefix),
                 contextPath(rawPath, entry, projectRoot, databaseParent)},
                index + 1,
            };
        }
    }
    for (const QString &prefix : {QStringLiteral("/FI"), QStringLiteral("-B")}) {
        if (token.startsWith(prefix) && token != prefix) {
            return ResidualOption{
                {kPathValueOptions.value(prefix),
                 contextPath(token.mid(prefix.size()), entry, projectRoot, databaseParent)},
                index + 1,
            };
        }
    }
    return std::nullopt;
}

std::optional<ResidualOption> nonsemanticOption(const QStringList &argv, int index,
                                              const QJsonObject &entry,
                                              const QString &projectRoot,
                                              const QString &databaseParent) {
    const QString &token = argv.at(index);
    if (kNoValueOptions.contains(token)) {
        return ResidualOption{{}, index + 1};
    }
    if (kValueOptions.contains(token)) {
        if (index + 1 >= argv.size()) {
            throw DiffPolicyError(
                QStringLiteral("compiler option %1 has no value").arg(token));
        }
        return ResidualOption{{}, index + 2};
    }
    const auto pathOption =
        pathResidualOption(argv, index, entry, projectRoot, databaseParent);
    if (pathOption.has_value()) {
        return pathOption;
    }
    const auto outputIndex = outputOptionIndex(argv, index);
    if (outputIndex.has_value()) {
        return ResidualOption{{}, outputIndex.value()};
    }
    const auto modeledIndex = modeledOption(argv, index);
    if (modeledIndex.has_value()) {
        return ResidualOption{{}, modeledIndex.value()};
    }
    return std::nullopt;
}

QStringList residualFlags(const QJsonObject &entry, int compilerIdx,
                          const QString &projectRoot, const QString &databaseParent) {
    const QStringList argv = entry.value(QStringLiteral("normalized"))
                                 .toObject()
                                 .value(QStringLiteral("argv"))
                                 .toVariant()
                                 .toStringList();
    QStringList residual;
    int index = compilerIdx + 1;
    bool terminated = false;
    while (index < argv.size()) {
        const QString &token = argv.at(index);
        if (token == QLatin1String("--")) {
            residual.append(token);
            terminated = true;
            ++index;
            continue;
        }
        if (isSourceOperand(token, entry, projectRoot, databaseParent)) {
            ++index;
            continue;
        }
        if (!terminated) {
            const auto option =
                nonsemanticOption(argv, index, entry, projectRoot, databaseParent);
            if (option.has_value()) {
                residual.append(option->values);
                index = option->next;
                continue;
            }
        }
        residual.append(token);
        ++index;
    }
    return residual;
}

QJsonArray definitions(const QJsonArray &values) {
    QJsonArray result;
    for (const QJsonValue &item : values) {
        const QJsonObject value = item.toObject();
        const QString action = value.value(QStringLiteral("action")).toString();
        const QJsonValue raw = value.value(QStringLiteral("value"));
        const QString semanticValue = action == QLatin1String("define") && raw.isNull()
                                          ? QStringLiteral("1")
                                          : raw.toString();
        result.append(QJsonObject{
            {QStringLiteral("action"), action},
            {QStringLiteral("name"), value.value(QStringLiteral("name"))},
            {QStringLiteral("value"), semanticValue},
        });
    }
    return result;
}

}  // namespace

const QStringList kIgnoredFields = {
    QStringLiteral("raw command spelling"),
    QStringLiteral("compilation directory"),
    QStringLiteral("output path and filename"),
    QStringLiteral("original entry index and duplicate annotation"),
    QStringLiteral("filesystem existence and stale status"),
    QStringLiteral("snapshot diagnostics and include-analysis observations"),
};

QString sourceKey(const QJsonObject &entry) {
    const QJsonObject normalized = entry.value(QStringLiteral("normalized")).toObject();
    const QString style = normalized.value(QStringLiteral("command_style")).toString();
    QString path = normalized.value(QStringLiteral("source"))
                       .toObject()
                       .value(QStringLiteral("path"))
                       .toString();
    if (style == QLatin1String("windows")) {
        path = path.toLower();
    }
    return style + QLatin1Char('\0') + path;
}

QString sourcePath(const QJsonObject &entry) {
    return entry.value(QStringLiteral("normalized"))
        .toObject()
        .value(QStringLiteral("source"))
        .toObject()
        .value(QStringLiteral("path"))
        .toString();
}

QJsonObject semanticConfiguration(const QJsonObject &entry, const QString &projectRoot,
                                  const QString &databaseParent) {
    const QJsonObject normalized = entry.value(QStringLiteral("normalized")).toObject();
    const QStringList argv =
        normalized.value(QStringLiteral("argv")).toVariant().toStringList();
    const QJsonObject compiler = normalized.value(QStringLiteral("compiler")).toObject();
    const int index = compilerIndex(argv, compiler.value(QStringLiteral("path")).toString());
    for (int token = index + 1; token < argv.size(); ++token) {
        if (argv.at(token).startsWith(QLatin1Char('@'))) {
            throw DiffPolicyError(QStringLiteral(
                "%1 uses an opaque response file and cannot be compared exactly")
                                      .arg(sourcePath(entry)));
        }
    }
    const bool windows =
        normalized.value(QStringLiteral("command_style")).toString() ==
        QLatin1String("windows");
    QString compilerName = compiler.value(QStringLiteral("name")).toString();
    QStringList wrappers =
        compiler.value(QStringLiteral("wrappers")).toVariant().toStringList();
    if (windows) {
        compilerName = compilerName.toLower();
        for (QString &wrapper : wrappers) {
            wrapper = wrapper.toLower();
        }
    }
    QJsonArray includes;
    for (const QJsonValue &item :
         normalized.value(QStringLiteral("include_paths")).toArray()) {
        const QJsonObject value = item.toObject();
        includes.append(QJsonObject{
            {QStringLiteral("kind"), value.value(QStringLiteral("kind"))},
            {QStringLiteral("path"), pathValue(value)},
        });
    }
    QStringList launcher;
    for (int token = 0; token < index; ++token) {
        launcher.append(
            launcherToken(argv.at(token), entry, projectRoot, databaseParent));
    }
    return QJsonObject{
        {QStringLiteral("compiler"),
         QJsonObject{
             {QStringLiteral("command_style"),
              normalized.value(QStringLiteral("command_style"))},
             {QStringLiteral("family"), compiler.value(QStringLiteral("family"))},
             {QStringLiteral("name"), compilerName},
             {QStringLiteral("path"),
              launcherToken(compiler.value(QStringLiteral("path")).toString(), entry,
                            projectRoot, databaseParent)},
             {QStringLiteral("wrappers"), QJsonArray::fromStringList(wrappers)},
         }},
        {QStringLiteral("defines"), definitions(normalized.value(QStringLiteral("defines"))
                                                    .toArray())},
        {QStringLiteral("flags"),
         QJsonArray::fromStringList(
             residualFlags(entry, index, projectRoot, databaseParent))},
        {QStringLiteral("include_paths"), includes},
        {QStringLiteral("language"), normalized.value(QStringLiteral("language"))},
        {QStringLiteral("launcher"), QJsonArray::fromStringList(launcher)},
        {QStringLiteral("standard"), normalized.value(QStringLiteral("standard"))},
        {QStringLiteral("sysroot"), pathValue(normalized.value(QStringLiteral("sysroot")))},
        {QStringLiteral("target"),
         QJsonObject{
             {QStringLiteral("build_target"),
              normalized.value(QStringLiteral("target"))
                  .toObject()
                  .value(QStringLiteral("build_target"))},
             {QStringLiteral("triple"),
              normalized.value(QStringLiteral("target"))
                  .toObject()
                  .value(QStringLiteral("triple"))},
         }},
    };
}

QJsonObject configurationView(const QJsonObject &entry, const QString &projectRoot,
                              const QString &databaseParent) {
    const QJsonObject semantic =
        semanticConfiguration(entry, projectRoot, databaseParent);
    return QJsonObject{
        {QStringLiteral("entry_index"),
         entry.value(QStringLiteral("state")).toObject().value(QStringLiteral("entry_index"))},
        {QStringLiteral("semantic"), semantic},
        {QStringLiteral("semantic_digest"), canonicalDigest(semantic)},
    };
}

QString roleKey(const QJsonObject &view) {
    const QJsonObject semantic = view.value(QStringLiteral("semantic")).toObject();
    const QJsonObject target = semantic.value(QStringLiteral("target")).toObject();
    return semantic.value(QStringLiteral("language")).toString() + QLatin1Char('\0') +
           target.value(QStringLiteral("build_target")).toString() + QLatin1Char('\0') +
           target.value(QStringLiteral("triple")).toString();
}

QJsonArray parseSuppressions(const QStringList &values) {
    if (values.size() > kMaxSuppressions) {
        throw DiffPolicyError(
            QStringLiteral("suppression count exceeds %1").arg(kMaxSuppressions));
    }
    QJsonArray rules;
    QSet<QString> seen;
    for (const QString &raw : values) {
        if (raw.isEmpty() || raw.contains(QLatin1Char('\0')) ||
            raw.size() > kMaxSuppressionChars) {
            throw DiffPolicyError(
                QStringLiteral("suppression must be a non-empty bounded string"));
        }
        const qsizetype separator = raw.indexOf(QLatin1Char(':'));
        const QString category = separator < 0 ? raw : raw.left(separator);
        if (category != QLatin1String("*") && !kChangeCategories.contains(category)) {
            throw DiffPolicyError(
                QStringLiteral("unknown suppression category: %1").arg(category));
        }
        const QString pattern = separator < 0 ? QStringLiteral("*") : raw.mid(separator + 1);
        if (pattern.isEmpty()) {
            throw DiffPolicyError(
                QStringLiteral("suppression path glob must not be empty"));
        }
        if (pattern.contains(QLatin1Char('\\')) || pattern.contains(QLatin1Char('[')) ||
            pattern.contains(QLatin1Char(']'))) {
            throw DiffPolicyError(QStringLiteral(
                "suppression path glob supports only /, literal characters, *, **, and ?"));
        }
        const QString identity = category + QLatin1Char('\0') + pattern;
        if (seen.contains(identity)) {
            throw DiffPolicyError(
                QStringLiteral("duplicate suppression rule: %1").arg(raw));
        }
        seen.insert(identity);
        rules.append(QJsonObject{
            {QStringLiteral("category"), category},
            {QStringLiteral("path"), pattern},
        });
    }
        QList<QJsonObject> sorted;
    for (const QJsonValue &value : rules) {
        sorted.append(value.toObject());
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const QJsonObject &first, const QJsonObject &second) {
                  const QByteArray firstKey =
                      first.value(QStringLiteral("category")).toString().toUtf8() +
                      QByteArray(1, '\0') +
                      first.value(QStringLiteral("path")).toString().toUtf8();
                  const QByteArray secondKey =
                      second.value(QStringLiteral("category")).toString().toUtf8() +
                      QByteArray(1, '\0') +
                      second.value(QStringLiteral("path")).toString().toUtf8();
                  return firstKey < secondKey;
              });
    QJsonArray result;
    for (const QJsonObject &rule : sorted) {
        result.append(rule);
    }
    return result;
}

QString matchingSuppression(const QJsonArray &rules, const QString &category,
                            const QString &before, const QString &after, bool windows) {
    for (const QJsonValue &value : rules) {
        const QJsonObject rule = value.toObject();
        const QString ruleCategory = rule.value(QStringLiteral("category")).toString();
        if (ruleCategory != QLatin1String("*") && ruleCategory != category) {
            continue;
        }
        const QString pattern = rule.value(QStringLiteral("path")).toString();
        for (const QString &rawPath : {before, after}) {
            if (!rawPath.isEmpty() && globMatches(rawPath, pattern, windows)) {
                return ruleCategory + QLatin1Char(':') + pattern;
            }
        }
    }
    return {};
}

QJsonObject policyRecord(const QJsonArray &rules) {
    return QJsonObject{
        {QStringLiteral("ignored_fields"), QJsonArray::fromStringList(kIgnoredFields)},
        {QStringLiteral("suppression_rules"), rules},
        {QStringLiteral("version"), kPolicyVersion},
    };
}

}  // namespace buildscope::native
