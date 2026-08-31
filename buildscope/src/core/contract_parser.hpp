#pragma once

#include "buildscope/contract.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace buildscope::detail {

struct ParsedRawEntry {
    QString file;
    QString directory;
    QStringList arguments;
    QString command;
    QString output;
    bool hasArguments = false;
    bool hasCommand = false;
};

void rejectUnknownKeys(const QJsonObject &object, const QStringList &allowed,
                       const QString &location);
QString requiredString(const QJsonObject &object, const QString &key, const QString &location);
QString stringValue(const QJsonObject &object, const QString &key, const QString &location);
QString requiredEnumString(const QJsonObject &object, const QString &key, const QString &location,
                           const QStringList &allowed);
QString optionalString(const QJsonObject &object, const QString &key, const QString &location);
QStringList optionalArguments(const QJsonObject &object, const QString &location,
                              bool v2);
bool isPresent(const QJsonValue &value);
bool requiredBool(const QJsonObject &object, const QString &key, const QString &location);
std::optional<bool> requiredNullableBool(const QJsonObject &object, const QString &key,
                                         const QString &location);
qsizetype requiredInteger(const QJsonObject &object, const QString &key,
                          const QString &location);
QStringList requiredStringArray(const QJsonObject &object, const QString &key,
                                const QString &location, bool nonEmpty,
                                bool allowEmptyNonCompiler = false);
ParsedRawEntry parseRawEntry(const QJsonValue &value, qsizetype index, bool v2);
SnapshotEntry parseV2Entry(const QJsonValue &value, qsizetype index);
Snapshot parseSnapshotDocument(const QJsonDocument &document);

}  // namespace buildscope::detail
