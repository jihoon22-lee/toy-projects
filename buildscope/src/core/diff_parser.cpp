#include "buildscope/diff.hpp"

#include "buildscope/contract.hpp"
#include "contract_loader.hpp"
#include "contract_parser.hpp"

#include <QCryptographicHash>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace buildscope {
namespace {

constexpr qsizetype kMaxDiffUnits = 200000;
constexpr qsizetype kMaxDiffChanges = 10;
constexpr qsizetype kMaxDiffDiagnostics = 100000;
constexpr qsizetype kMaxSuppressions = 256;
constexpr qsizetype kMaxIgnoredFields = 64;
constexpr qsizetype kMaxSemanticArray = 32768;
constexpr qsizetype kMaxFieldChars = 1024 * 1024;
constexpr qsizetype kMaxConfigurations = 100000;
constexpr qsizetype kMaxSummaryChanges = 1000000;

const QStringList kIgnoredFields = {
    QStringLiteral("raw command spelling"),
    QStringLiteral("compilation directory"),
    QStringLiteral("output path and filename"),
    QStringLiteral("original entry index and duplicate annotation"),
    QStringLiteral("filesystem existence and stale status"),
    QStringLiteral("snapshot diagnostics and include-analysis observations"),
};

const QStringList kChangeCategories = {
    QStringLiteral("added"),    QStringLiteral("compiler"), QStringLiteral("define"),
    QStringLiteral("flag"),     QStringLiteral("include"),  QStringLiteral("language"),
    QStringLiteral("launcher"), QStringLiteral("moved"),    QStringLiteral("removed"),
    QStringLiteral("standard"), QStringLiteral("sysroot"),  QStringLiteral("target"),
};

QString nullableString(const QJsonObject &object, const QString &key,
                       const QString &location, bool allowEmpty = false) {
    if (!object.contains(key)) {
        throw ContractError(location + "." + key + " is required");
    }
    const auto value = object.value(key);
    if (value.isNull()) {
        return {};
    }
    if (!value.isString() || (!allowEmpty && value.toString().isEmpty()) ||
        value.toString().contains(QChar::Null)) {
        throw ContractError(location + "." + key + " must be " +
                            (allowEmpty ? QStringLiteral("a string")
                                        : QStringLiteral("a non-empty string")) +
                            QStringLiteral(" or null"));
    }
    if (value.toString().size() > kMaxFieldChars) {
        throw ContractError(location + "." + key + " exceeds the character limit");
    }
    return value.toString();
}

QJsonObject requiredObject(const QJsonObject &object, const QString &key,
                           const QString &location) {
    const auto value = object.value(key);
    if (!value.isObject()) {
        throw ContractError(location + "." + key + " must be an object");
    }
    return value.toObject();
}

QJsonArray boundedArray(const QJsonObject &object, const QString &key,
                        const QString &location, qsizetype limit) {
    const auto value = object.value(key);
    if (!value.isArray()) {
        throw ContractError(location + "." + key + " must be an array");
    }
    const auto values = value.toArray();
    if (values.size() > limit) {
        throw ContractError(location + "." + key + " exceeds the item limit");
    }
    return values;
}

void validateStringArray(const QJsonObject &object, const QString &key,
                         const QString &location, bool allowEmptyItems = false) {
    const auto values = boundedArray(object, key, location, kMaxSemanticArray);
    for (qsizetype index = 0; index < values.size(); ++index) {
        const auto value = values.at(index);
        if (!value.isString() || (!allowEmptyItems && value.toString().isEmpty()) ||
            value.toString().contains(QChar::Null)) {
            throw ContractError(location + "." + key + "[" + QString::number(index) +
                                "] must be a" +
                                (allowEmptyItems ? QString() : QStringLiteral(" non-empty")) +
                                QStringLiteral(" string"));
        }
        if (value.toString().size() > kMaxFieldChars) {
            throw ContractError(location + "." + key + "[" + QString::number(index) +
                                "] exceeds the character limit");
        }
    }
}

qsizetype boundedInteger(const QJsonObject &object, const QString &key,
                         const QString &location, qsizetype maximum) {
    const auto value = detail::requiredInteger(object, key, location);
    if (value > maximum) {
        throw ContractError(location + "." + key + " exceeds " +
                            QString::number(maximum));
    }
    return value;
}

bool validDigest(const QString &value) {
    static const QRegularExpression pattern(
        QStringLiteral(R"(^sha256:[0-9a-f]{64}$)"));
    return pattern.match(value).hasMatch();
}

QString requiredDigest(const QJsonObject &object, const QString &key,
                       const QString &location) {
    const auto digest = detail::requiredString(object, key, location);
    if (!validDigest(digest)) {
        throw ContractError(location + "." + key +
                            " must be a lowercase sha256 digest");
    }
    return digest;
}

QString semanticDigest(const QJsonObject &semantic) {
    const auto payload = QJsonDocument(semantic).toJson(QJsonDocument::Compact);
    return QStringLiteral("sha256:") +
           QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256)
                                   .toHex());
}

void validateCompiler(const QJsonObject &semantic, const QString &location) {
    const auto compiler = requiredObject(semantic, QStringLiteral("compiler"), location);
    detail::rejectUnknownKeys(
        compiler,
        {QStringLiteral("command_style"), QStringLiteral("family"),
         QStringLiteral("name"), QStringLiteral("path"),
         QStringLiteral("wrappers")},
        location + ".compiler");
    detail::requiredEnumString(compiler, QStringLiteral("command_style"),
                               location + ".compiler",
                               {QStringLiteral("posix"), QStringLiteral("windows")});
    detail::requiredEnumString(
        compiler, QStringLiteral("family"), location + ".compiler",
        {QStringLiteral("clang"), QStringLiteral("clang-cl"),
         QStringLiteral("emscripten"), QStringLiteral("gcc"), QStringLiteral("msvc"),
         QStringLiteral("unknown")});
    detail::requiredString(compiler, QStringLiteral("name"), location + ".compiler");
    detail::requiredString(compiler, QStringLiteral("path"), location + ".compiler");
    validateStringArray(compiler, QStringLiteral("wrappers"), location + ".compiler");
}

void validateDefines(const QJsonObject &semantic, const QString &location) {
    const auto defines = boundedArray(semantic, QStringLiteral("defines"), location,
                                      kMaxSemanticArray);
    for (qsizetype index = 0; index < defines.size(); ++index) {
        const auto itemLocation =
            location + ".defines[" + QString::number(index) + "]";
        if (!defines.at(index).isObject()) {
            throw ContractError(itemLocation + " must be an object");
        }
        const auto define = defines.at(index).toObject();
        detail::rejectUnknownKeys(
            define,
            {QStringLiteral("action"), QStringLiteral("name"), QStringLiteral("value")},
            itemLocation);
        detail::requiredEnumString(
            define, QStringLiteral("action"), itemLocation,
            {QStringLiteral("define"), QStringLiteral("undefine")});
        const auto name =
            detail::requiredString(define, QStringLiteral("name"), itemLocation);
        static const QRegularExpression defineName(
            QStringLiteral(R"(^[A-Za-z_][A-Za-z0-9_]*$)"));
        if (!defineName.match(name).hasMatch()) {
            throw ContractError(itemLocation + ".name is not a valid macro identifier");
        }
        detail::stringValue(define, QStringLiteral("value"), itemLocation);
    }
}

void validateIncludes(const QJsonObject &semantic, const QString &location) {
    const auto includes = boundedArray(semantic, QStringLiteral("include_paths"), location,
                                       kMaxSemanticArray);
    for (qsizetype index = 0; index < includes.size(); ++index) {
        const auto itemLocation =
            location + ".include_paths[" + QString::number(index) + "]";
        if (!includes.at(index).isObject()) {
            throw ContractError(itemLocation + " must be an object");
        }
        const auto include = includes.at(index).toObject();
        detail::rejectUnknownKeys(
            include, {QStringLiteral("kind"), QStringLiteral("path")}, itemLocation);
        detail::requiredEnumString(
            include, QStringLiteral("kind"), itemLocation,
            {QStringLiteral("after"), QStringLiteral("framework"),
             QStringLiteral("include"), QStringLiteral("quote"),
             QStringLiteral("system")});
        detail::requiredString(include, QStringLiteral("path"), itemLocation);
    }
}

void validateTarget(const QJsonObject &semantic, const QString &location) {
    const auto target = requiredObject(semantic, QStringLiteral("target"), location);
    detail::rejectUnknownKeys(
        target, {QStringLiteral("build_target"), QStringLiteral("triple")},
        location + ".target");
    detail::stringValue(target, QStringLiteral("build_target"), location + ".target");
    detail::stringValue(target, QStringLiteral("triple"), location + ".target");
}

void validateSemantic(const QJsonObject &semantic, const QString &location) {
    detail::rejectUnknownKeys(
        semantic,
        {QStringLiteral("compiler"), QStringLiteral("defines"), QStringLiteral("flags"),
         QStringLiteral("include_paths"), QStringLiteral("language"),
         QStringLiteral("launcher"), QStringLiteral("standard"),
         QStringLiteral("sysroot"), QStringLiteral("target")},
        location);
    validateCompiler(semantic, location);
    validateDefines(semantic, location);
    validateStringArray(semantic, QStringLiteral("flags"), location, true);
    validateIncludes(semantic, location);
    const auto language =
        detail::stringValue(semantic, QStringLiteral("language"), location);
    if (!QStringList{QStringLiteral(""), QStringLiteral("c"), QStringLiteral("c++"),
                     QStringLiteral("objective-c"),
                     QStringLiteral("objective-c++")}
             .contains(language)) {
        throw ContractError(location + ".language is unsupported: " + language);
    }
    validateStringArray(semantic, QStringLiteral("launcher"), location, true);
    detail::stringValue(semantic, QStringLiteral("standard"), location);
    detail::stringValue(semantic, QStringLiteral("sysroot"), location);
    validateTarget(semantic, location);
}

std::optional<DiffConfiguration> parseConfiguration(const QJsonObject &unit,
                                                    const QString &key,
                                                    const QString &location) {
    if (!unit.contains(key)) {
        throw ContractError(location + "." + key + " is required");
    }
    const auto value = unit.value(key);
    if (value.isNull()) {
        return std::nullopt;
    }
    if (!value.isObject()) {
        throw ContractError(location + "." + key + " must be an object or null");
    }
    const auto object = value.toObject();
    const auto objectLocation = location + "." + key;
    detail::rejectUnknownKeys(
        object,
        {QStringLiteral("entry_index"), QStringLiteral("semantic"),
         QStringLiteral("semantic_digest")},
        objectLocation);
    DiffConfiguration configuration;
    configuration.entryIndex = boundedInteger(
        object, QStringLiteral("entry_index"), objectLocation, kMaxConfigurations);
    configuration.semantic =
        requiredObject(object, QStringLiteral("semantic"), objectLocation);
    validateSemantic(configuration.semantic, objectLocation + ".semantic");
    configuration.semanticDigest =
        requiredDigest(object, QStringLiteral("semantic_digest"), objectLocation);
    if (configuration.semanticDigest != semanticDigest(configuration.semantic)) {
        throw ContractError(objectLocation +
                            ".semantic_digest does not match semantic configuration");
    }
    return configuration;
}

DiffSource parseSource(const QJsonObject &unit, const QString &location) {
    const auto object = requiredObject(unit, QStringLiteral("source"), location);
    const auto objectLocation = location + ".source";
    detail::rejectUnknownKeys(
        object,
        {QStringLiteral("after"), QStringLiteral("before"), QStringLiteral("style")},
        objectLocation);
    DiffSource source;
    const auto after = nullableString(object, QStringLiteral("after"), objectLocation);
    const auto before = nullableString(object, QStringLiteral("before"), objectLocation);
    if (!after.isEmpty()) {
        source.after = after;
    }
    if (!before.isEmpty()) {
        source.before = before;
    }
    if (!source.after.has_value() && !source.before.has_value()) {
        throw ContractError(objectLocation + " must retain at least one source path");
    }
    source.style = detail::requiredEnumString(
        object, QStringLiteral("style"), objectLocation,
        {QStringLiteral("posix"), QStringLiteral("windows")});
    return source;
}

QJsonValue requiredJsonValue(const QJsonObject &object, const QString &key,
                             const QString &location) {
    if (!object.contains(key)) {
        throw ContractError(location + "." + key + " is required");
    }
    const auto value = object.value(key);
    if (value.isUndefined()) {
        throw ContractError(location + "." + key + " is invalid");
    }
    return value;
}

DiffChange parseChange(const QJsonValue &value, const QString &location) {
    if (!value.isObject()) {
        throw ContractError(location + " must be an object");
    }
    const auto object = value.toObject();
    detail::rejectUnknownKeys(
        object,
        {QStringLiteral("after"), QStringLiteral("before"), QStringLiteral("category"),
         QStringLiteral("suppressed"), QStringLiteral("suppression")},
        location);
    DiffChange change;
    change.after = requiredJsonValue(object, QStringLiteral("after"), location);
    change.before = requiredJsonValue(object, QStringLiteral("before"), location);
    change.category = detail::requiredEnumString(
        object, QStringLiteral("category"), location, kChangeCategories);
    change.suppressed =
        detail::requiredBool(object, QStringLiteral("suppressed"), location);
    const auto suppression =
        nullableString(object, QStringLiteral("suppression"), location);
    if (suppression.size() > 1024) {
        throw ContractError(location + ".suppression exceeds 1024 characters");
    }
    if (!suppression.isEmpty()) {
        change.suppression = suppression;
    }
    if (change.suppressed != change.suppression.has_value()) {
        throw ContractError(location +
                            ".suppression must be present exactly when suppressed is true");
    }
    return change;
}

QString sourceComparable(const std::optional<QString> &path, const QString &style) {
    if (!path.has_value()) {
        return {};
    }
    return style == QStringLiteral("windows") ? path->toCaseFolded() : *path;
}

const QVector<QPair<QString, QString>> kSemanticChangeFields = {
    {QStringLiteral("compiler"), QStringLiteral("compiler")},
    {QStringLiteral("define"), QStringLiteral("defines")},
    {QStringLiteral("flag"), QStringLiteral("flags")},
    {QStringLiteral("include"), QStringLiteral("include_paths")},
    {QStringLiteral("language"), QStringLiteral("language")},
    {QStringLiteral("launcher"), QStringLiteral("launcher")},
    {QStringLiteral("standard"), QStringLiteral("standard")},
    {QStringLiteral("sysroot"), QStringLiteral("sysroot")},
    {QStringLiteral("target"), QStringLiteral("target")},
};

void validateLifecycleChange(const DiffUnit &unit, const QString &location) {
    if (unit.changes.size() != 1 || unit.changes.front().category != unit.kind) {
        throw ContractError(location + ".changes must contain exactly one " + unit.kind +
                            " change");
    }
    const auto &change = unit.changes.front();
    if (unit.kind == QStringLiteral("added")) {
        if (!change.before.isNull() || change.after != unit.after->semantic) {
            throw ContractError(location + ".changes[0] does not match the added unit");
        }
    } else if (unit.kind == QStringLiteral("removed")) {
        if (!change.after.isNull() || change.before != unit.before->semantic) {
            throw ContractError(location + ".changes[0] does not match the removed unit");
        }
    }
}

void validateSemanticChanges(const DiffUnit &unit, const QString &location,
                             bool movedUnit) {
    QVector<QPair<QString, QString>> expected;
    if (movedUnit) {
        expected.append({QStringLiteral("moved"), QString()});
    }
    for (const auto &[category, field] : kSemanticChangeFields) {
        if (unit.before->semantic.value(field) != unit.after->semantic.value(field)) {
            expected.append({category, field});
        }
    }
    if (unit.changes.size() != expected.size()) {
        throw ContractError(location +
                            ".changes does not contain the exact semantic change set");
    }
    for (qsizetype index = 0; index < unit.changes.size(); ++index) {
        const auto &change = unit.changes.at(index);
        const auto &[category, field] = expected.at(index);
        if (change.category != category) {
            throw ContractError(location + ".changes[" + QString::number(index) +
                                "] is not in canonical semantic order");
        }
        if (category == QStringLiteral("moved")) {
            if (!movedUnit ||
                change.before.toString() != *unit.source.before ||
                change.after.toString() != *unit.source.after) {
                throw ContractError(location + ".changes contains an invalid moved category");
            }
            continue;
        }
        if (change.before != unit.before->semantic.value(field) ||
            change.after != unit.after->semantic.value(field) ||
            change.before == change.after) {
            throw ContractError(location + ".changes[" + QString::number(index) +
                                "] does not match semantic field " + field);
        }
    }
}

void validateUnitShape(const DiffUnit &unit, const QString &location) {
    const auto hasBefore = unit.before.has_value();
    const auto hasAfter = unit.after.has_value();
    const auto hasBeforeSource = unit.source.before.has_value();
    const auto hasAfterSource = unit.source.after.has_value();
    if (unit.changes.isEmpty()) {
        throw ContractError(location + ".changes must not be empty");
    }
    if (unit.kind == QStringLiteral("added")) {
        if (hasBefore || !hasAfter || hasBeforeSource || !hasAfterSource) {
            throw ContractError(location + " added unit sides are inconsistent");
        }
        validateLifecycleChange(unit, location);
        return;
    }
    if (unit.kind == QStringLiteral("removed")) {
        if (!hasBefore || hasAfter || !hasBeforeSource || hasAfterSource) {
            throw ContractError(location + " removed unit sides are inconsistent");
        }
        validateLifecycleChange(unit, location);
        return;
    }
    if (!hasBefore || !hasAfter || !hasBeforeSource || !hasAfterSource) {
        throw ContractError(location + " changed/moved unit must retain both sides");
    }
    const auto sameSource =
        sourceComparable(unit.source.before, unit.source.style) ==
        sourceComparable(unit.source.after, unit.source.style);
    const auto sameConfiguration =
        unit.before->semanticDigest == unit.after->semanticDigest;
    if (unit.kind == QStringLiteral("moved")) {
        if (sameSource) {
            throw ContractError(location + " moved unit identity is inconsistent");
        }
        validateSemanticChanges(unit, location, true);
    } else {
        if (!sameSource || sameConfiguration) {
            throw ContractError(location + " changed unit identity is inconsistent");
        }
        validateSemanticChanges(unit, location, false);
    }
}

DiffInput parseInput(const QJsonObject &inputs, const QString &key) {
    const auto object = requiredObject(inputs, key, QStringLiteral("root.inputs"));
    const auto location = QStringLiteral("root.inputs.") + key;
    detail::rejectUnknownKeys(
        object,
        {QStringLiteral("configuration_count"), QStringLiteral("label"),
         QStringLiteral("semantic_digest"), QStringLiteral("source_count")},
        location);
    DiffInput input;
    input.configurationCount = boundedInteger(
        object, QStringLiteral("configuration_count"), location, kMaxConfigurations);
    input.label = detail::requiredString(object, QStringLiteral("label"), location);
    if (input.label.size() > 256) {
        throw ContractError(location + ".label exceeds 256 characters");
    }
    input.semanticDigest =
        requiredDigest(object, QStringLiteral("semantic_digest"), location);
    input.sourceCount = boundedInteger(
        object, QStringLiteral("source_count"), location, kMaxConfigurations);
    if (input.sourceCount > input.configurationCount) {
        throw ContractError(location + ".source_count exceeds configuration_count");
    }
    return input;
}

DiffPolicy parsePolicy(const QJsonObject &root) {
    const auto object = requiredObject(root, QStringLiteral("policy"), QStringLiteral("root"));
    const auto location = QStringLiteral("root.policy");
    detail::rejectUnknownKeys(
        object,
        {QStringLiteral("ignored_fields"), QStringLiteral("suppression_rules"),
         QStringLiteral("version")},
        location);
    DiffPolicy policy;
    const auto ignored = boundedArray(object, QStringLiteral("ignored_fields"), location,
                                      kMaxIgnoredFields);
    QSet<QString> seenIgnored;
    for (qsizetype index = 0; index < ignored.size(); ++index) {
        if (!ignored.at(index).isString() || ignored.at(index).toString().isEmpty() ||
            ignored.at(index).toString().contains(QChar::Null)) {
            throw ContractError(location + ".ignored_fields[" + QString::number(index) +
                                "] must be a non-empty string");
        }
        const auto value = ignored.at(index).toString();
        if (seenIgnored.contains(value)) {
            throw ContractError(location + ".ignored_fields contains a duplicate");
        }
        seenIgnored.insert(value);
        policy.ignoredFields.append(value);
    }
    const auto rules = boundedArray(object, QStringLiteral("suppression_rules"), location,
                                    kMaxSuppressions);
    QSet<QString> seenRules;
    for (qsizetype index = 0; index < rules.size(); ++index) {
        const auto ruleLocation =
            location + ".suppression_rules[" + QString::number(index) + "]";
        if (!rules.at(index).isObject()) {
            throw ContractError(ruleLocation + " must be an object");
        }
        const auto rule = rules.at(index).toObject();
        detail::rejectUnknownKeys(
            rule, {QStringLiteral("category"), QStringLiteral("path")}, ruleLocation);
        DiffSuppressionRule parsed;
        parsed.category = detail::requiredString(
            rule, QStringLiteral("category"), ruleLocation);
        if (parsed.category != QStringLiteral("*") &&
            !kChangeCategories.contains(parsed.category)) {
            throw ContractError(ruleLocation + ".category is unsupported: " +
                                parsed.category);
        }
        parsed.path = detail::requiredString(rule, QStringLiteral("path"), ruleLocation);
        if (parsed.path.size() > 1024) {
            throw ContractError(ruleLocation + ".path exceeds 1024 characters");
        }
        if (parsed.path.contains(QLatin1Char('\\')) ||
            parsed.path.contains(QLatin1Char('[')) ||
            parsed.path.contains(QLatin1Char(']'))) {
            throw ContractError(
                ruleLocation +
                ".path supports only /, literal characters, *, **, and ?");
        }
        const auto identity = parsed.category + QChar::Null + parsed.path;
        if (seenRules.contains(identity)) {
            throw ContractError(location + ".suppression_rules contains a duplicate");
        }
        if (!policy.suppressionRules.isEmpty()) {
            const auto &previous = policy.suppressionRules.back();
            const auto previousIdentity =
                previous.category.toUtf8() + QByteArray(1, '\0') + previous.path.toUtf8();
            const auto currentIdentity =
                parsed.category.toUtf8() + QByteArray(1, '\0') + parsed.path.toUtf8();
            if (currentIdentity < previousIdentity) {
                throw ContractError(location +
                                    ".suppression_rules is not in canonical order");
            }
        }
        seenRules.insert(identity);
        policy.suppressionRules.append(parsed);
    }
    policy.version = detail::requiredString(object, QStringLiteral("version"), location);
    if (policy.ignoredFields != kIgnoredFields) {
        throw ContractError(location +
                            ".ignored_fields does not match diff policy v1");
    }
    if (policy.version != QString::fromLatin1(kDiffPolicyV1)) {
        throw ContractError(location + ".version is unsupported: " + policy.version);
    }
    return policy;
}

QString globRegularExpression(const QString &pattern) {
    QString expression = pattern.contains(QLatin1Char('/'))
                             ? QStringLiteral("^")
                             : QStringLiteral("^(?:.*/)?");
    for (qsizetype index = 0; index < pattern.size();) {
        const auto character = pattern.at(index);
        if (character == QLatin1Char('*')) {
            if (index + 1 < pattern.size() && pattern.at(index + 1) == QLatin1Char('*')) {
                if (index + 2 < pattern.size() &&
                    pattern.at(index + 2) == QLatin1Char('/')) {
                    expression += QStringLiteral("(?:.*/)?");
                    index += 3;
                } else {
                    expression += QStringLiteral(".*");
                    index += 2;
                }
            } else {
                expression += QStringLiteral("[^/]*");
                ++index;
            }
        } else if (character == QLatin1Char('?')) {
            expression += QStringLiteral("[^/]");
            ++index;
        } else {
            expression += QRegularExpression::escape(QString(character));
            ++index;
        }
    }
    return expression + QLatin1Char('$');
}

bool suppressionMatches(const DiffSuppressionRule &rule, const DiffUnit &unit,
                        const DiffChange &change) {
    if (rule.category != QStringLiteral("*") && rule.category != change.category) {
        return false;
    }
    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
    if (unit.source.style == QStringLiteral("windows")) {
        options |= QRegularExpression::CaseInsensitiveOption;
    }
    const QRegularExpression expression(globRegularExpression(rule.path), options);
    const auto matches = [&expression](const std::optional<QString> &path) {
        if (!path.has_value()) {
            return false;
        }
        auto normalized = *path;
        normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
        return expression.match(normalized).hasMatch();
    };
    return matches(unit.source.before) || matches(unit.source.after);
}

void validateSuppressions(const DiffPolicy &policy, const DiffUnit &unit,
                          const QString &location) {
    for (qsizetype index = 0; index < unit.changes.size(); ++index) {
        const auto &change = unit.changes.at(index);
        std::optional<QString> expected;
        for (const auto &rule : policy.suppressionRules) {
            if (suppressionMatches(rule, unit, change)) {
                expected = rule.category + QLatin1Char(':') + rule.path;
                break;
            }
        }
        if (change.suppressed != expected.has_value() || change.suppression != expected) {
            throw ContractError(location + ".changes[" + QString::number(index) +
                                "].suppression does not match canonical policy and source");
        }
    }
}

DiffUnit parseUnit(const QJsonValue &value, qsizetype index, const DiffPolicy &policy) {
    const auto location = QStringLiteral("root.units[") + QString::number(index) + "]";
    if (!value.isObject()) {
        throw ContractError(location + " must be an object");
    }
    const auto object = value.toObject();
    detail::rejectUnknownKeys(
        object,
        {QStringLiteral("after"), QStringLiteral("before"), QStringLiteral("changes"),
         QStringLiteral("kind"), QStringLiteral("source"), QStringLiteral("suppressed")},
        location);
    DiffUnit unit;
    unit.after = parseConfiguration(object, QStringLiteral("after"), location);
    unit.before = parseConfiguration(object, QStringLiteral("before"), location);
    unit.kind = detail::requiredEnumString(
        object, QStringLiteral("kind"), location,
        {QStringLiteral("added"), QStringLiteral("changed"), QStringLiteral("moved"),
         QStringLiteral("removed")});
    unit.source = parseSource(object, location);
    unit.suppressed =
        detail::requiredBool(object, QStringLiteral("suppressed"), location);
    const auto changes = boundedArray(object, QStringLiteral("changes"), location,
                                      kMaxDiffChanges);
    unit.changes.reserve(changes.size());
    for (qsizetype changeIndex = 0; changeIndex < changes.size(); ++changeIndex) {
        unit.changes.append(parseChange(
            changes.at(changeIndex),
            location + ".changes[" + QString::number(changeIndex) + "]"));
    }
    validateUnitShape(unit, location);
    const auto allSuppressed =
        std::all_of(unit.changes.cbegin(), unit.changes.cend(),
                    [](const DiffChange &change) { return change.suppressed; });
    if (unit.suppressed != allSuppressed) {
        throw ContractError(location + ".suppressed does not match its changes");
    }
    validateSuppressions(policy, unit, location);
    return unit;
}

DiffSummary parseSummary(const QJsonObject &root) {
    const auto object = requiredObject(root, QStringLiteral("summary"), QStringLiteral("root"));
    const auto location = QStringLiteral("root.summary");
    detail::rejectUnknownKeys(
        object,
        {QStringLiteral("added"), QStringLiteral("changed"),
         QStringLiteral("change_count"), QStringLiteral("moved"),
         QStringLiteral("removed"), QStringLiteral("suppressed_changes"),
         QStringLiteral("suppressed_units"), QStringLiteral("unchanged"),
         QStringLiteral("visible_changes"), QStringLiteral("visible_units")},
        location);
    DiffSummary summary;
    summary.added = boundedInteger(object, QStringLiteral("added"), location,
                                   kMaxConfigurations);
    summary.changed = boundedInteger(object, QStringLiteral("changed"), location,
                                     kMaxConfigurations);
    summary.changeCount = boundedInteger(object, QStringLiteral("change_count"), location,
                                         kMaxSummaryChanges);
    summary.moved = boundedInteger(object, QStringLiteral("moved"), location,
                                   kMaxConfigurations);
    summary.removed = boundedInteger(object, QStringLiteral("removed"), location,
                                     kMaxConfigurations);
    summary.suppressedChanges = boundedInteger(
        object, QStringLiteral("suppressed_changes"), location, kMaxSummaryChanges);
    summary.suppressedUnits = boundedInteger(
        object, QStringLiteral("suppressed_units"), location, kMaxDiffUnits);
    summary.unchanged = boundedInteger(object, QStringLiteral("unchanged"), location,
                                       kMaxConfigurations);
    summary.visibleChanges = boundedInteger(
        object, QStringLiteral("visible_changes"), location, kMaxSummaryChanges);
    summary.visibleUnits = boundedInteger(
        object, QStringLiteral("visible_units"), location, kMaxDiffUnits);
    return summary;
}

void validateSummary(const DiffReport &report) {
    DiffSummary actual;
    actual.unchanged = report.summary.unchanged;
    for (const auto &unit : report.units) {
        if (unit.kind == QStringLiteral("added")) {
            ++actual.added;
        } else if (unit.kind == QStringLiteral("changed")) {
            ++actual.changed;
        } else if (unit.kind == QStringLiteral("moved")) {
            ++actual.moved;
        } else {
            ++actual.removed;
        }
        if (unit.suppressed) {
            ++actual.suppressedUnits;
        } else {
            ++actual.visibleUnits;
        }
        actual.changeCount += unit.changes.size();
        for (const auto &change : unit.changes) {
            if (change.suppressed) {
                ++actual.suppressedChanges;
            } else {
                ++actual.visibleChanges;
            }
        }
    }
    const auto &declared = report.summary;
    if (actual.added != declared.added || actual.changed != declared.changed ||
        actual.changeCount != declared.changeCount || actual.moved != declared.moved ||
        actual.removed != declared.removed ||
        actual.suppressedChanges != declared.suppressedChanges ||
        actual.suppressedUnits != declared.suppressedUnits ||
        actual.visibleChanges != declared.visibleChanges ||
        actual.visibleUnits != declared.visibleUnits) {
        throw ContractError("root.summary does not match root.units");
    }
    const auto expectedBefore =
        declared.removed + declared.moved + declared.changed + declared.unchanged;
    const auto expectedAfter =
        declared.added + declared.moved + declared.changed + declared.unchanged;
    if (report.beforeInput.configurationCount != expectedBefore ||
        report.afterInput.configurationCount != expectedAfter) {
        throw ContractError("root.inputs configuration counts do not match summary");
    }
}

QVector<DiffDiagnostic> parseDiagnostics(const QJsonObject &root) {
    const auto values = boundedArray(root, QStringLiteral("diagnostics"), QStringLiteral("root"),
                                     kMaxDiffDiagnostics);
    QVector<DiffDiagnostic> diagnostics;
    diagnostics.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        const auto location =
            QStringLiteral("root.diagnostics[") + QString::number(index) + "]";
        if (!values.at(index).isObject()) {
            throw ContractError(location + " must be an object");
        }
        const auto object = values.at(index).toObject();
        detail::rejectUnknownKeys(
            object,
            {QStringLiteral("code"), QStringLiteral("message"),
             QStringLiteral("severity"), QStringLiteral("source")},
            location);
        diagnostics.append(
            {detail::requiredString(object, QStringLiteral("code"), location),
             detail::requiredString(object, QStringLiteral("message"), location),
             detail::requiredEnumString(
                 object, QStringLiteral("severity"), location,
                 {QStringLiteral("error"), QStringLiteral("info"),
                  QStringLiteral("warning")}),
             detail::optionalString(object, QStringLiteral("source"), location)});
    }
    return diagnostics;
}

DiffReport parseDiffDocument(const QJsonDocument &document) {
    if (!document.isObject()) {
        throw ContractError("diff report root must be an object");
    }
    const auto root = document.object();
    detail::rejectUnknownKeys(
        root,
        {QStringLiteral("diagnostics"), QStringLiteral("inputs"),
         QStringLiteral("policy"), QStringLiteral("producer"),
         QStringLiteral("schema_version"), QStringLiteral("summary"),
         QStringLiteral("units")},
        QStringLiteral("root"));
    DiffReport report;
    report.schemaVersion =
        detail::requiredString(root, QStringLiteral("schema_version"), QStringLiteral("root"));
    if (report.schemaVersion != QString::fromLatin1(kDiffSchemaV1)) {
        throw ContractError("root.schema_version is unsupported: " + report.schemaVersion);
    }
    const auto producer =
        requiredObject(root, QStringLiteral("producer"), QStringLiteral("root"));
    detail::rejectUnknownKeys(
        producer, {QStringLiteral("name"), QStringLiteral("version")},
        QStringLiteral("root.producer"));
    const auto producerName = detail::requiredString(
        producer, QStringLiteral("name"), QStringLiteral("root.producer"));
    if (producerName != QStringLiteral("buildscope")) {
        throw ContractError("root.producer.name is unsupported: " + producerName);
    }
    report.producerVersion = detail::requiredString(
        producer, QStringLiteral("version"), QStringLiteral("root.producer"));
    const auto inputs = requiredObject(root, QStringLiteral("inputs"), QStringLiteral("root"));
    detail::rejectUnknownKeys(inputs, {QStringLiteral("after"), QStringLiteral("before")},
                              QStringLiteral("root.inputs"));
    report.afterInput = parseInput(inputs, QStringLiteral("after"));
    report.beforeInput = parseInput(inputs, QStringLiteral("before"));
    report.policy = parsePolicy(root);
    report.diagnostics = parseDiagnostics(root);
    report.summary = parseSummary(root);
    const auto units = boundedArray(root, QStringLiteral("units"), QStringLiteral("root"),
                                    kMaxDiffUnits);
    report.units.reserve(units.size());
    for (qsizetype index = 0; index < units.size(); ++index) {
        report.units.append(parseUnit(units.at(index), index, report.policy));
    }
    validateSummary(report);
    return report;
}

}  // namespace

DiffReport loadDiffFile(const QString &path) {
    return parseDiffDocument(detail::loadJsonContractFile(
        path, QStringLiteral("diff report")));
}

QString renderDiffValue(const QJsonValue &value) {
    if (value.isNull() || value.isUndefined()) {
        return QStringLiteral("—");
    }
    if (value.isString()) {
        return value.toString();
    }
    QJsonArray wrapper;
    wrapper.append(value);
    auto rendered = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(rendered.mid(1, rendered.size() - 2));
}

}  // namespace buildscope
