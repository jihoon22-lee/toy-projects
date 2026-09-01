#include "buildscope/diff.hpp"

#include "buildscope/contract.hpp"
#include "contract_loader.hpp"
#include "contract_parser.hpp"
#include "diff_validation.hpp"

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

QStringList parseIgnoredFields(const QJsonObject &object, const QString &location) {
    const auto ignored = boundedArray(object, QStringLiteral("ignored_fields"), location,
                                      kMaxIgnoredFields);
    QStringList result;
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
        result.append(value);
    }
    return result;
}

DiffSuppressionRule parseSuppressionRule(const QJsonValue &value,
                                         const QString &ruleLocation) {
    if (!value.isObject()) {
        throw ContractError(ruleLocation + " must be an object");
    }
    const auto rule = value.toObject();
    detail::rejectUnknownKeys(
        rule, {QStringLiteral("category"), QStringLiteral("path")}, ruleLocation);
    DiffSuppressionRule parsed;
    parsed.category = detail::requiredString(
        rule, QStringLiteral("category"), ruleLocation);
    if (parsed.category != QStringLiteral("*") &&
        !kChangeCategories.contains(parsed.category)) {
        throw ContractError(ruleLocation + ".category is unsupported: " + parsed.category);
    }
    parsed.path = detail::requiredString(rule, QStringLiteral("path"), ruleLocation);
    if (parsed.path.size() > 1024) {
        throw ContractError(ruleLocation + ".path exceeds 1024 characters");
    }
    if (parsed.path.contains(QLatin1Char('\\')) ||
        parsed.path.contains(QLatin1Char('[')) ||
        parsed.path.contains(QLatin1Char(']'))) {
        throw ContractError(
            ruleLocation + ".path supports only /, literal characters, *, **, and ?");
    }
    return parsed;
}

QVector<DiffSuppressionRule> parseSuppressionRules(const QJsonObject &object,
                                                   const QString &location) {
    const auto rules = boundedArray(object, QStringLiteral("suppression_rules"), location,
                                    kMaxSuppressions);
    QVector<DiffSuppressionRule> result;
    QSet<QString> seenRules;
    for (qsizetype index = 0; index < rules.size(); ++index) {
        const auto ruleLocation =
            location + ".suppression_rules[" + QString::number(index) + "]";
        const auto parsed = parseSuppressionRule(rules.at(index), ruleLocation);
        const auto identity = parsed.category + QChar::Null + parsed.path;
        if (seenRules.contains(identity)) {
            throw ContractError(location + ".suppression_rules contains a duplicate");
        }
        if (!result.isEmpty()) {
            const auto &previous = result.back();
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
        result.append(parsed);
    }
    return result;
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
    policy.ignoredFields = parseIgnoredFields(object, location);
    policy.suppressionRules = parseSuppressionRules(object, location);
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

qsizetype appendGlobToken(QString &expression, const QString &pattern, qsizetype index) {
    const auto character = pattern.at(index);
    if (character != QLatin1Char('*')) {
        expression += character == QLatin1Char('?')
                          ? QStringLiteral("[^/]")
                          : QRegularExpression::escape(QString(character));
        return index + 1;
    }
    if (index + 1 >= pattern.size() || pattern.at(index + 1) != QLatin1Char('*')) {
        expression += QStringLiteral("[^/]*");
        return index + 1;
    }
    if (index + 2 < pattern.size() && pattern.at(index + 2) == QLatin1Char('/')) {
        expression += QStringLiteral("(?:.*/)?");
        return index + 3;
    }
    expression += QStringLiteral(".*");
    return index + 2;
}

QString globRegularExpression(const QString &pattern) {
    QString expression = pattern.contains(QLatin1Char('/'))
                             ? QStringLiteral("^")
                             : QStringLiteral("^(?:.*/)?");
    for (qsizetype index = 0; index < pattern.size();) {
        index = appendGlobToken(expression, pattern, index);
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
    unit.after = diff_validation::parseConfiguration(
        object, QStringLiteral("after"), location);
    unit.before = diff_validation::parseConfiguration(
        object, QStringLiteral("before"), location);
    unit.kind = detail::requiredEnumString(
        object, QStringLiteral("kind"), location,
        {QStringLiteral("added"), QStringLiteral("changed"), QStringLiteral("moved"),
         QStringLiteral("removed")});
    unit.source = diff_validation::parseSource(object, location);
    unit.suppressed =
        detail::requiredBool(object, QStringLiteral("suppressed"), location);
    const auto changes = boundedArray(object, QStringLiteral("changes"), location,
                                      kMaxDiffChanges);
    unit.changes.reserve(changes.size());
    for (qsizetype changeIndex = 0; changeIndex < changes.size(); ++changeIndex) {
        unit.changes.append(diff_validation::parseChange(
            changes.at(changeIndex),
            location + ".changes[" + QString::number(changeIndex) + "]"));
    }
    diff_validation::validateUnitShape(unit, location);
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

void accumulateUnitSummary(DiffSummary &summary, const DiffUnit &unit) {
    if (unit.kind == QStringLiteral("added")) {
        ++summary.added;
    } else if (unit.kind == QStringLiteral("changed")) {
        ++summary.changed;
    } else if (unit.kind == QStringLiteral("moved")) {
        ++summary.moved;
    } else {
        ++summary.removed;
    }
    unit.suppressed ? ++summary.suppressedUnits : ++summary.visibleUnits;
    summary.changeCount += unit.changes.size();
    for (const auto &change : unit.changes) {
        change.suppressed ? ++summary.suppressedChanges : ++summary.visibleChanges;
    }
}

bool summariesMatch(const DiffSummary &actual, const DiffSummary &declared) {
    return actual.added == declared.added && actual.changed == declared.changed &&
           actual.changeCount == declared.changeCount && actual.moved == declared.moved &&
           actual.removed == declared.removed &&
           actual.suppressedChanges == declared.suppressedChanges &&
           actual.suppressedUnits == declared.suppressedUnits &&
           actual.visibleChanges == declared.visibleChanges &&
           actual.visibleUnits == declared.visibleUnits;
}

void validateSummary(const DiffReport &report) {
    DiffSummary actual;
    actual.unchanged = report.summary.unchanged;
    for (const auto &unit : report.units) {
        accumulateUnitSummary(actual, unit);
    }
    if (!summariesMatch(actual, report.summary)) {
        throw ContractError("root.summary does not match root.units");
    }
    const auto &declared = report.summary;
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
