#include "native_metadata.hpp"

#include <QMap>
#include <QRegularExpression>
#include <QSet>

namespace buildscope::native {
namespace {

const QRegularExpression kDefineName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
const QMap<QString, QString> kSourceLanguages = {
    {QStringLiteral(".c"), QStringLiteral("c")},
    {QStringLiteral(".cc"), QStringLiteral("c++")},
    {QStringLiteral(".cpp"), QStringLiteral("c++")},
    {QStringLiteral(".cxx"), QStringLiteral("c++")},
    {QStringLiteral(".m"), QStringLiteral("objective-c")},
    {QStringLiteral(".mm"), QStringLiteral("objective-c++")},
};
const QMap<QString, QString> kLanguageAliases = {
    {QStringLiteral("c"), QStringLiteral("c")},
    {QStringLiteral("c-header"), QStringLiteral("c")},
    {QStringLiteral("c++"), QStringLiteral("c++")},
    {QStringLiteral("c++-header"), QStringLiteral("c++")},
    {QStringLiteral("objective-c"), QStringLiteral("objective-c")},
    {QStringLiteral("objective-c++"), QStringLiteral("objective-c++")},
};

struct OptionMatch {
    std::optional<QString> value;
    int next = 0;
};

// _separated / _equals / _joined option spellings.
std::optional<OptionMatch> separated(const QStringList &argv, int index,
                                     const QString &option) {
    if (argv.at(index) != option) {
        return std::nullopt;
    }
    if (index + 1 < argv.size()) {
        return OptionMatch{argv.at(index + 1), index + 2};
    }
    return OptionMatch{std::nullopt, index + 1};
}

std::optional<OptionMatch> equals(const QStringList &argv, int index,
                                  const QString &option) {
    const QString &token = argv.at(index);
    if (token == option) {
        return OptionMatch{std::nullopt, index + 1};
    }
    const QString prefix = option + QLatin1Char('=');
    if (token.startsWith(prefix)) {
        const QString value = token.mid(prefix.size());
        return OptionMatch{value.isEmpty() ? std::optional<QString>(std::nullopt)
                                           : std::optional<QString>(value),
                           index + 1};
    }
    return std::nullopt;
}

std::optional<OptionMatch> joined(const QStringList &argv, int index, const QString &option) {
    const QString &token = argv.at(index);
    if (token == option) {
        return separated(argv, index, option);
    }
    if (token.startsWith(option)) {
        const QString value = token.mid(option.size());
        return OptionMatch{value.isEmpty() ? std::optional<QString>(std::nullopt)
                                           : std::optional<QString>(value),
                           index + 1};
    }
    return std::nullopt;
}

enum class Mode { Equals, Joined, Separated };

struct Candidate {
    QString option;
    Mode mode;
};

std::optional<OptionMatch> firstMatch(const QStringList &argv, int index,
                                      const std::initializer_list<Candidate> &options) {
    for (const Candidate &candidate : options) {
        std::optional<OptionMatch> match;
        switch (candidate.mode) {
        case Mode::Equals:
            match = equals(argv, index, candidate.option);
            break;
        case Mode::Joined:
            match = joined(argv, index, candidate.option);
            break;
        case Mode::Separated:
            match = separated(argv, index, candidate.option);
            break;
        }
        if (match.has_value()) {
            return match;
        }
    }
    return std::nullopt;
}

struct DefinitionResult {
    std::optional<QJsonObject> value;
    int next = 0;
};

std::optional<DefinitionResult> definition(const QStringList &argv, int index) {
    static const std::pair<QString, QString> options[] = {
        {QStringLiteral("-D"), QStringLiteral("define")},
        {QStringLiteral("-U"), QStringLiteral("undefine")},
        {QStringLiteral("/D"), QStringLiteral("define")},
        {QStringLiteral("/U"), QStringLiteral("undefine")},
    };
    for (const auto &[option, action] : options) {
        const auto match = joined(argv, index, option);
        if (!match.has_value()) {
            continue;
        }
        const QString raw = match->value.value_or(QString());
        const qsizetype separator = raw.indexOf(QLatin1Char('='));
        const QString name = separator < 0 ? raw : raw.left(separator);
        if (raw.isEmpty() || !kDefineName.match(name).hasMatch()) {
            return DefinitionResult{std::nullopt, match->next};
        }
        return DefinitionResult{
            QJsonObject{
                {QStringLiteral("action"), action},
                {QStringLiteral("name"), name},
                {QStringLiteral("value"),
                 separator < 0 ? QJsonValue(QJsonValue::Null)
                               : QJsonValue(raw.mid(separator + 1))},
            },
            match->next,
        };
    }
    return std::nullopt;
}

struct IncludeResult {
    QString kind;
    std::optional<QString> value;
    int next = 0;
};

std::optional<IncludeResult> include(const QStringList &argv, int index) {
    static const Candidate options[] = {
        {QStringLiteral("/external:I"), Mode::Joined},
        {QStringLiteral("/imsvc"), Mode::Joined},
        {QStringLiteral("-isystem"), Mode::Joined},
        {QStringLiteral("-iquote"), Mode::Joined},
        {QStringLiteral("-idirafter"), Mode::Joined},
        {QStringLiteral("-iframework"), Mode::Joined},
        {QStringLiteral("-F"), Mode::Joined},
        {QStringLiteral("-I"), Mode::Joined},
        {QStringLiteral("/I"), Mode::Joined},
    };
    static const QString kinds[] = {
        QStringLiteral("system"), QStringLiteral("system"), QStringLiteral("system"),
        QStringLiteral("quote"),  QStringLiteral("after"),  QStringLiteral("framework"),
        QStringLiteral("framework"), QStringLiteral("include"), QStringLiteral("include"),
    };
    for (std::size_t entry = 0; entry < std::size(options); ++entry) {
        const auto match = firstMatch(argv, index, {options[entry]});
        if (match.has_value()) {
            return IncludeResult{kinds[entry], match->value, match->next};
        }
    }
    return std::nullopt;
}

std::optional<OptionMatch> target(const QStringList &argv, int index) {
    const QString &token = argv.at(index);
    if (token.startsWith(QLatin1String("/clang:"))) {
        const QString forwarded = token.mid(7);
        for (const QString &prefix :
             {QStringLiteral("--target="), QStringLiteral("-target=")}) {
            if (forwarded.startsWith(prefix)) {
                const QString value = forwarded.mid(prefix.size());
                return OptionMatch{value.isEmpty() ? std::optional<QString>(std::nullopt)
                                                   : std::optional<QString>(value),
                                   index + 1};
            }
        }
    }
    return firstMatch(argv, index,
                      {{QStringLiteral("--target"), Mode::Separated},
                       {QStringLiteral("--target"), Mode::Equals},
                       {QStringLiteral("-target"), Mode::Separated},
                       {QStringLiteral("-target"), Mode::Equals}});
}

QString suffixOf(const QString &source) {
    const qsizetype slash = source.lastIndexOf(QLatin1Char('/'));
    const QString name = slash < 0 ? source : source.mid(slash + 1);
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? QString() : name.mid(dot).toLower();
}

std::optional<int> consumeStandard(const QStringList &argv, int index,
                                   ExtractedMetadata &result) {
    const QString &token = argv.at(index);
    if (token.startsWith(QLatin1String("/std:"))) {
        result.standard = token.mid(5);
        return index + 1;
    }
    const auto match = firstMatch(argv, index,
                                  {{QStringLiteral("-std"), Mode::Separated},
                                   {QStringLiteral("-std"), Mode::Equals}});
    if (!match.has_value()) {
        return std::nullopt;
    }
    if (match->value.has_value()) {
        result.standard = match->value.value();
    } else {
        result.diagnostics.append(
            diagnostic(QStringLiteral("missing-standard"),
                       QStringLiteral("Compiler standard flag has no value.")));
    }
    return match->next;
}

std::optional<int> consumeLanguage(const QStringList &argv, int index,
                                   ExtractedMetadata &result) {
    const QString &token = argv.at(index);
    const auto match = joined(argv, index, QStringLiteral("-x"));
    if (match.has_value()) {
        const QString key = match->value.value_or(QString()).toLower();
        const auto language = kLanguageAliases.constFind(key);
        if (language != kLanguageAliases.constEnd()) {
            result.language = language.value();
        } else {
            result.diagnostics.append(
                diagnostic(QStringLiteral("unknown-language"),
                           QStringLiteral("Compiler language flag is unsupported.")));
        }
        return match->next;
    }
    if (token.startsWith(QLatin1String("/Tc"))) {
        result.language = QStringLiteral("c");
        return index + 1;
    }
    if (token.startsWith(QLatin1String("/Tp"))) {
        result.language = QStringLiteral("c++");
        return index + 1;
    }
    return std::nullopt;
}

std::optional<int> consumeDefinition(const QStringList &argv, int index,
                                     ExtractedMetadata &result) {
    const auto match = definition(argv, index);
    if (!match.has_value()) {
        return std::nullopt;
    }
    if (!match->value.has_value()) {
        result.diagnostics.append(
            diagnostic(QStringLiteral("invalid-define"),
                       QStringLiteral("Compiler definition is malformed.")));
    } else {
        result.defines.append(match->value.value());
    }
    return match->next;
}

std::optional<int> consumeInclude(const QStringList &argv, int index,
                                  ExtractedMetadata &result) {
    const auto match = include(argv, index);
    if (!match.has_value()) {
        return std::nullopt;
    }
    if (match->value.has_value()) {
        result.includePaths.append(QJsonObject{
            {QStringLiteral("kind"), match->kind},
            {QStringLiteral("value"), match->value.value()},
        });
    } else {
        result.diagnostics.append(
            diagnostic(QStringLiteral("missing-include"),
                       QStringLiteral("Compiler include flag has no value.")));
    }
    return match->next;
}

std::optional<int> consumeSysroot(const QStringList &argv, int index,
                                  ExtractedMetadata &result) {
    const auto match = firstMatch(argv, index,
                                  {{QStringLiteral("--sysroot"), Mode::Separated},
                                   {QStringLiteral("--sysroot"), Mode::Equals},
                                   {QStringLiteral("-isysroot"), Mode::Joined}});
    if (!match.has_value()) {
        return std::nullopt;
    }
    if (match->value.has_value()) {
        result.sysroot = match->value.value();
    } else {
        result.diagnostics.append(
            diagnostic(QStringLiteral("missing-sysroot"),
                       QStringLiteral("Compiler sysroot flag has no value.")));
    }
    return match->next;
}

std::optional<int> consumeTarget(const QStringList &argv, int index,
                                 ExtractedMetadata &result) {
    const auto match = target(argv, index);
    if (!match.has_value()) {
        return std::nullopt;
    }
    if (match->value.has_value()) {
        result.targetTriple = match->value.value();
    } else {
        result.diagnostics.append(
            diagnostic(QStringLiteral("missing-target"),
                       QStringLiteral("Compiler target flag has no value.")));
    }
    return match->next;
}

std::optional<OptionMatch> outputOption(const QStringList &argv, int index) {
    const QString &token = argv.at(index);
    if (token == QLatin1String("-o") || token == QLatin1String("/Fo") ||
        token == QLatin1String("/Fo:")) {
        return separated(argv, index, token);
    }
    for (const QString &prefix : {QStringLiteral("/Fo:"), QStringLiteral("/Fo")}) {
        if (token.startsWith(prefix)) {
            const QString value = token.mid(prefix.size());
            return OptionMatch{value.isEmpty() ? std::optional<QString>(std::nullopt)
                                               : std::optional<QString>(value),
                               index + 1};
        }
    }
    return std::nullopt;
}

}  // namespace

QJsonObject diagnostic(const QString &code, const QString &message,
                       const QString &severity) {
    return QJsonObject{
        {QStringLiteral("code"), code},
        {QStringLiteral("message"), message},
        {QStringLiteral("severity"), severity},
    };
}

ExtractedMetadata extractMetadata(const QStringList &argv, const QString &source) {
    ExtractedMetadata result;
    result.language =
        kSourceLanguages.value(suffixOf(source), QStringLiteral(""));
    result.standard = QStringLiteral("");
    result.sysroot = QStringLiteral("");
    result.targetTriple = QStringLiteral("");
    int index = 1;
    while (index < argv.size()) {
        const QString &token = argv.at(index);
        if (token == QLatin1String("--")) {
            break;
        }
        if (token.startsWith(QLatin1Char('@'))) {
            bool seen = false;
            for (const QJsonValue &item : result.diagnostics) {
                if (item.toObject().value(QStringLiteral("code")).toString() ==
                    QLatin1String("response-file-opaque")) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                result.diagnostics.append(
                    diagnostic(QStringLiteral("response-file-opaque"),
                               QStringLiteral("Response-file contents were not expanded.")));
            }
            ++index;
            continue;
        }
        std::optional<int> consumed = consumeStandard(argv, index, result);
        if (!consumed.has_value()) {
            consumed = consumeLanguage(argv, index, result);
        }
        if (!consumed.has_value()) {
            consumed = consumeDefinition(argv, index, result);
        }
        if (!consumed.has_value()) {
            consumed = consumeInclude(argv, index, result);
        }
        if (!consumed.has_value()) {
            consumed = consumeSysroot(argv, index, result);
        }
        if (!consumed.has_value()) {
            consumed = consumeTarget(argv, index, result);
        }
        index = consumed.value_or(index + 1);
    }
    return result;
}

QString outputFromArgv(const QStringList &argv) {
    QString output;
    int index = 1;
    while (index < argv.size() && argv.at(index) != QLatin1String("--")) {
        const auto match = outputOption(argv, index);
        if (match.has_value()) {
            if (match->value.has_value()) {
                output = match->value.value();
            }
            index = match->next;
            continue;
        }
        ++index;
    }
    return output;
}

QString cmakeTarget(const QString &output) {
    QString replaced = output;
    replaced.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList parts = replaced.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (qsizetype index = 0; index + 1 < parts.size(); ++index) {
        const QString &part = parts.at(index);
        const QString &next = parts.at(index + 1);
        if (part == QLatin1String("CMakeFiles") && next.endsWith(QLatin1String(".dir"))) {
            return next.left(next.size() - 4);
        }
    }
    return {};
}

}  // namespace buildscope::native
