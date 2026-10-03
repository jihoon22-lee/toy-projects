#include "native_command.hpp"

#include "native_error.hpp"
#include "native_paths.hpp"

#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <numeric>

namespace buildscope::native {
namespace {

const QSet<QString> kCompilerWrappers = {QStringLiteral("ccache"), QStringLiteral("distcc"),
                                         QStringLiteral("icecc"), QStringLiteral("sccache")};
const QSet<QString> kEnvOptionsWithValue = {QStringLiteral("-u"), QStringLiteral("--unset")};
const QRegularExpression kAssignment(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*=.*$"));
const QRegularExpression kVersionSuffix(QStringLiteral("(?:[-.]\\d+(?:\\.\\d+)*)$"));
const QRegularExpression kWindowsCompiler(
    QStringLiteral("(?:^|\\s)(?:cl|clang-cl)(?:\\.exe)?(?:\\s|$)"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kQuotedWindowsAnchor(
    QStringLiteral("^\"?(?:[A-Za-z]:[\\\\/]|\\\\\\\\)"));

const QMap<QString, QString> kCompilerFamilies = {
    {QStringLiteral("c89"), QStringLiteral("gcc")},
    {QStringLiteral("c99"), QStringLiteral("gcc")},
    {QStringLiteral("cc"), QStringLiteral("gcc")},
    {QStringLiteral("c++"), QStringLiteral("gcc")},
    {QStringLiteral("gcc"), QStringLiteral("gcc")},
    {QStringLiteral("g++"), QStringLiteral("gcc")},
    {QStringLiteral("clang"), QStringLiteral("clang")},
    {QStringLiteral("clang++"), QStringLiteral("clang")},
    {QStringLiteral("clang-cl"), QStringLiteral("clang-cl")},
    {QStringLiteral("cl"), QStringLiteral("msvc")},
    {QStringLiteral("emcc"), QStringLiteral("emscripten")},
    {QStringLiteral("em++"), QStringLiteral("emscripten")},
};

QString baseName(const QString &token) {
    QString replaced = token;
    replaced.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return replaced.mid(replaced.lastIndexOf(QLatin1Char('/')) + 1);
}

// _windows_quote + _windows_argument from _command.py: Microsoft C runtime
// argv parsing.
std::pair<qsizetype, bool> windowsQuote(const QString &command, qsizetype index,
                                        qsizetype slashes, bool quoted, QString &value) {
    for (qsizetype count = 0; count < slashes / 2; ++count) {
        value += QLatin1Char('\\');
    }
    if (slashes % 2) {
        value += QLatin1Char('"');
    } else if (quoted && index + 1 < command.size() &&
               command.at(index + 1) == QLatin1Char('"')) {
        value += QLatin1Char('"');
        index += 1;
    } else {
        quoted = !quoted;
    }
    return {index + 1, quoted};
}

std::pair<QString, qsizetype> windowsArgument(const QString &command, qsizetype index) {
    QString value;
    bool quoted = false;
    while (index < command.size() &&
           (quoted || (command.at(index) != QLatin1Char(' ') &&
                       command.at(index) != QLatin1Char('\t')))) {
        qsizetype slashes = 0;
        while (index < command.size() && command.at(index) == QLatin1Char('\\')) {
            ++slashes;
            ++index;
        }
        if (index < command.size() && command.at(index) == QLatin1Char('"')) {
            const auto result = windowsQuote(command, index, slashes, quoted, value);
            index = result.first;
            quoted = result.second;
            continue;
        }
        for (qsizetype count = 0; count < slashes; ++count) {
            value += QLatin1Char('\\');
        }
        if (index < command.size() &&
            (quoted || (command.at(index) != QLatin1Char(' ') &&
                        command.at(index) != QLatin1Char('\t')))) {
            value += command.at(index);
            ++index;
        }
    }
    if (quoted) {
        throw CommandError(QStringLiteral("command contains an unclosed Windows quote"));
    }
    return {value, index};
}

int envPrefix(const QStringList &argv, QStringList &wrappers) {
    if (argv.isEmpty() || programName(argv.at(0)) != QLatin1String("env")) {
        return 0;
    }
    wrappers.append(baseName(argv.at(0)));
    int index = 1;
    while (index < argv.size()) {
        const QString &token = argv.at(index);
        if (kEnvOptionsWithValue.contains(token)) {
            index += 2;
            continue;
        }
        if (token.startsWith(QLatin1String("--unset=")) ||
            token == QLatin1String("-i") ||
            token == QLatin1String("--ignore-environment") ||
            kAssignment.match(token).hasMatch()) {
            index += 1;
            continue;
        }
        break;
    }
    return index;
}

QString compilerFamily(const QString &stem) {
    QString unversioned = stem;
    unversioned.remove(kVersionSuffix);
    const auto mapped = kCompilerFamilies.constFind(unversioned);
    if (mapped != kCompilerFamilies.constEnd()) {
        return mapped.value();
    }
    if (unversioned.endsWith(QLatin1String("clang-cl"))) {
        return QStringLiteral("clang-cl");
    }
    if (unversioned.endsWith(QLatin1String("clang++")) ||
        unversioned.endsWith(QLatin1String("clang"))) {
        return QStringLiteral("clang");
    }
    if (unversioned.endsWith(QLatin1String("g++")) ||
        unversioned.endsWith(QLatin1String("gcc")) ||
        unversioned.endsWith(QLatin1String("c++")) ||
        unversioned.endsWith(QLatin1String("cc"))) {
        return QStringLiteral("gcc");
    }
    if (unversioned.endsWith(QLatin1String("em++")) ||
        unversioned.endsWith(QLatin1String("emcc"))) {
        return QStringLiteral("emscripten");
    }
    return QStringLiteral("unknown");
}

QStringList argumentsValue(const QJsonObject &entry, qsizetype index, bool &present) {
    const QJsonValue value = entry.value(QStringLiteral("arguments"));
    present = false;
    if (value.isNull() || value.isUndefined()) {
        return {};
    }
    if (!value.isArray()) {
        throw CommandError(
            QStringLiteral("entry[%1].arguments exceeds the bounded argv contract").arg(index));
    }
    const QJsonArray array = value.toArray();
    if (array.isEmpty() || array.size() > kMaxArguments) {
        throw CommandError(
            QStringLiteral("entry[%1].arguments exceeds the bounded argv contract").arg(index));
    }
    QStringList result;
    qsizetype characters = 0;
    for (const QJsonValue &item : array) {
        if (!item.isString() || item.toString().contains(QLatin1Char('\0'))) {
            throw CommandError(
                QStringLiteral("entry[%1].arguments must be a bounded string array")
                    .arg(index));
        }
        result.append(item.toString());
        characters += item.toString().size();
    }
    if (result.first().isEmpty()) {
        throw CommandError(QStringLiteral("entry[%1].arguments must name a compiler").arg(index));
    }
    if (characters > kMaxArgumentChars) {
        throw CommandError(
            QStringLiteral("entry[%1].arguments exceeds the character limit").arg(index));
    }
    present = true;
    return result;
}

std::optional<QString> commandValue(const QJsonObject &entry, qsizetype index) {
    const QJsonValue value = entry.value(QStringLiteral("command"));
    if (value.isNull() || value.isUndefined()) {
        return std::nullopt;
    }
    if (!value.isString() || value.toString().isEmpty() ||
        value.toString().contains(QLatin1Char('\0'))) {
        throw CommandError(
            QStringLiteral("entry[%1].command must be a non-empty string").arg(index));
    }
    if (value.toString().size() > kMaxCommandChars) {
        throw CommandError(
            QStringLiteral("entry[%1].command exceeds the character limit").arg(index));
    }
    return value.toString();
}

QStringList splitCommand(const QString &command, const QString &style, qsizetype index) {
    QStringList argv;
    try {
        argv = style == QLatin1String("windows") ? splitWindowsCommand(command)
                                                 : splitPosixCommand(command);
    } catch (const NativeError &error) {
        throw CommandError(
            QStringLiteral("entry[%1].command could not be parsed: %2")
                .arg(index)
                .arg(QString::fromStdString(error.what())));
    }
    if (argv.isEmpty() || argv.size() > kMaxArguments || argv.at(0).isEmpty() ||
        std::any_of(argv.cbegin(), argv.cend(), [](const QString &item) {
            return item.contains(QLatin1Char('\0'));
        }) ||
        std::accumulate(argv.cbegin(), argv.cend(), qsizetype(0),
                        [](qsizetype total, const QString &item) {
                            return total + item.size();
                        }) > kMaxArgumentChars) {
        throw CommandError(
            QStringLiteral("entry[%1].command produced an invalid bounded argv").arg(index));
    }
    return argv;
}

}  // namespace

QString programName(const QString &token) {
    QString name = baseName(token).toLower();
    if (name.endsWith(QLatin1String(".exe"))) {
        name.chop(4);
    }
    return name;
}

QStringList splitWindowsCommand(const QString &command) {
    QStringList argv;
    qsizetype index = 0;
    while (index < command.size()) {
        while (index < command.size() && (command.at(index) == QLatin1Char(' ') ||
                                          command.at(index) == QLatin1Char('\t'))) {
            ++index;
        }
        if (index < command.size()) {
            const auto result = windowsArgument(command, index);
            argv.append(result.first);
            index = result.second;
        }
    }
    return argv;
}

// shlex.split(posix=True): whitespace splitting with '..' literals, ".."
// limited escapes, backslash escapes, and '#' comments at token boundaries.
QStringList splitPosixCommand(const QString &command) {
    QStringList argv;
    QString token;
    bool started = false;
    QChar quote;
    for (qsizetype index = 0; index < command.size(); ++index) {
        const QChar character = command.at(index);
        if (!quote.isNull()) {
            if (character == quote) {
                quote = QChar();
            } else if (character == QLatin1Char('\\') && quote == QLatin1Char('"') &&
                       index + 1 < command.size() &&
                       (command.at(index + 1) == QLatin1Char('"') ||
                        command.at(index + 1) == QLatin1Char('\\'))) {
                token += command.at(++index);
            } else {
                token += character;
            }
            continue;
        }
        if (character == QLatin1Char('\\')) {
            if (index + 1 >= command.size()) {
                throw CommandError(QStringLiteral("No escaped character"));
            }
            token += command.at(++index);
            started = true;
            continue;
        }
        if (character == QLatin1Char('\'') || character == QLatin1Char('"')) {
            quote = character;
            started = true;
            continue;
        }
        if (character == QLatin1Char(' ') || character == QLatin1Char('\t') ||
            character == QLatin1Char('\r') || character == QLatin1Char('\n')) {
            if (started) {
                argv.append(token);
                token.clear();
                started = false;
            }
            continue;
        }
        if (character == QLatin1Char('#') && !started) {
            break;
        }
        token += character;
        started = true;
    }
    if (!quote.isNull()) {
        throw CommandError(QStringLiteral("No closing quotation"));
    }
    if (started) {
        argv.append(token);
    }
    return argv;
}

QString commandStyle(const QJsonObject &entry) {
    for (const QString &key : {QStringLiteral("directory"), QStringLiteral("file")}) {
        const QJsonValue value = entry.value(key);
        if (value.isString() && looksWindowsPath(value.toString())) {
            return QStringLiteral("windows");
        }
    }
    const QJsonValue command = entry.value(QStringLiteral("command"));
    if (command.isString()) {
        const QString text = command.toString();
        if (kWindowsCompiler.match(text).hasMatch() ||
            kQuotedWindowsAnchor.match(text.trimmed()).hasMatch()) {
            return QStringLiteral("windows");
        }
    }
    const QJsonValue arguments = entry.value(QStringLiteral("arguments"));
    if (arguments.isArray() && !arguments.toArray().isEmpty()) {
        const QString raw = arguments.toArray().first().toString();
        if (looksWindowsPath(raw)) {
            return QStringLiteral("windows");
        }
        const QString compiler = programName(raw);
        if (compiler == QLatin1String("cl") || compiler == QLatin1String("clang-cl")) {
            return QStringLiteral("windows");
        }
    }
    return QStringLiteral("posix");
}

Invocation parseInvocation(const QJsonObject &entry, qsizetype index) {
    bool hasArguments = false;
    const QStringList arguments = argumentsValue(entry, index, hasArguments);
    const std::optional<QString> command = commandValue(entry, index);
    if (!hasArguments && !command.has_value()) {
        throw CommandError(QStringLiteral("entry[%1] must contain arguments or command")
                               .arg(index));
    }
    Invocation invocation;
    invocation.style = commandStyle(entry);
    if (hasArguments) {
        invocation.argv = arguments;
        invocation.arguments = arguments;
        invocation.command = command;
        return invocation;
    }
    invocation.argv = splitCommand(command.value(), invocation.style, index);
    invocation.arguments = std::nullopt;
    invocation.command = command;
    return invocation;
}

QJsonObject compilerRecord(const QStringList &argv) {
    QStringList wrappers;
    int index = argv.isEmpty() ? 0 : envPrefix(argv, wrappers);
    while (index < argv.size() && kCompilerWrappers.contains(programName(argv.at(index)))) {
        wrappers.append(baseName(argv.at(index)));
        ++index;
    }
    if (index >= argv.size()) {
        index = static_cast<int>(argv.size()) - 1;
    }
    const QString path = index >= 0 && index < argv.size() ? argv.at(index) : QString();
    return QJsonObject{
        {QStringLiteral("family"), compilerFamily(programName(path))},
        {QStringLiteral("name"), baseName(path)},
        {QStringLiteral("path"), path},
        {QStringLiteral("wrappers"), QJsonArray::fromStringList(wrappers)},
    };
}

}  // namespace buildscope::native
