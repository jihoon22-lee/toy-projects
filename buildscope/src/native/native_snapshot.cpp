#include "native_snapshot.hpp"

#include "canonical_json.hpp"
#include "native_error.hpp"
#include "native_io.hpp"
#include "native_normalize.hpp"
#include "native_paths.hpp"

#include "../core/contract_json_guard.hpp"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace buildscope::native {
namespace {

const QString kSchemaV1 = QStringLiteral("buildscope.snapshot/v1");
const QString kSchemaV2 = QStringLiteral("buildscope.snapshot/v2");
const QString kSchemaV3 = QStringLiteral("buildscope.snapshot/v3");
const QString kProducerVersion = QStringLiteral(BUILDSCOPE_VERSION);

QString requiredString(const QJsonObject &entry, const QString &key, qsizetype index) {
    const QJsonValue value = entry.value(key);
    if (!value.isString() || value.toString().isEmpty() ||
        value.toString().contains(QLatin1Char('\0')) ||
        value.toString().size() > kMaxFieldChars) {
        throw SnapshotError(QStringLiteral("entry[%1].%2 must be a non-empty string")
                                .arg(index)
                                .arg(key));
    }
    return value.toString();
}

std::optional<QString> outputValue(const QJsonObject &entry, qsizetype index) {
    const QJsonValue value = entry.value(QStringLiteral("output"));
    if (value.isNull() || value.isUndefined()) {
        return std::nullopt;
    }
    if (!value.isString() || value.toString().isEmpty() ||
        value.toString().contains(QLatin1Char('\0')) ||
        value.toString().size() > kMaxFieldChars) {
        throw SnapshotError(
            QStringLiteral("entry[%1].output must be a non-empty string").arg(index));
    }
    return value.toString();
}

QJsonObject snapshotEntry(const QJsonValue &value, qsizetype index,
                          const QString &projectRoot, const QString &databaseParent) {
    if (!value.isObject()) {
        throw SnapshotError(QStringLiteral("entry[%1] must be an object").arg(index));
    }
    const QJsonObject raw = value.toObject();
    QJsonObject validated{
        {QStringLiteral("arguments"), raw.value(QStringLiteral("arguments"))},
        {QStringLiteral("command"), raw.value(QStringLiteral("command"))},
        {QStringLiteral("directory"), requiredString(raw, QStringLiteral("directory"), index)},
        {QStringLiteral("file"), requiredString(raw, QStringLiteral("file"), index)},
    };
    const auto output = outputValue(raw, index);
    if (output.has_value()) {
        validated.insert(QStringLiteral("output"), output.value());
    } else {
        validated.insert(QStringLiteral("output"), QJsonValue::Null);
    }
    try {
        return normalizeEntry(validated, index, projectRoot, databaseParent);
    } catch (const NativeError &error) {
        throw SnapshotError(QString::fromStdString(error.what()));
    }
}

QString projectRootPath(const QString &path, const QString &projectRoot) {
    if (projectRoot.isEmpty()) {
        return QFileInfo(path).absolutePath();
    }
    if (projectRoot.contains(QLatin1Char('\0')) || projectRoot.size() > kMaxFieldChars) {
        throw SnapshotError(QStringLiteral("project root must be a non-empty bounded path"));
    }
    if (looksWindowsPath(projectRoot)) {
        return normalizeLexical(projectRoot, projectRoot, QStringLiteral("windows"));
    }
    std::error_code error;
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(projectRoot.toStdString(), error);
    if (error) {
        throw SnapshotError(
            QStringLiteral("cannot resolve compilation database paths: %1")
                .arg(QString::fromStdString(error.message())));
    }
    return QString::fromStdString(resolved.generic_string());
}

// QJsonValue rejects the Python-legal constants NaN/Infinity/-Infinity
// with a generic parse error; mirror the producer's explicit rejection.
void rejectNonstandardConstants(const QByteArray &bytes) {
    bool inString = false;
    bool escaped = false;
    for (qsizetype index = 0; index < bytes.size(); ++index) {
        const char character = bytes.at(index);
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }
        if (character == '"') {
            inString = true;
            continue;
        }
        for (const char *constant : {"NaN", "Infinity", "-Infinity"}) {
            const qsizetype length = static_cast<qsizetype>(std::strlen(constant));
            if (bytes.mid(index, length) == constant &&
                (index == 0 || bytes.at(index - 1) != '"')) {
                throw SnapshotError(
                    QStringLiteral("non-standard JSON constant is forbidden: %1")
                        .arg(QLatin1String(constant)));
            }
        }
    }
}

// Splits the top-level JSON array into element byte ranges without building
// a document. Strings, escapes, and bracket depth are tracked exactly, so a
// `,`/`]` inside an element never splits it. Each range is handed to `visit`;
// malformed structure fails closed with a SnapshotError.
template <typename Visit>
void forEachDatabaseElement(const QByteArray &raw, Visit &&visit) {
    auto isSpace = [](char character) {
        return character == ' ' || character == '\t' || character == '\n' ||
               character == '\r';
    };
    qsizetype cursor = 0;
    while (cursor < raw.size() && isSpace(raw.at(cursor))) {
        ++cursor;
    }
    if (cursor >= raw.size() || raw.at(cursor) != '[') {
        throw SnapshotError(
            QStringLiteral("compilation database root must be an array"));
    }
    ++cursor;
    bool afterComma = false;
    for (;;) {
        while (cursor < raw.size() && isSpace(raw.at(cursor))) {
            ++cursor;
        }
        if (cursor >= raw.size()) {
            throw SnapshotError(QStringLiteral(
                "cannot read compilation database: unexpected end of input"));
        }
        if (raw.at(cursor) == ']') {
            if (afterComma) {
                throw SnapshotError(QStringLiteral(
                    "cannot read compilation database: trailing comma in entry array"));
            }
            ++cursor;
            break;
        }
        const qsizetype begin = cursor;
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        bool closed = false;
        for (; cursor < raw.size(); ++cursor) {
            const char character = raw.at(cursor);
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (character == '\\') {
                    escaped = true;
                } else if (character == '"') {
                    inString = false;
                }
                continue;
            }
            if (character == '"') {
                inString = true;
                continue;
            }
            if (character == '{' || character == '[') {
                ++depth;
                continue;
            }
            if (character == ']' && depth == 0) {
                closed = true;
                break;
            }
            if (character == '}' || character == ']') {
                if (depth == 0) {
                    throw SnapshotError(QStringLiteral(
                        "cannot read compilation database: unbalanced brackets"));
                }
                --depth;
                continue;
            }
            if (character == ',' && depth == 0) {
                closed = true;
                break;
            }
        }
        if (!closed) {
            throw SnapshotError(QStringLiteral(
                "cannot read compilation database: unexpected end of input"));
        }
        visit(raw.mid(begin, cursor - begin));
        if (raw.at(cursor) == ',') {
            ++cursor;
            afterComma = true;
        } else {
            ++cursor;  // closing ']'
            break;
        }
    }
    while (cursor < raw.size() && isSpace(raw.at(cursor))) {
        ++cursor;
    }
    if (cursor != raw.size()) {
        throw SnapshotError(QStringLiteral(
            "cannot read compilation database: trailing data after entry array"));
    }
}

}  // namespace

QJsonObject loadCompilationDatabase(const QString &path, const QString &projectRoot) {
    QByteArray raw;
    QString resolvedPath;
    try {
        resolvedPath = QFileInfo(path).absoluteFilePath();
        raw = readBoundedRegular(path, kMaxDatabaseBytes);
    } catch (const SnapshotIoError &error) {
        throw SnapshotError(QStringLiteral("cannot read compilation database: %1")
                                .arg(QString::fromStdString(error.what())));
    }
    QString root;
    try {
        root = projectRootPath(resolvedPath, projectRoot);
    } catch (const NativeError &error) {
        throw SnapshotError(QStringLiteral("cannot resolve compilation database paths: %1")
                                .arg(QString::fromStdString(error.what())));
    }
    const QString databaseParent = QFileInfo(resolvedPath).absolutePath();
    QJsonArray entries;
    qsizetype index = 0;
    // Walks the top-level array element by element so a large compilation
    // database never materializes one giant DOM: each element is validated
    // (duplicate keys, non-standard constants) and parsed inside its own
    // byte range, bounded by the largest single entry instead of the file.
    forEachDatabaseElement(raw, [&](const QByteArray &element) {
        if (index >= kMaxEntries) {
            throw SnapshotError(
                QStringLiteral("compilation database exceeds %1 entry limit")
                    .arg(kMaxEntries));
        }
        try {
            detail::rejectDuplicateJsonKeys(element);
        } catch (const std::exception &error) {
            throw SnapshotError(QString::fromUtf8(error.what()));
        }
        rejectNonstandardConstants(element);
        QJsonParseError parseError{};
        const QJsonValue value = QJsonValue::fromJson(element, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            throw SnapshotError(
                QStringLiteral("cannot read compilation database entry %1: %2")
                    .arg(index)
                    .arg(parseError.errorString()));
        }
        entries.append(snapshotEntry(value, index, root, databaseParent));
        ++index;
    });
    annotateEntrySets(entries);
    QList<QJsonObject> sorted;
    sorted.reserve(entries.size());
    for (const QJsonValue &value : entries) {
        sorted.append(value.toObject());
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const QJsonObject &first, const QJsonObject &second) {
                  const QJsonObject firstNormalized =
                      first.value(QStringLiteral("normalized")).toObject();
                  const QJsonObject secondNormalized =
                      second.value(QStringLiteral("normalized")).toObject();
                  const QString firstSource = firstNormalized.value(QStringLiteral("source"))
                                                .toObject()
                                                .value(QStringLiteral("path"))
                                                .toString();
                  const QString secondSource = secondNormalized.value(QStringLiteral("source"))
                                                   .toObject()
                                                   .value(QStringLiteral("path"))
                                                   .toString();
                  if (firstSource != secondSource) {
                      return firstSource < secondSource;
                  }
                  const QString firstConfiguration =
                      firstNormalized.value(QStringLiteral("configuration")).toString();
                  const QString secondConfiguration =
                      secondNormalized.value(QStringLiteral("configuration")).toString();
                  if (firstConfiguration != secondConfiguration) {
                      return firstConfiguration < secondConfiguration;
                  }
                  return first.value(QStringLiteral("state"))
                             .toObject()
                             .value(QStringLiteral("entry_index"))
                             .toInt() <
                         second.value(QStringLiteral("state"))
                             .toObject()
                             .value(QStringLiteral("entry_index"))
                             .toInt();
              });
    QJsonArray entryValues;
    for (const QJsonObject &entry : sorted) {
        entryValues.append(entry);
    }
    return QJsonObject{
        {QStringLiteral("entries"), entryValues},
        {QStringLiteral("producer"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("buildscope")},
                     {QStringLiteral("version"), kProducerVersion}}},
        {QStringLiteral("schema_version"), kSchemaV2},
        {QStringLiteral("source"),
         QJsonObject{{QStringLiteral("entry_count"), static_cast<double>(sorted.size())},
                     {QStringLiteral("path"), resolvedPath},
                     {QStringLiteral("project_root"), root}}},
    };
}

QString dumpsSnapshot(const QJsonObject &snapshot, bool pretty) {
    const QString rendered = dumpsJson(snapshot, pretty) + QLatin1Char('\n');
    if (rendered.toUtf8().size() > kMaxSnapshotBytes) {
        throw SnapshotError(QStringLiteral("snapshot exceeds %1 byte limit")
                                .arg(kMaxSnapshotBytes));
    }
    return rendered;
}

QJsonObject snapshotForSchema(QJsonObject snapshot, const QString &schema) {
    if (schema == QLatin1String("v3")) {
        if (snapshot.value(QStringLiteral("schema_version")).toString() != kSchemaV3) {
            throw SnapshotError(QStringLiteral("v3 snapshots require include analysis"));
        }
        return snapshot;
    }
    if (schema == QLatin1String("v2")) {
        if (snapshot.value(QStringLiteral("schema_version")).toString() == kSchemaV3) {
            QJsonArray entries = snapshot.value(QStringLiteral("entries")).toArray();
            for (qsizetype index = 0; index < entries.size(); ++index) {
                QJsonObject entry = entries.at(index).toObject();
                entry.remove(QStringLiteral("include_analysis"));
                entries.replace(index, entry);
            }
            snapshot.insert(QStringLiteral("entries"), entries);
            snapshot.insert(QStringLiteral("schema_version"), kSchemaV2);
        }
        return snapshot;
    }
    if (schema != QLatin1String("v1")) {
        throw SnapshotError(QStringLiteral("unsupported snapshot schema: %1").arg(schema));
    }
    QJsonArray entries;
    for (const QJsonValue &value :
         snapshot.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QJsonValue arguments = entry.value(QStringLiteral("arguments"));
        entries.append(QJsonObject{
            {QStringLiteral("arguments"), arguments},
            {QStringLiteral("command"),
             arguments.isNull() ? entry.value(QStringLiteral("command"))
                                : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("directory"), entry.value(QStringLiteral("directory"))},
            {QStringLiteral("file"), entry.value(QStringLiteral("file"))},
            {QStringLiteral("output"), entry.value(QStringLiteral("output"))},
        });
    }
    const QJsonObject source = snapshot.value(QStringLiteral("source")).toObject();
    return QJsonObject{
        {QStringLiteral("entries"), entries},
        {QStringLiteral("producer"), snapshot.value(QStringLiteral("producer"))},
        {QStringLiteral("schema_version"), kSchemaV1},
        {QStringLiteral("source"),
         QJsonObject{
             {QStringLiteral("entry_count"), entries.size()},
             {QStringLiteral("path"), source.value(QStringLiteral("path"))},
         }},
    };
}

}  // namespace buildscope::native
