#include "buildscope/contract.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>

namespace buildscope {
namespace {

constexpr qint64 kMaxSnapshotBytes = 64LL * 1024LL * 1024LL;
constexpr qsizetype kMaxEntries = 100000;

QString requiredString(const QJsonObject &object, const QString &key, const QString &location) {
    const auto value = object.value(key);
    if (!value.isString() || value.toString().isEmpty()) {
        throw ContractError(location + "." + key + " must be a non-empty string");
    }
    return value.toString();
}

QString optionalString(const QJsonObject &object, const QString &key, const QString &location) {
    const auto value = object.value(key);
    if (value.isNull() || value.isUndefined()) {
        return {};
    }
    if (!value.isString() || value.toString().isEmpty()) {
        throw ContractError(location + "." + key + " must be a non-empty string or null");
    }
    return value.toString();
}

QStringList optionalArguments(const QJsonObject &object, const QString &location) {
    const auto value = object.value(QStringLiteral("arguments"));
    if (value.isNull() || value.isUndefined()) {
        return {};
    }
    if (!value.isArray() || value.toArray().isEmpty()) {
        throw ContractError(location + ".arguments must be a non-empty string array or null");
    }
    QStringList arguments;
    const auto values = value.toArray();
    arguments.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        if (!values.at(index).isString()) {
            throw ContractError(location + ".arguments[" + QString::number(index) +
                                "] must be a string");
        }
        arguments.append(values.at(index).toString());
    }
    return arguments;
}

SnapshotEntry parseEntry(const QJsonValue &value, qsizetype index) {
    const auto location = QStringLiteral("entries[") + QString::number(index) + "]";
    if (!value.isObject()) {
        throw ContractError(location + " must be an object");
    }
    const auto object = value.toObject();
    SnapshotEntry entry;
    entry.file = requiredString(object, QStringLiteral("file"), location);
    entry.directory = requiredString(object, QStringLiteral("directory"), location);
    entry.arguments = optionalArguments(object, location);
    entry.command = optionalString(object, QStringLiteral("command"), location);
    entry.output = optionalString(object, QStringLiteral("output"), location);
    if (entry.arguments.isEmpty() == entry.command.isEmpty()) {
        throw ContractError(location + " must contain exactly one of arguments or command");
    }
    return entry;
}

Snapshot parseSnapshotDocument(const QJsonDocument &document) {
    if (!document.isObject()) {
        throw ContractError("snapshot root must be an object");
    }
    const auto root = document.object();
    Snapshot snapshot;
    snapshot.schemaVersion = requiredString(root, QStringLiteral("schema_version"), "root");
    if (snapshot.schemaVersion != QString::fromLatin1(kSnapshotSchema)) {
        throw ContractError("root.schema_version is unsupported: " + snapshot.schemaVersion);
    }

    const auto producer = root.value(QStringLiteral("producer"));
    if (!producer.isObject()) {
        throw ContractError("root.producer must be an object");
    }
    const auto producerName =
        requiredString(producer.toObject(), QStringLiteral("name"), "root.producer");
    if (producerName != QStringLiteral("buildscope")) {
        throw ContractError("root.producer.name is unsupported: " + producerName);
    }
    snapshot.producerVersion =
        requiredString(producer.toObject(), QStringLiteral("version"), "root.producer");

    const auto source = root.value(QStringLiteral("source"));
    if (!source.isObject()) {
        throw ContractError("root.source must be an object");
    }
    const auto sourceObject = source.toObject();
    snapshot.sourcePath = requiredString(sourceObject, QStringLiteral("path"), "root.source");

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
    snapshot.entries.reserve(entryArray.size());
    for (qsizetype index = 0; index < entryArray.size(); ++index) {
        snapshot.entries.append(parseEntry(entryArray.at(index), index));
    }
    return snapshot;
}

}  // namespace

ContractError::ContractError(const QString &message) : std::runtime_error(message.toStdString()) {}

Snapshot loadSnapshotFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw ContractError("cannot open snapshot: " + file.errorString());
    }
    if (!QFileInfo(file).isFile()) {
        throw ContractError("snapshot must be a regular file");
    }
    if (file.size() > kMaxSnapshotBytes) {
        throw ContractError("snapshot exceeds 67108864 byte limit");
    }
    const auto payload = file.read(kMaxSnapshotBytes + 1);
    if (payload.size() > kMaxSnapshotBytes) {
        throw ContractError("snapshot exceeds 67108864 byte limit");
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        throw ContractError("invalid JSON at byte " + QString::number(parseError.offset) + ": " +
                            parseError.errorString());
    }
    return parseSnapshotDocument(document);
}

QString invocationText(const SnapshotEntry &entry) {
    if (!entry.arguments.isEmpty()) {
        return entry.arguments.join(QLatin1Char(' '));
    }
    return entry.command;
}

}  // namespace buildscope
