#include "native_diff.hpp"

#include "canonical_json.hpp"
#include "native_diff_policy.hpp"
#include "native_error.hpp"
#include "native_snapshot.hpp"

#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QSet>

#include <algorithm>

namespace buildscope::native {
namespace {

const QString kDiffSchema = QStringLiteral("buildscope.diff/v1");
const QString kProducerVersion = QStringLiteral(BUILDSCOPE_VERSION);
const QSet<QString> kUntrustedDiagnostics = {
    QStringLiteral("invalid-define"),  QStringLiteral("missing-standard"),
    QStringLiteral("missing-sysroot"), QStringLiteral("missing-target"),
    QStringLiteral("output-mismatch"), QStringLiteral("unknown-language"),
};

const std::pair<QString, QString> kChangeFields[] = {
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

const QMap<QString, int> kKindOrder = {
    {QStringLiteral("changed"), 0},
    {QStringLiteral("moved"), 1},
    {QStringLiteral("added"), 2},
    {QStringLiteral("removed"), 3},
};

QString label(const QString &value, const QString &name) {
    if (value.isEmpty() || value.contains(QLatin1Char('\0')) ||
        value.size() > kMaxLabelChars) {
        throw DiffError(QStringLiteral("%1 label must be a non-empty bounded string").arg(name));
    }
    return value;
}

// A record is identified by its index inside a stable QList — the C++
// replacement for Python id()-based pairing.
struct DiffContext {
    QList<QJsonObject> records;
};

QJsonObject makeRecord(const QJsonObject &entry, const QString &projectRoot,
                       const QString &databaseParent) {
    return QJsonObject{
        {QStringLiteral("key"), sourceKey(entry)},
        {QStringLiteral("source"), sourcePath(entry)},
        {QStringLiteral("style"),
         entry.value(QStringLiteral("normalized"))
             .toObject()
             .value(QStringLiteral("command_style"))},
        {QStringLiteral("view"), configurationView(entry, projectRoot, databaseParent)},
    };
}

QList<QJsonObject> records(const QJsonObject &snapshot) {
    const QJsonObject source = snapshot.value(QStringLiteral("source")).toObject();
    const QString projectRoot = source.value(QStringLiteral("project_root")).toString();
    const QString databaseParent =
        QFileInfo(source.value(QStringLiteral("path")).toString()).absolutePath();
    QList<QJsonObject> result;
    for (const QJsonValue &value : snapshot.value(QStringLiteral("entries")).toArray()) {
        result.append(makeRecord(value.toObject(), projectRoot, databaseParent));
    }
    std::sort(result.begin(), result.end(),
              [](const QJsonObject &first, const QJsonObject &second) {
                  const QString firstKey = first.value(QStringLiteral("key")).toString();
                  const QString secondKey = second.value(QStringLiteral("key")).toString();
                  if (firstKey != secondKey) {
                      return firstKey < secondKey;
                  }
                  const QString firstDigest = first.value(QStringLiteral("view"))
                                                  .toObject()
                                                  .value(QStringLiteral("semantic_digest"))
                                                  .toString();
                  const QString secondDigest = second.value(QStringLiteral("view"))
                                                   .toObject()
                                                   .value(QStringLiteral("semantic_digest"))
                                                   .toString();
                  if (firstDigest != secondDigest) {
                      return firstDigest < secondDigest;
                  }
                  return dumpsJson(first.value(QStringLiteral("view"))
                                       .toObject()
                                       .value(QStringLiteral("semantic")),
                                   false) <
                         dumpsJson(second.value(QStringLiteral("view"))
                                        .toObject()
                                        .value(QStringLiteral("semantic")),
                                   false);
              });
    for (int index = 0; index < result.size(); ++index) {
        QJsonObject record = result.at(index);
        QJsonObject view = record.value(QStringLiteral("view")).toObject();
        view.insert(QStringLiteral("entry_index"), index);
        record.insert(QStringLiteral("view"), view);
        result[index] = record;
    }
    return result;
}

void rejectUntrustedDiagnostics(const QJsonObject &snapshot) {
    for (const QJsonValue &entryValue :
         snapshot.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject entry = entryValue.toObject();
        for (const QJsonValue &item : entry.value(QStringLiteral("diagnostics")).toArray()) {
            const QJsonObject diagnostic = item.toObject();
            const QString code = diagnostic.value(QStringLiteral("code")).toString();
            const QString message = diagnostic.value(QStringLiteral("message")).toString();
            if (kUntrustedDiagnostics.contains(code) ||
                (code == QLatin1String("missing-include") &&
                 message.contains(QLatin1String("flag has no value")))) {
                throw DiffError(QStringLiteral("%1 cannot be compared exactly: %2: %3")
                                    .arg(sourcePath(entry), code, message));
            }
        }
    }
}

QString inventoryDigest(const QList<QJsonObject> &records) {
    QJsonArray inventory;
    for (const QJsonObject &record : records) {
        inventory.append(QJsonArray{
            record.value(QStringLiteral("key")),
            record.value(QStringLiteral("view"))
                .toObject()
                .value(QStringLiteral("semantic_digest")),
        });
    }
    QList<QJsonValue> sorted;
    for (const QJsonValue &value : inventory) {
        sorted.append(value);
    }
    std::sort(sorted.begin(), sorted.end(), [](const QJsonValue &first, const QJsonValue &second) {
        return dumpsJson(first, false) < dumpsJson(second, false);
    });
    QJsonArray canonical;
    for (const QJsonValue &value : sorted) {
        canonical.append(value);
    }
    return canonicalDigest(canonical);
}

QJsonObject inputRecord(const QString &inputLabel, const QList<QJsonObject> &records) {
    QSet<QString> sources;
    for (const QJsonObject &record : records) {
        sources.insert(record.value(QStringLiteral("key")).toString());
    }
    return QJsonObject{
        {QStringLiteral("configuration_count"), records.size()},
        {QStringLiteral("label"), inputLabel},
        {QStringLiteral("semantic_digest"), inventoryDigest(records)},
        {QStringLiteral("source_count"), sources.size()},
    };
}

QJsonObject change(const QString &category, const QJsonValue &before,
                   const QJsonValue &after) {
    return QJsonObject{
        {QStringLiteral("after"), after},
        {QStringLiteral("before"), before},
        {QStringLiteral("category"), category},
    };
}

QJsonArray semanticChanges(const QJsonObject &before, const QJsonObject &after) {
    QJsonArray changes;
    for (const auto &[category, field] : kChangeFields) {
        const QJsonValue beforeValue = before.value(QStringLiteral("view"))
                                           .toObject()
                                           .value(QStringLiteral("semantic"))
                                           .toObject()
                                           .value(field);
        const QJsonValue afterValue = after.value(QStringLiteral("view"))
                                          .toObject()
                                          .value(QStringLiteral("semantic"))
                                          .toObject()
                                          .value(field);
        if (beforeValue != afterValue) {
            changes.append(change(category, beforeValue, afterValue));
        }
    }
    return changes;
}

QJsonObject makeUnit(const QString &kind, const QJsonValue &before,
                     const QJsonValue &after, const QJsonArray &changes) {
    const QJsonObject record = after.isObject() ? after.toObject() : before.toObject();
    if (record.isEmpty()) {
        throw DiffError(QStringLiteral("diff unit must retain at least one configuration"));
    }
    return QJsonObject{
        {QStringLiteral("after"),
         after.isObject() ? after.toObject().value(QStringLiteral("view"))
                          : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("before"),
         before.isObject() ? before.toObject().value(QStringLiteral("view"))
                           : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("changes"), changes},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("source"),
         QJsonObject{
             {QStringLiteral("after"),
              after.isObject() ? after.toObject().value(QStringLiteral("source"))
                               : QJsonValue(QJsonValue::Null)},
             {QStringLiteral("before"),
              before.isObject() ? before.toObject().value(QStringLiteral("source"))
                                : QJsonValue(QJsonValue::Null)},
             {QStringLiteral("style"), record.value(QStringLiteral("style"))},
         }},
    };
}

QJsonObject changedUnit(const QJsonObject &before, const QJsonObject &after) {
    const QJsonArray changes = semanticChanges(before, after);
    if (changes.isEmpty()) {
        throw DiffError(QStringLiteral(
            "configuration pairing produced a change without semantic drift"));
    }
    return makeUnit(QStringLiteral("changed"), before, after, changes);
}

QJsonObject addedUnit(const QJsonObject &after) {
    return makeUnit(QStringLiteral("added"), QJsonValue(QJsonValue::Null), after,
                    QJsonArray{change(QStringLiteral("added"), QJsonValue(QJsonValue::Null),
                                      after.value(QStringLiteral("view"))
                                          .toObject()
                                          .value(QStringLiteral("semantic")))});
}

QJsonObject removedUnit(const QJsonObject &before) {
    return makeUnit(QStringLiteral("removed"), before, QJsonValue(QJsonValue::Null),
                    QJsonArray{change(QStringLiteral("removed"),
                                      before.value(QStringLiteral("view"))
                                          .toObject()
                                          .value(QStringLiteral("semantic")),
                                      QJsonValue(QJsonValue::Null))});
}

QJsonObject movedUnit(const QJsonObject &before, const QJsonObject &after) {
    QJsonArray changes{
        change(QStringLiteral("moved"), before.value(QStringLiteral("source")),
               after.value(QStringLiteral("source")))};
    const QJsonArray semantic = semanticChanges(before, after);
    for (const QJsonValue &value : semantic) {
        changes.append(value);
    }
    return makeUnit(QStringLiteral("moved"), before, after, changes);
}

struct DigestMatch {
    QList<int> before;
    QList<int> after;
    int unchanged = 0;
};

DigestMatch popDigestMatches(const QList<QJsonObject> &before,
                             const QList<QJsonObject> &after) {
    QMap<QString, QList<int>> beforeByDigest;
    QMap<QString, QList<int>> afterByDigest;
    for (int index = 0; index < before.size(); ++index) {
        beforeByDigest[before.at(index)
                           .value(QStringLiteral("view"))
                           .toObject()
                           .value(QStringLiteral("semantic_digest"))
                           .toString()]
            .append(index);
    }
    for (int index = 0; index < after.size(); ++index) {
        afterByDigest[after.at(index)
                          .value(QStringLiteral("view"))
                          .toObject()
                          .value(QStringLiteral("semantic_digest"))
                          .toString()]
            .append(index);
    }
    DigestMatch result;
    QSet<int> beforeMatched;
    QSet<int> afterMatched;
    for (const QString &digest : beforeByDigest.keys()) {
        if (!afterByDigest.contains(digest)) {
            continue;
        }
        QList<int> first = beforeByDigest.value(digest);
        QList<int> second = afterByDigest.value(digest);
        std::sort(first.begin(), first.end(), [&](int left, int right) {
            return before.at(left)
                       .value(QStringLiteral("view"))
                       .toObject()
                       .value(QStringLiteral("entry_index"))
                       .toInt() <
                   before.at(right)
                       .value(QStringLiteral("view"))
                       .toObject()
                       .value(QStringLiteral("entry_index"))
                       .toInt();
        });
        std::sort(second.begin(), second.end(), [&](int left, int right) {
            return after.at(left)
                       .value(QStringLiteral("view"))
                       .toObject()
                       .value(QStringLiteral("entry_index"))
                       .toInt() <
                   after.at(right)
                       .value(QStringLiteral("view"))
                       .toObject()
                       .value(QStringLiteral("entry_index"))
                       .toInt();
        });
        const int count = qMin(first.size(), second.size());
        for (int index = 0; index < count; ++index) {
            beforeMatched.insert(first.at(index));
            afterMatched.insert(second.at(index));
        }
        result.unchanged += count;
    }
    for (int index = 0; index < before.size(); ++index) {
        if (!beforeMatched.contains(index)) {
            result.before.append(index);
        }
    }
    for (int index = 0; index < after.size(); ++index) {
        if (!afterMatched.contains(index)) {
            result.after.append(index);
        }
    }
    return result;
}

QString viewRoleKey(const QJsonObject &record) {
    return roleKey(record.value(QStringLiteral("view")).toObject());
}

struct RolePairs {
    QList<QPair<int, int>> pairs;
    QList<int> before;
    QList<int> after;
};

RolePairs uniqueRolePairs(const QList<QJsonObject> &before,
                          const QList<QJsonObject> &after) {
    QMap<QString, int> beforeCounts;
    QMap<QString, int> afterCounts;
    QMap<QString, int> beforeByRole;
    QMap<QString, int> afterByRole;
    for (int index = 0; index < before.size(); ++index) {
        const QString key = viewRoleKey(before.at(index));
        beforeCounts[key] += 1;
        beforeByRole[key] = index;
    }
    for (int index = 0; index < after.size(); ++index) {
        const QString key = viewRoleKey(after.at(index));
        afterCounts[key] += 1;
        afterByRole[key] = index;
    }
    RolePairs result;
    QSet<int> beforePaired;
    QSet<int> afterPaired;
    for (const QString &key : beforeCounts.keys()) {
        QString stripped = key;
        stripped.remove(QLatin1Char('\0'));
        if (!afterCounts.contains(key) || stripped.isEmpty() ||
            beforeCounts.value(key) != 1 || afterCounts.value(key) != 1) {
            continue;
        }
        const int first = beforeByRole.value(key);
        const int second = afterByRole.value(key);
        result.pairs.append({first, second});
        beforePaired.insert(first);
        afterPaired.insert(second);
    }
    for (int index = 0; index < before.size(); ++index) {
        if (!beforePaired.contains(index)) {
            result.before.append(index);
        }
    }
    for (int index = 0; index < after.size(); ++index) {
        if (!afterPaired.contains(index)) {
            result.after.append(index);
        }
    }
    return result;
}

struct SameSourceResult {
    QJsonArray units;
    int unchanged = 0;
    bool ambiguous = false;
};

SameSourceResult sameSourceUnits(const QList<QJsonObject> &before,
                                 const QList<QJsonObject> &after) {
    const DigestMatch matched = popDigestMatches(before, after);
    const QList<QJsonObject> beforeRest = [&] {
        QList<QJsonObject> items;
        for (int index : matched.before) {
            items.append(before.at(index));
        }
        return items;
    }();
    const QList<QJsonObject> afterRest = [&] {
        QList<QJsonObject> items;
        for (int index : matched.after) {
            items.append(after.at(index));
        }
        return items;
    }();
    const RolePairs roles = uniqueRolePairs(beforeRest, afterRest);
    QList<QPair<QJsonObject, QJsonObject>> pairs;
    for (const auto &[first, second] : roles.pairs) {
        pairs.append({beforeRest.at(first), afterRest.at(second)});
    }
    QList<QJsonObject> remainingBefore;
    QList<QJsonObject> remainingAfter;
    for (int index : roles.before) {
        remainingBefore.append(beforeRest.at(index));
    }
    for (int index : roles.after) {
        remainingAfter.append(afterRest.at(index));
    }
    if (remainingBefore.size() == 1 && remainingAfter.size() == 1) {
        pairs.append({remainingBefore.takeFirst(), remainingAfter.takeFirst()});
    }
    SameSourceResult result;
    result.unchanged = matched.unchanged;
    for (const auto &[first, second] : pairs) {
        result.units.append(changedUnit(first, second));
    }
    for (const QJsonObject &item : remainingBefore) {
        result.units.append(removedUnit(item));
    }
    for (const QJsonObject &item : remainingAfter) {
        result.units.append(addedUnit(item));
    }
    result.ambiguous = !remainingBefore.isEmpty() && !remainingAfter.isEmpty();
    return result;
}

struct GroupComparison {
    QJsonArray units;
    QList<QJsonObject> removed;
    QList<QJsonObject> added;
    int unchanged = 0;
    QJsonArray diagnostics;
};

GroupComparison compareSourceGroups(const QList<QJsonObject> &beforeRecords,
                                    const QList<QJsonObject> &afterRecords) {
    QMap<QString, QList<QJsonObject>> beforeGroups;
    QMap<QString, QList<QJsonObject>> afterGroups;
    for (const QJsonObject &record : beforeRecords) {
        beforeGroups[record.value(QStringLiteral("key")).toString()].append(record);
    }
    for (const QJsonObject &record : afterRecords) {
        afterGroups[record.value(QStringLiteral("key")).toString()].append(record);
    }
    GroupComparison comparison;
    QSet<QString> keys;
    for (const QString &key : beforeGroups.keys()) {
        keys.insert(key);
    }
    for (const QString &key : afterGroups.keys()) {
        keys.insert(key);
    }
    QList<QString> sortedKeys = keys.values();
    std::sort(sortedKeys.begin(), sortedKeys.end());
    for (const QString &key : sortedKeys) {
        const QList<QJsonObject> first = beforeGroups.value(key);
        const QList<QJsonObject> second = afterGroups.value(key);
        if (first.isEmpty()) {
            comparison.added.append(second);
            continue;
        }
        if (second.isEmpty()) {
            comparison.removed.append(first);
            continue;
        }
        const SameSourceResult sourceResult = sameSourceUnits(first, second);
        for (const QJsonValue &unit : sourceResult.units) {
            comparison.units.append(unit);
        }
        comparison.unchanged += sourceResult.unchanged;
        if (sourceResult.ambiguous) {
            comparison.diagnostics.append(QJsonObject{
                {QStringLiteral("code"),
                 QStringLiteral("ambiguous-configuration-match")},
                {QStringLiteral("message"),
                 QStringLiteral("Multiple configurations could not be paired safely.")},
                {QStringLiteral("severity"), QStringLiteral("warning")},
                {QStringLiteral("source"),
                 first.first().value(QStringLiteral("source"))},
            });
        }
    }
    return comparison;
}

QString basenameOf(const QJsonObject &record) {
    QString source = record.value(QStringLiteral("source")).toString();
    source.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QString name = source.mid(source.lastIndexOf(QLatin1Char('/')) + 1);
    if (record.value(QStringLiteral("style")).toString() == QLatin1String("windows")) {
        name = name.toLower();
    }
    return name;
}

QString moveKey(const QJsonObject &record) {
    return basenameOf(record) + QLatin1Char('\0') + viewRoleKey(record);
}

QString exactKey(const QJsonObject &record) {
    return moveKey(record) + QLatin1Char('\0') +
           record.value(QStringLiteral("view"))
               .toObject()
               .value(QStringLiteral("semantic_digest"))
               .toString();
}

struct MovePairs {
    QJsonArray units;
    QList<QJsonObject> removed;
    QList<QJsonObject> added;
    QJsonArray diagnostics;
};

MovePairs movePairs(const QList<QJsonObject> &removed, const QList<QJsonObject> &added) {
    QSet<int> pairedRemoved;
    QSet<int> pairedAdded;
    MovePairs result;
    for (int pass = 0; pass < 2; ++pass) {
        const auto keyFunction = pass == 0 ? exactKey : moveKey;
        QList<int> remainingRemoved;
        QList<int> remainingAdded;
        for (int index = 0; index < removed.size(); ++index) {
            if (!pairedRemoved.contains(index)) {
                remainingRemoved.append(index);
            }
        }
        for (int index = 0; index < added.size(); ++index) {
            if (!pairedAdded.contains(index)) {
                remainingAdded.append(index);
            }
        }
        QMap<QString, int> removedCounts;
        QMap<QString, int> addedCounts;
        QMap<QString, int> removedByKey;
        QMap<QString, int> addedByKey;
        for (int index : remainingRemoved) {
            const QString key = keyFunction(removed.at(index));
            removedCounts[key] += 1;
            removedByKey[key] = index;
        }
        for (int index : remainingAdded) {
            const QString key = keyFunction(added.at(index));
            addedCounts[key] += 1;
            addedByKey[key] = index;
        }
        for (const QString &key : removedCounts.keys()) {
            QString stripped = key;
            stripped.remove(QLatin1Char('\0'));
            if (!addedCounts.contains(key) || stripped.isEmpty() ||
                removedCounts.value(key) != 1 || addedCounts.value(key) != 1) {
                continue;
            }
            const int first = removedByKey.value(key);
            const int second = addedByKey.value(key);
            result.units.append(movedUnit(removed.at(first), added.at(second)));
            pairedRemoved.insert(first);
            pairedAdded.insert(second);
        }
    }
    for (int index = 0; index < removed.size(); ++index) {
        if (!pairedRemoved.contains(index)) {
            result.removed.append(removed.at(index));
        }
    }
    for (int index = 0; index < added.size(); ++index) {
        if (!pairedAdded.contains(index)) {
            result.added.append(added.at(index));
        }
    }
    QSet<QString> removedKeys;
    QSet<QString> addedKeys;
    for (const QJsonObject &item : result.removed) {
        removedKeys.insert(moveKey(item));
    }
    for (const QJsonObject &item : result.added) {
        addedKeys.insert(moveKey(item));
    }
    QList<QString> ambiguousKeys;
    for (const QString &key : removedKeys) {
        if (addedKeys.contains(key)) {
            ambiguousKeys.append(key);
        }
    }
    std::sort(ambiguousKeys.begin(), ambiguousKeys.end());
    for (const QString &key : ambiguousKeys) {
        result.diagnostics.append(QJsonObject{
            {QStringLiteral("code"), QStringLiteral("ambiguous-move-match")},
            {QStringLiteral("message"),
             QStringLiteral("Potential source moves could not be paired safely.")},
            {QStringLiteral("severity"), QStringLiteral("warning")},
            {QStringLiteral("source"), key.split(QLatin1Char('\0')).first()},
        });
    }
    return result;
}

void applySuppressions(QJsonArray &units, const QJsonArray &rules) {
    for (qsizetype index = 0; index < units.size(); ++index) {
        QJsonObject unit = units.at(index).toObject();
        const QJsonObject source = unit.value(QStringLiteral("source")).toObject();
        const QString before = source.value(QStringLiteral("before")).toString();
        const QString after = source.value(QStringLiteral("after")).toString();
        const bool windows = source.value(QStringLiteral("style")).toString() ==
                             QLatin1String("windows");
        QJsonArray changes = unit.value(QStringLiteral("changes")).toArray();
        bool allSuppressed = true;
        for (qsizetype changeIndex = 0; changeIndex < changes.size(); ++changeIndex) {
            QJsonObject changeItem = changes.at(changeIndex).toObject();
            const QString rule = matchingSuppression(
                rules, changeItem.value(QStringLiteral("category")).toString(), before,
                after, windows);
            changeItem.insert(QStringLiteral("suppressed"), !rule.isEmpty());
            changeItem.insert(QStringLiteral("suppression"),
                              rule.isEmpty() ? QJsonValue(QJsonValue::Null)
                                             : QJsonValue(rule));
            changes.replace(changeIndex, changeItem);
            allSuppressed = allSuppressed && !rule.isEmpty();
        }
        unit.insert(QStringLiteral("changes"), changes);
        unit.insert(QStringLiteral("suppressed"), allSuppressed);
        units.replace(index, unit);
    }
}

void sortUnits(QJsonArray &units) {
    QList<QJsonObject> items;
    for (const QJsonValue &value : units) {
        items.append(value.toObject());
    }
    std::sort(items.begin(), items.end(),
              [](const QJsonObject &first, const QJsonObject &second) {
                  const QJsonObject firstSource =
                      first.value(QStringLiteral("source")).toObject();
                  const QJsonObject secondSource =
                      second.value(QStringLiteral("source")).toObject();
                  QString firstName =
                      firstSource.value(QStringLiteral("after")).toString().isEmpty()
                          ? firstSource.value(QStringLiteral("before")).toString()
                          : firstSource.value(QStringLiteral("after")).toString();
                  QString secondName =
                      secondSource.value(QStringLiteral("after")).toString().isEmpty()
                          ? secondSource.value(QStringLiteral("before")).toString()
                          : secondSource.value(QStringLiteral("after")).toString();
                  if (firstSource.value(QStringLiteral("style")).toString() ==
                      QLatin1String("windows")) {
                      firstName = firstName.toLower();
                  }
                  if (secondSource.value(QStringLiteral("style")).toString() ==
                      QLatin1String("windows")) {
                      secondName = secondName.toLower();
                  }
                  if (firstName != secondName) {
                      return firstName < secondName;
                  }
                  const int firstKind =
                      kKindOrder.value(first.value(QStringLiteral("kind")).toString(), 4);
                  const int secondKind =
                      kKindOrder.value(second.value(QStringLiteral("kind")).toString(), 4);
                  if (firstKind != secondKind) {
                      return firstKind < secondKind;
                  }
                  const QJsonObject firstView =
                      first.value(QStringLiteral("after")).isObject()
                          ? first.value(QStringLiteral("after")).toObject()
                          : first.value(QStringLiteral("before")).toObject();
                  const QJsonObject secondView =
                      second.value(QStringLiteral("after")).isObject()
                          ? second.value(QStringLiteral("after")).toObject()
                          : second.value(QStringLiteral("before")).toObject();
                  return firstView.value(QStringLiteral("semantic_digest")).toString() <
                         secondView.value(QStringLiteral("semantic_digest")).toString();
              });
    units = QJsonArray();
    for (const QJsonObject &item : items) {
        units.append(item);
    }
}

QJsonObject summary(const QJsonArray &units, int unchanged) {
    int added = 0;
    int changed = 0;
    int moved = 0;
    int removed = 0;
    int changeCount = 0;
    int suppressedChanges = 0;
    int suppressedUnits = 0;
    int visibleChanges = 0;
    int visibleUnits = 0;
    for (const QJsonValue &value : units) {
        const QJsonObject unit = value.toObject();
        const QString kind = unit.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("added")) {
            ++added;
        } else if (kind == QLatin1String("changed")) {
            ++changed;
        } else if (kind == QLatin1String("moved")) {
            ++moved;
        } else if (kind == QLatin1String("removed")) {
            ++removed;
        }
        const QJsonArray changes = unit.value(QStringLiteral("changes")).toArray();
        changeCount += changes.size();
        for (const QJsonValue &item : changes) {
            if (item.toObject().value(QStringLiteral("suppressed")).toBool()) {
                ++suppressedChanges;
            } else {
                ++visibleChanges;
            }
        }
        if (unit.value(QStringLiteral("suppressed")).toBool()) {
            ++suppressedUnits;
        } else {
            ++visibleUnits;
        }
    }
    return QJsonObject{
        {QStringLiteral("added"), added},
        {QStringLiteral("changed"), changed},
        {QStringLiteral("change_count"), changeCount},
        {QStringLiteral("moved"), moved},
        {QStringLiteral("removed"), removed},
        {QStringLiteral("suppressed_changes"), suppressedChanges},
        {QStringLiteral("suppressed_units"), suppressedUnits},
        {QStringLiteral("unchanged"), unchanged},
        {QStringLiteral("visible_changes"), visibleChanges},
        {QStringLiteral("visible_units"), visibleUnits},
    };
}

}  // namespace

QJsonObject compareDatabases(const QString &beforePath, const QString &afterPath,
                             const QString &beforeProjectRoot, const QString &afterProjectRoot,
                             const QString &beforeLabel, const QString &afterLabel,
                             const QStringList &suppressions) {
    const QString checkedBeforeLabel = label(beforeLabel, QStringLiteral("before"));
    const QString checkedAfterLabel = label(afterLabel, QStringLiteral("after"));
    QJsonArray rules;
    QList<QJsonObject> beforeRecords;
    QList<QJsonObject> afterRecords;
    try {
        rules = parseSuppressions(suppressions);
        const QJsonObject beforeSnapshot =
            loadCompilationDatabase(beforePath, beforeProjectRoot);
        const QJsonObject afterSnapshot = loadCompilationDatabase(afterPath, afterProjectRoot);
        rejectUntrustedDiagnostics(beforeSnapshot);
        rejectUntrustedDiagnostics(afterSnapshot);
        beforeRecords = records(beforeSnapshot);
        afterRecords = records(afterSnapshot);
    } catch (const DiffError &) {
        throw;
    } catch (const NativeError &error) {
        throw DiffError(QString::fromStdString(error.what()));
    }
    GroupComparison comparison = compareSourceGroups(beforeRecords, afterRecords);
    const MovePairs moves = movePairs(comparison.removed, comparison.added);
    for (const QJsonValue &diagnostic : moves.diagnostics) {
        comparison.diagnostics.append(diagnostic);
    }
    QJsonArray units = comparison.units;
    for (const QJsonValue &unit : moves.units) {
        units.append(unit);
    }
    for (const QJsonObject &item : moves.removed) {
        units.append(removedUnit(item));
    }
    for (const QJsonObject &item : moves.added) {
        units.append(addedUnit(item));
    }
    applySuppressions(units, rules);
    sortUnits(units);
    return QJsonObject{
        {QStringLiteral("diagnostics"), comparison.diagnostics},
        {QStringLiteral("inputs"),
         QJsonObject{
             {QStringLiteral("after"), inputRecord(checkedAfterLabel, afterRecords)},
             {QStringLiteral("before"), inputRecord(checkedBeforeLabel, beforeRecords)},
         }},
        {QStringLiteral("policy"), policyRecord(rules)},
        {QStringLiteral("producer"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("buildscope")},
                     {QStringLiteral("version"), kProducerVersion}}},
        {QStringLiteral("schema_version"), kDiffSchema},
        {QStringLiteral("summary"), summary(units, comparison.unchanged)},
        {QStringLiteral("units"), units},
    };
}

QString dumpsDiff(const QJsonObject &report, bool pretty) {
    QString rendered = dumpsJson(report, pretty) + QLatin1Char('\n');
    if (rendered.toUtf8().size() > kMaxSnapshotBytes) {
        throw DiffError(QStringLiteral("diff report exceeds %1 byte limit")
                            .arg(kMaxSnapshotBytes));
    }
    return rendered;
}

}  // namespace buildscope::native
