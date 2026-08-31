#include "contract_parser.hpp"

#include <QJsonArray>
#include <QJsonObject>

namespace buildscope::detail {
namespace {

constexpr qsizetype kMaxArguments = 32768;
constexpr qsizetype kMaxDiagnostics = kMaxArguments + 4;
constexpr qint64 kMaxFieldChars = 1024LL * 1024LL;

bool isLowerHex(const QChar character) {
    return (character >= QLatin1Char('0') && character <= QLatin1Char('9')) ||
           (character >= QLatin1Char('a') && character <= QLatin1Char('f'));
}

bool isConfigurationDigest(const QString &value) {
    constexpr auto kDigestPrefix = "sha256:";
    constexpr qsizetype kDigestPrefixLength = 7;
    constexpr qsizetype kDigestLength = 64;
    if (!value.startsWith(QLatin1String(kDigestPrefix)) ||
        value.size() != kDigestPrefixLength + kDigestLength) {
        return false;
    }
    for (qsizetype index = kDigestPrefixLength; index < value.size(); ++index) {
        if (!isLowerHex(value.at(index))) {
            return false;
        }
    }
    return true;
}

bool isAsciiLetter(const QChar character) {
    return (character >= QLatin1Char('A') && character <= QLatin1Char('Z')) ||
           (character >= QLatin1Char('a') && character <= QLatin1Char('z'));
}

bool isAsciiDigit(const QChar character) {
    return character >= QLatin1Char('0') && character <= QLatin1Char('9');
}

bool isDefineStart(const QChar character) {
    return isAsciiLetter(character) || character == QLatin1Char('_');
}

bool isDefineContinuation(const QChar character) {
    return isDefineStart(character) || isAsciiDigit(character);
}

bool isDefineName(const QString &value) {
    if (value.isEmpty()) {
        return false;
    }
    const auto first = value.front();
    if (!isDefineStart(first)) {
        return false;
    }
    for (qsizetype index = 1; index < value.size(); ++index) {
        if (!isDefineContinuation(value.at(index))) {
            return false;
        }
    }
    return true;
}

QJsonObject requiredObject(const QJsonValue &value, const QString &location) {
    if (!value.isObject()) {
        throw ContractError(location + " must be an object");
    }
    return value.toObject();
}

template <typename Record, typename Parser>
QVector<Record> parseObjectArray(const QJsonValue &value, const QString &location,
                                 qsizetype limit, Parser parser) {
    if (!value.isArray()) {
        throw ContractError(location + " must be an array");
    }
    const auto values = value.toArray();
    if (values.size() > limit) {
        throw ContractError(location + " exceeds the item limit");
    }
    QVector<Record> records;
    records.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        const auto itemLocation = location + "[" + QString::number(index) + "]";
        records.append(parser(requiredObject(values.at(index), itemLocation), itemLocation,
                              index));
    }
    return records;
}

SnapshotPath parsePathRecord(const QJsonValue &value, const QString &location) {
    const auto object = requiredObject(value, location);
    rejectUnknownKeys(object,
                      {QStringLiteral("exists"), QStringLiteral("path"),
                       QStringLiteral("scope"), QStringLiteral("style")},
                      location);
    SnapshotPath path;
    path.path = requiredString(object, QStringLiteral("path"), location);
    path.scope = requiredEnumString(object, QStringLiteral("scope"), location,
                                    {QStringLiteral("project"), QStringLiteral("vendor"),
                                     QStringLiteral("system")});
    path.style = requiredEnumString(object, QStringLiteral("style"), location,
                                    {QStringLiteral("posix"), QStringLiteral("windows")});
    path.exists = requiredNullableBool(object, QStringLiteral("exists"), location);
    return path;
}

SnapshotCompiler parseCompiler(const QJsonValue &value, const QString &location) {
    const auto object = requiredObject(value, location);
    rejectUnknownKeys(object,
                      {QStringLiteral("family"), QStringLiteral("name"),
                       QStringLiteral("path"), QStringLiteral("wrappers")},
                      location);
    SnapshotCompiler compiler;
    compiler.family = requiredEnumString(
        object, QStringLiteral("family"), location,
        {QStringLiteral("clang"), QStringLiteral("clang-cl"), QStringLiteral("emscripten"),
         QStringLiteral("gcc"), QStringLiteral("msvc"), QStringLiteral("unknown")});
    compiler.name = requiredString(object, QStringLiteral("name"), location);
    compiler.path = requiredString(object, QStringLiteral("path"), location);
    compiler.wrappers = requiredStringArray(object, QStringLiteral("wrappers"), location, false);
    return compiler;
}

QVector<SnapshotDefine> parseDefines(const QJsonValue &value, const QString &location) {
    return parseObjectArray<SnapshotDefine>(
        value, location, kMaxArguments,
        [](const QJsonObject &object, const QString &itemLocation, qsizetype) {
            rejectUnknownKeys(object,
                              {QStringLiteral("action"), QStringLiteral("name"),
                               QStringLiteral("value")},
                              itemLocation);
            SnapshotDefine define;
            define.action = requiredEnumString(
                object, QStringLiteral("action"), itemLocation,
                {QStringLiteral("define"), QStringLiteral("undefine")});
            define.name = requiredString(object, QStringLiteral("name"), itemLocation);
            if (!isDefineName(define.name)) {
                throw ContractError(itemLocation + ".name is not a valid definition name");
            }
            const auto rawValue = object.value(QStringLiteral("value"));
            if (rawValue.isUndefined() || (!rawValue.isNull() && !rawValue.isString())) {
                throw ContractError(itemLocation + ".value must be a string or null");
            }
            if (rawValue.isString()) {
                if (rawValue.toString().contains(QChar::Null) ||
                    rawValue.toString().size() > kMaxFieldChars) {
                    throw ContractError(itemLocation + ".value must be a string or null");
                }
                define.value = rawValue.toString();
            }
            return define;
        });
}

QVector<SnapshotIncludePath> parseIncludePaths(const QJsonValue &value, const QString &location) {
    return parseObjectArray<SnapshotIncludePath>(
        value, location, kMaxArguments,
        [](const QJsonObject &object, const QString &itemLocation, qsizetype index) {
            rejectUnknownKeys(object,
                              {QStringLiteral("exists"), QStringLiteral("kind"),
                               QStringLiteral("order"), QStringLiteral("path"),
                               QStringLiteral("scope"), QStringLiteral("style")},
                              itemLocation);
            SnapshotIncludePath include;
            include.path = requiredString(object, QStringLiteral("path"), itemLocation);
            include.scope = requiredEnumString(
                object, QStringLiteral("scope"), itemLocation,
                {QStringLiteral("project"), QStringLiteral("vendor"),
                 QStringLiteral("system")});
            include.style = requiredEnumString(
                object, QStringLiteral("style"), itemLocation,
                {QStringLiteral("posix"), QStringLiteral("windows")});
            include.exists = requiredNullableBool(object, QStringLiteral("exists"), itemLocation);
            include.kind = requiredEnumString(
                object, QStringLiteral("kind"), itemLocation,
                {QStringLiteral("after"), QStringLiteral("framework"),
                 QStringLiteral("include"), QStringLiteral("quote"),
                 QStringLiteral("system")});
            include.order = requiredInteger(object, QStringLiteral("order"), itemLocation);
            if (include.order != index) {
                throw ContractError(itemLocation + ".order must match include path order");
            }
            return include;
        });
}

SnapshotTarget parseTarget(const QJsonValue &value, const QString &location) {
    const auto object = requiredObject(value, location);
    rejectUnknownKeys(object, {QStringLiteral("build_target"), QStringLiteral("triple")}, location);
    SnapshotTarget target;
    target.buildTarget = stringValue(object, QStringLiteral("build_target"), location);
    target.triple = stringValue(object, QStringLiteral("triple"), location);
    return target;
}

SnapshotNormalized parseNormalized(const QJsonValue &value, const QString &location) {
    const auto object = requiredObject(value, location);
    rejectUnknownKeys(object,
                      {QStringLiteral("argv"), QStringLiteral("command_style"),
                       QStringLiteral("compiler"), QStringLiteral("configuration"),
                       QStringLiteral("defines"), QStringLiteral("directory"),
                       QStringLiteral("include_paths"), QStringLiteral("invocation_source"),
                       QStringLiteral("language"), QStringLiteral("output"),
                       QStringLiteral("source"), QStringLiteral("standard"),
                       QStringLiteral("sysroot"), QStringLiteral("target")},
                      location);
    SnapshotNormalized normalized;
    normalized.argv = requiredStringArray(object, QStringLiteral("argv"), location, true, true);
    normalized.commandStyle = requiredEnumString(object, QStringLiteral("command_style"), location,
                                                 {QStringLiteral("posix"),
                                                  QStringLiteral("windows")});
    normalized.invocationSource =
        requiredEnumString(object, QStringLiteral("invocation_source"), location,
                           {QStringLiteral("arguments"), QStringLiteral("command")});
    normalized.compiler = parseCompiler(object.value(QStringLiteral("compiler")),
                                        location + ".compiler");
    normalized.configuration = requiredString(object, QStringLiteral("configuration"), location);
    if (!isConfigurationDigest(normalized.configuration)) {
        throw ContractError(location + ".configuration must be a sha256 digest");
    }
    normalized.defines = parseDefines(object.value(QStringLiteral("defines")),
                                      location + ".defines");
    normalized.directory = parsePathRecord(object.value(QStringLiteral("directory")),
                                           location + ".directory");
    normalized.includePaths = parseIncludePaths(object.value(QStringLiteral("include_paths")),
                                               location + ".include_paths");
    normalized.language = stringValue(object, QStringLiteral("language"), location);
    if (!QStringList{QStringLiteral(""), QStringLiteral("c"), QStringLiteral("c++"),
                     QStringLiteral("objective-c"), QStringLiteral("objective-c++")}
             .contains(normalized.language)) {
        throw ContractError(location + ".language is unsupported: " + normalized.language);
    }
    const auto output = object.value(QStringLiteral("output"));
    if (output.isUndefined() || (!output.isNull() && !output.isObject())) {
        throw ContractError(location + ".output must be an object or null");
    }
    if (output.isObject()) {
        normalized.output = parsePathRecord(output, location + ".output");
    }
    normalized.source = parsePathRecord(object.value(QStringLiteral("source")),
                                        location + ".source");
    normalized.standard = stringValue(object, QStringLiteral("standard"), location);
    const auto sysroot = object.value(QStringLiteral("sysroot"));
    if (sysroot.isUndefined() || (!sysroot.isNull() && !sysroot.isObject())) {
        throw ContractError(location + ".sysroot must be an object or null");
    }
    if (sysroot.isObject()) {
        normalized.sysroot = parsePathRecord(sysroot, location + ".sysroot");
    }
    normalized.target = parseTarget(object.value(QStringLiteral("target")),
                                    location + ".target");
    return normalized;
}

SnapshotState parseState(const QJsonValue &value, const QString &location) {
    const auto object = requiredObject(value, location);
    rejectUnknownKeys(object,
                      {QStringLiteral("duplicate"), QStringLiteral("entry_index"),
                       QStringLiteral("source_configuration_count"),
                       QStringLiteral("source_status")},
                      location);
    SnapshotState state;
    state.duplicate = requiredBool(object, QStringLiteral("duplicate"), location);
    state.entryIndex = requiredInteger(object, QStringLiteral("entry_index"), location);
    state.sourceConfigurationCount =
        requiredInteger(object, QStringLiteral("source_configuration_count"), location);
    if (state.sourceConfigurationCount == 0) {
        throw ContractError(location + ".source_configuration_count must be positive");
    }
    state.sourceStatus = requiredEnumString(
        object, QStringLiteral("source_status"), location,
        {QStringLiteral("missing"), QStringLiteral("present"), QStringLiteral("stale"),
         QStringLiteral("unknown")});
    return state;
}

QVector<SnapshotDiagnostic> parseDiagnostics(const QJsonValue &value, const QString &location) {
    return parseObjectArray<SnapshotDiagnostic>(
        value, location, kMaxDiagnostics,
        [](const QJsonObject &object, const QString &itemLocation, qsizetype) {
            rejectUnknownKeys(object,
                              {QStringLiteral("code"), QStringLiteral("message"),
                               QStringLiteral("severity")},
                              itemLocation);
            SnapshotDiagnostic diagnostic;
            diagnostic.code = requiredString(object, QStringLiteral("code"), itemLocation);
            diagnostic.message = requiredString(object, QStringLiteral("message"), itemLocation);
            diagnostic.severity = requiredEnumString(
                object, QStringLiteral("severity"), itemLocation,
                {QStringLiteral("info"), QStringLiteral("warning"), QStringLiteral("error")});
            return diagnostic;
        });
}

}  // namespace

SnapshotEntry parseV2Entry(const QJsonValue &value, qsizetype index) {
    const auto raw = parseRawEntry(value, index, true);
    const auto object = value.toObject();
    SnapshotEntry entry;
    entry.file = raw.file;
    entry.directory = raw.directory;
    entry.arguments = raw.arguments;
    entry.command = raw.command;
    entry.output = raw.output;
    entry.normalized = parseNormalized(object.value(QStringLiteral("normalized")),
                                       QStringLiteral("entries[") + QString::number(index) +
                                           "].normalized");
    entry.state = parseState(object.value(QStringLiteral("state")),
                             QStringLiteral("entries[") + QString::number(index) + "].state");
    entry.diagnostics = parseDiagnostics(
        object.value(QStringLiteral("diagnostics")),
        QStringLiteral("entries[") + QString::number(index) + "].diagnostics");
    const auto expectedSource = raw.hasArguments ? QStringLiteral("arguments")
                                                 : QStringLiteral("command");
    if (entry.normalized.invocationSource != expectedSource) {
        throw ContractError(QStringLiteral("entries[") + QString::number(index) +
                            "].normalized.invocation_source does not match the raw invocation form");
    }
    if (raw.hasArguments && entry.normalized.argv != raw.arguments) {
        throw ContractError(QStringLiteral("entries[") + QString::number(index) +
                            "].normalized.argv must match raw arguments");
    }
    entry.hasNormalized = true;
    entry.hasState = true;
    return entry;
}

}  // namespace buildscope::detail
