#include "diff_validation.hpp"

#include "buildscope/contract.hpp"
#include "contract_parser.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace buildscope::diff_validation {
namespace {

constexpr qsizetype kMaxSemanticArray = 32768;
constexpr qsizetype kMaxFieldChars = 1024 * 1024;
constexpr qsizetype kMaxConfigurations = 100000;

const QStringList kChangeCategories = {
    QStringLiteral("added"),    QStringLiteral("compiler"), QStringLiteral("define"),
    QStringLiteral("flag"),     QStringLiteral("include"),  QStringLiteral("language"),
    QStringLiteral("launcher"), QStringLiteral("moved"),    QStringLiteral("removed"),
    QStringLiteral("standard"), QStringLiteral("sysroot"),  QStringLiteral("target"),
};

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

QString requiredDigest(const QJsonObject &object, const QString &key,
                       const QString &location) {
    static const QRegularExpression pattern(
        QStringLiteral(R"(^sha256:[0-9a-f]{64}$)"));
    const auto digest = detail::requiredString(object, key, location);
    if (!pattern.match(digest).hasMatch()) {
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
         QStringLiteral("name"), QStringLiteral("path"), QStringLiteral("wrappers")},
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
        const auto itemLocation = location + ".defines[" + QString::number(index) + "]";
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
        const auto name = detail::requiredString(
            define, QStringLiteral("name"), itemLocation);
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
    const auto language = detail::stringValue(
        semantic, QStringLiteral("language"), location);
    if (!QStringList{QStringLiteral(""), QStringLiteral("c"), QStringLiteral("c++"),
                     QStringLiteral("objective-c"), QStringLiteral("objective-c++")}
             .contains(language)) {
        throw ContractError(location + ".language is unsupported: " + language);
    }
    validateStringArray(semantic, QStringLiteral("launcher"), location, true);
    detail::stringValue(semantic, QStringLiteral("standard"), location);
    detail::stringValue(semantic, QStringLiteral("sysroot"), location);
    validateTarget(semantic, location);
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

QString sourceComparable(const std::optional<QString> &path, const QString &style) {
    if (!path.has_value()) {
        return {};
    }
    return style == QStringLiteral("windows") ? path->toCaseFolded() : *path;
}

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
            if (!movedUnit || change.before.toString() != *unit.source.before ||
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

void validateLifecycleUnitShape(const DiffUnit &unit, const QString &location) {
    const auto hasBefore = unit.before.has_value();
    const auto hasAfter = unit.after.has_value();
    const auto hasBeforeSource = unit.source.before.has_value();
    const auto hasAfterSource = unit.source.after.has_value();
    const auto added = unit.kind == QStringLiteral("added");
    if (added) {
        if (hasBefore || !hasAfter || hasBeforeSource || !hasAfterSource) {
            throw ContractError(location + " added unit sides are inconsistent");
        }
    } else if (!hasBefore || hasAfter || !hasBeforeSource || hasAfterSource) {
        throw ContractError(location + " removed unit sides are inconsistent");
    }
    validateLifecycleChange(unit, location);
}

void validatePairedUnitShape(const DiffUnit &unit, const QString &location) {
    if (!unit.before.has_value() || !unit.after.has_value() ||
        !unit.source.before.has_value() || !unit.source.after.has_value()) {
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
        return;
    }
    if (!sameSource || sameConfiguration) {
        throw ContractError(location + " changed unit identity is inconsistent");
    }
    validateSemanticChanges(unit, location, false);
}

}  // namespace

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
    configuration.semantic = requiredObject(
        object, QStringLiteral("semantic"), objectLocation);
    validateSemantic(configuration.semantic, objectLocation + ".semantic");
    configuration.semanticDigest = requiredDigest(
        object, QStringLiteral("semantic_digest"), objectLocation);
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
    change.suppressed = detail::requiredBool(
        object, QStringLiteral("suppressed"), location);
    const auto suppression = nullableString(
        object, QStringLiteral("suppression"), location);
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

void validateUnitShape(const DiffUnit &unit, const QString &location) {
    if (unit.changes.isEmpty()) {
        throw ContractError(location + ".changes must not be empty");
    }
    if (unit.kind == QStringLiteral("added") ||
        unit.kind == QStringLiteral("removed")) {
        validateLifecycleUnitShape(unit, location);
        return;
    }
    validatePairedUnitShape(unit, location);
}

}  // namespace buildscope::diff_validation
