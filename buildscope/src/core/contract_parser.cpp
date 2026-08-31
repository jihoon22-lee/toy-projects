#include "contract_parser.hpp"

#include <QJsonArray>
#include <QHash>
#include <QJsonObject>
#include <QSet>

#include <cmath>
#include <limits>

namespace buildscope::detail {
namespace {

constexpr qsizetype kMaxEntries = 100000;
constexpr qsizetype kMaxArguments = 32768;
constexpr qint64 kMaxFieldChars = 1024LL * 1024LL;
constexpr qint64 kMaxCommandChars = 4LL * 1024LL * 1024LL;

bool isOneOf(const QString &value, const QStringList &allowed) {
    return allowed.contains(value);
}

}  // namespace

void rejectUnknownKeys(const QJsonObject &object, const QStringList &allowed,
                       const QString &location) {
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (!allowed.contains(iterator.key())) {
            throw ContractError(location + " contains unsupported field " + iterator.key());
        }
    }
}

QString requiredString(const QJsonObject &object, const QString &key, const QString &location) {
    const auto value = object.value(key);
    if (!value.isString() || value.toString().isEmpty() ||
        value.toString().contains(QChar::Null)) {
        throw ContractError(location + "." + key + " must be a non-empty string");
    }
    if (value.toString().size() > kMaxFieldChars) {
        throw ContractError(location + "." + key + " exceeds the character limit");
    }
    return value.toString();
}

QString stringValue(const QJsonObject &object, const QString &key, const QString &location) {
    const auto value = object.value(key);
    if (!value.isString() || value.toString().contains(QChar::Null)) {
        throw ContractError(location + "." + key + " must be a string");
    }
    if (value.toString().size() > kMaxFieldChars) {
        throw ContractError(location + "." + key + " exceeds the character limit");
    }
    return value.toString();
}

QString requiredEnumString(const QJsonObject &object, const QString &key, const QString &location,
                           const QStringList &allowed) {
    const auto value = requiredString(object, key, location);
    if (!isOneOf(value, allowed)) {
        throw ContractError(location + "." + key + " is unsupported: " + value);
    }
    return value;
}

QString optionalString(const QJsonObject &object, const QString &key, const QString &location) {
    const auto value = object.value(key);
    if (value.isNull() || value.isUndefined()) {
        return {};
    }
    if (!value.isString() || value.toString().isEmpty() ||
        value.toString().contains(QChar::Null)) {
        throw ContractError(location + "." + key + " must be a non-empty string or null");
    }
    if (key == QStringLiteral("command") && value.toString().size() > kMaxCommandChars) {
        throw ContractError(location + ".command exceeds the character limit");
    }
    if (key != QStringLiteral("command") && value.toString().size() > kMaxFieldChars) {
        throw ContractError(location + "." + key + " exceeds the character limit");
    }
    return value.toString();
}

QStringList optionalArguments(const QJsonObject &object, const QString &location,
                              bool v2) {
    const auto value = object.value(QStringLiteral("arguments"));
    if (value.isNull() || value.isUndefined()) {
        return {};
    }
    if (!value.isArray() || value.toArray().isEmpty()) {
        throw ContractError(location + ".arguments must be a non-empty string array or null");
    }
    const auto values = value.toArray();
    if (values.size() > kMaxArguments) {
        throw ContractError(location + ".arguments exceeds the argument limit");
    }
    QStringList arguments;
    arguments.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        if (!values.at(index).isString()) {
            throw ContractError(location + ".arguments[" + QString::number(index) +
                                "] must be a string");
        }
        const auto argument = values.at(index).toString();
        if ((v2 && index == 0 && argument.isEmpty()) || argument.contains(QChar::Null)) {
            throw ContractError(location + ".arguments[" + QString::number(index) +
                                "] must be a non-empty string");
        }
        if (argument.size() > kMaxFieldChars) {
            throw ContractError(location + ".arguments[" + QString::number(index) +
                                "] exceeds the character limit");
        }
        arguments.append(argument);
    }
    return arguments;
}

bool isPresent(const QJsonValue &value) {
    return !value.isNull() && !value.isUndefined();
}

bool requiredBool(const QJsonObject &object, const QString &key, const QString &location) {
    const auto value = object.value(key);
    if (!value.isBool()) {
        throw ContractError(location + "." + key + " must be a boolean");
    }
    return value.toBool();
}

std::optional<bool> requiredNullableBool(const QJsonObject &object, const QString &key,
                                         const QString &location) {
    const auto value = object.value(key);
    if (value.isNull()) {
        return std::nullopt;
    }
    if (!value.isBool()) {
        throw ContractError(location + "." + key + " must be a boolean or null");
    }
    return value.toBool();
}

qsizetype requiredInteger(const QJsonObject &object, const QString &key,
                          const QString &location) {
    const auto value = object.value(key);
    const auto number = value.toDouble();
    if (!value.isDouble() || !std::isfinite(number) || std::floor(number) != number ||
        number < 0.0 || number > static_cast<double>(std::numeric_limits<qsizetype>::max())) {
        throw ContractError(location + "." + key + " must be a non-negative integer");
    }
    return static_cast<qsizetype>(number);
}

QStringList requiredStringArray(const QJsonObject &object, const QString &key,
                                const QString &location, bool nonEmpty,
                                bool allowEmptyNonCompiler) {
    const auto value = object.value(key);
    if (!value.isArray() || (nonEmpty && value.toArray().isEmpty())) {
        throw ContractError(location + "." + key + " must be a" +
                            (nonEmpty ? QStringLiteral(" non-empty") : QString()) +
                            QStringLiteral(" string array"));
    }
    const auto values = value.toArray();
    if (values.size() > kMaxArguments) {
        throw ContractError(location + "." + key + " exceeds the argument limit");
    }
    QStringList result;
    result.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        const auto item = values.at(index);
        if (!item.isString()) {
            throw ContractError(location + "." + key + "[" + QString::number(index) +
                                "] must be a string");
        }
        const auto string = item.toString();
        if ((string.isEmpty() && (!allowEmptyNonCompiler || index == 0)) ||
            string.contains(QChar::Null)) {
            throw ContractError(location + "." + key + "[" + QString::number(index) +
                                "] must be a non-empty string");
        }
        if (string.size() > kMaxFieldChars) {
            throw ContractError(location + "." + key + "[" + QString::number(index) +
                                "] exceeds the character limit");
        }
        result.append(string);
    }
    return result;
}

ParsedRawEntry parseRawEntry(const QJsonValue &value, qsizetype index, bool v2) {
    const auto location = QStringLiteral("entries[") + QString::number(index) + "]";
    if (!value.isObject()) {
        throw ContractError(location + " must be an object");
    }
    const auto object = value.toObject();
    if (v2) {
        rejectUnknownKeys(object,
                          {QStringLiteral("arguments"), QStringLiteral("command"),
                           QStringLiteral("diagnostics"), QStringLiteral("directory"),
                           QStringLiteral("file"), QStringLiteral("normalized"),
                           QStringLiteral("output"), QStringLiteral("state")},
                          location);
        for (const auto &key : {QStringLiteral("arguments"), QStringLiteral("command"),
                                QStringLiteral("diagnostics"), QStringLiteral("directory"),
                                QStringLiteral("file"), QStringLiteral("normalized"),
                                QStringLiteral("output"), QStringLiteral("state")}) {
            if (!object.contains(key)) {
                throw ContractError(location + "." + key + " is required");
            }
        }
    }

    ParsedRawEntry entry;
    entry.file = requiredString(object, QStringLiteral("file"), location);
    entry.directory = requiredString(object, QStringLiteral("directory"), location);
    entry.arguments = optionalArguments(object, location, v2);
    entry.command = optionalString(object, QStringLiteral("command"), location);
    entry.output = optionalString(object, QStringLiteral("output"), location);
    entry.hasArguments = isPresent(object.value(QStringLiteral("arguments")));
    entry.hasCommand = isPresent(object.value(QStringLiteral("command")));
    if ((!v2 && entry.hasArguments == entry.hasCommand) ||
        (v2 && !entry.hasArguments && !entry.hasCommand)) {
        throw ContractError(location + " must contain " +
                            (v2 ? QStringLiteral("at least one") : QStringLiteral("exactly one")) +
                            QStringLiteral(" of arguments or command"));
    }
    return entry;
}

bool isV2Schema(const QString &schemaVersion) {
    const bool v1 = schemaVersion == QString::fromLatin1(kSnapshotSchemaV1);
    const bool v2 = schemaVersion == QString::fromLatin1(kSnapshotSchemaV2);
    if (!v1 && !v2) {
        throw ContractError("root.schema_version is unsupported: " + schemaVersion);
    }
    return v2;
}

void validateV2Root(const QJsonObject &root, bool v2) {
    if (v2) {
        rejectUnknownKeys(root,
                          {QStringLiteral("entries"), QStringLiteral("producer"),
                           QStringLiteral("schema_version"), QStringLiteral("source")},
                          "root");
    }
}

void parseProducer(const QJsonObject &root, bool v2, Snapshot &snapshot) {
    const auto producer = root.value(QStringLiteral("producer"));
    if (!producer.isObject()) {
        throw ContractError("root.producer must be an object");
    }
    const auto producerObject = producer.toObject();
    if (v2) {
        rejectUnknownKeys(producerObject, {QStringLiteral("name"), QStringLiteral("version")},
                          "root.producer");
    }
    const auto producerName = requiredString(producerObject, QStringLiteral("name"),
                                             "root.producer");
    if (producerName != QStringLiteral("buildscope")) {
        throw ContractError("root.producer.name is unsupported: " + producerName);
    }
    snapshot.producerVersion =
        requiredString(producerObject, QStringLiteral("version"), "root.producer");
}

QJsonObject parseSource(const QJsonObject &root, bool v2, Snapshot &snapshot) {
    const auto source = root.value(QStringLiteral("source"));
    if (!source.isObject()) {
        throw ContractError("root.source must be an object");
    }
    const auto sourceObject = source.toObject();
    if (v2) {
        rejectUnknownKeys(sourceObject,
                          {QStringLiteral("entry_count"), QStringLiteral("path"),
                           QStringLiteral("project_root")},
                          "root.source");
    }
    snapshot.sourcePath = requiredString(sourceObject, QStringLiteral("path"), "root.source");
    if (v2) {
        snapshot.projectRoot =
            requiredString(sourceObject, QStringLiteral("project_root"), "root.source");
    }
    return sourceObject;
}

QJsonArray parseEntries(const QJsonObject &root, const QJsonObject &sourceObject) {
    const auto entries = root.value(QStringLiteral("entries"));
    if (!entries.isArray()) {
        throw ContractError("root.entries must be an array");
    }
    const auto entryArray = entries.toArray();
    if (entryArray.size() > kMaxEntries) {
        throw ContractError("root.entries exceeds 100000 entry limit");
    }
    const auto declaredCount = sourceObject.value(QStringLiteral("entry_count"));
    const auto declaredCountNumber = declaredCount.toDouble(-1.0);
    if (!declaredCount.isDouble() || !std::isfinite(declaredCountNumber) ||
        std::floor(declaredCountNumber) != declaredCountNumber || declaredCountNumber < 0.0 ||
        declaredCountNumber != static_cast<double>(entryArray.size())) {
        throw ContractError("root.source.entry_count must match root.entries size");
    }
    return entryArray;
}

SnapshotEntry parseV1Entry(const QJsonValue &value, qsizetype index) {
    const auto raw = parseRawEntry(value, index, false);
    SnapshotEntry entry;
    entry.file = raw.file;
    entry.directory = raw.directory;
    entry.arguments = raw.arguments;
    entry.command = raw.command;
    entry.output = raw.output;
    return entry;
}

QString v2SourceKey(const SnapshotEntry &entry) {
    auto path = entry.normalized.source.path;
    if (entry.normalized.commandStyle == QStringLiteral("windows")) {
        path = path.toCaseFolded();
    }
    return entry.normalized.commandStyle + QChar::Null + path;
}

void validateV2EntrySets(const Snapshot &snapshot) {
    QHash<QString, qsizetype> duplicateCounts;
    QHash<QString, QSet<QString>> sourceConfigurations;
    QSet<qsizetype> originalIndexes;
    for (qsizetype index = 0; index < snapshot.entries.size(); ++index) {
        const auto &entry = snapshot.entries.at(index);
        const auto location = QStringLiteral("entries[") + QString::number(index) + "].state";
        if (entry.state.entryIndex >= snapshot.entries.size() ||
            originalIndexes.contains(entry.state.entryIndex)) {
            throw ContractError(location + ".entry_index must be a unique in-range index");
        }
        originalIndexes.insert(entry.state.entryIndex);
        const auto source = v2SourceKey(entry);
        const auto configurationKey = source + QChar::Null + entry.normalized.configuration;
        ++duplicateCounts[configurationKey];
        sourceConfigurations[source].insert(entry.normalized.configuration);
    }
    for (qsizetype index = 0; index < snapshot.entries.size(); ++index) {
        const auto &entry = snapshot.entries.at(index);
        const auto location = QStringLiteral("entries[") + QString::number(index) + "].state";
        const auto source = v2SourceKey(entry);
        const auto configurationKey = source + QChar::Null + entry.normalized.configuration;
        if (entry.state.duplicate != (duplicateCounts.value(configurationKey) > 1)) {
            throw ContractError(location + ".duplicate does not match the entry set");
        }
        if (entry.state.sourceConfigurationCount != sourceConfigurations.value(source).size()) {
            throw ContractError(location +
                                ".source_configuration_count does not match the entry set");
        }
    }
}

void parseEntriesInto(Snapshot &snapshot, const QJsonArray &entryArray, bool v2) {
    snapshot.entries.reserve(entryArray.size());
    for (qsizetype index = 0; index < entryArray.size(); ++index) {
        snapshot.entries.append(v2 ? parseV2Entry(entryArray.at(index), index)
                                   : parseV1Entry(entryArray.at(index), index));
    }
    if (v2) {
        validateV2EntrySets(snapshot);
    }
}

Snapshot parseSnapshotDocument(const QJsonDocument &document) {
    if (!document.isObject()) {
        throw ContractError("snapshot root must be an object");
    }
    const auto root = document.object();
    Snapshot snapshot;
    snapshot.schemaVersion = requiredString(root, QStringLiteral("schema_version"), "root");
    const bool v2 = isV2Schema(snapshot.schemaVersion);
    validateV2Root(root, v2);
    parseProducer(root, v2, snapshot);
    const auto sourceObject = parseSource(root, v2, snapshot);
    parseEntriesInto(snapshot, parseEntries(root, sourceObject), v2);
    return snapshot;
}

}  // namespace buildscope::detail
