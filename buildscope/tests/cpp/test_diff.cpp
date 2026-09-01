#include "buildscope/diff.hpp"
#include "buildscope/diff_model.hpp"
#include "buildscope/contract.hpp"

#include <QFile>
#include <QAbstractItemModelTester>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QJsonObject loadFixtureObject() {
    QFile file(QStringLiteral(BUILDSCOPE_SAMPLE_DIFF));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString writeObject(const QJsonObject &object, QTemporaryDir &temporary,
                    const QString &name = QStringLiteral("diff.json")) {
    const auto path = temporary.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.close();
    return path;
}

QString contractError(const QString &path) {
    try {
        (void)buildscope::loadDiffFile(path);
    } catch (const buildscope::ContractError &error) {
        return QString::fromUtf8(error.what());
    }
    return {};
}

}  // namespace

class DiffContractTest final : public QObject {
    Q_OBJECT

private slots:
    void loadsStrictDiffContract();
    void rejectsUnknownAndInconsistentFields();
    void rejectsSemanticDigestTampering();
    void rejectsOmittedSemanticChange();
    void rejectsInvalidSuppressionEvidence();
    void rejectsOmittedSuppressionEvidence();
    void rejectsFinalSymbolicLink();
    void buildsIssuesFirstTreeModel();
};

void DiffContractTest::loadsStrictDiffContract() {
    const auto report = buildscope::loadDiffFile(QStringLiteral(BUILDSCOPE_SAMPLE_DIFF));

    QCOMPARE(report.schemaVersion, QStringLiteral("buildscope.diff/v1"));
    QCOMPARE(report.producerVersion, QStringLiteral("0.4.0"));
    QCOMPARE(report.beforeInput.configurationCount, 3);
    QCOMPARE(report.afterInput.configurationCount, 3);
    QCOMPARE(report.summary.visibleUnits, 4);
    QCOMPARE(report.summary.changeCount, 12);
    QCOMPARE(report.units.size(), 4);
    QCOMPARE(report.units.at(0).kind, QStringLiteral("moved"));
    QCOMPARE(report.units.at(0).changes.size(), 2);
    QCOMPARE(report.units.at(0).changes.at(1).category, QStringLiteral("standard"));
    QCOMPARE(report.units.at(2).kind, QStringLiteral("changed"));
    QCOMPARE(report.units.at(2).changes.size(), 8);
    QCOMPARE(report.units.at(2).changes.at(0).category, QStringLiteral("compiler"));
    QVERIFY(!report.units.at(2).suppressed);
}

void DiffContractTest::rejectsUnknownAndInconsistentFields() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    auto root = loadFixtureObject();
    root.insert(QStringLiteral("unexpected"), true);
    const auto unknown = writeObject(root, temporary, QStringLiteral("unknown.json"));
    QVERIFY(contractError(unknown).contains(QStringLiteral("unsupported field unexpected")));

    root = loadFixtureObject();
    auto summary = root.value(QStringLiteral("summary")).toObject();
    summary.insert(QStringLiteral("visible_units"), 3);
    root.insert(QStringLiteral("summary"), summary);
    const auto summaryPath = writeObject(root, temporary, QStringLiteral("summary.json"));
    QCOMPARE(contractError(summaryPath),
             QStringLiteral("root.summary does not match root.units"));

    root = loadFixtureObject();
    auto units = root.value(QStringLiteral("units")).toArray();
    auto moved = units.at(0).toObject();
    moved.insert(QStringLiteral("kind"), QStringLiteral("changed"));
    units.replace(0, moved);
    root.insert(QStringLiteral("units"), units);
    const auto kindPath = writeObject(root, temporary, QStringLiteral("kind.json"));
    QVERIFY(contractError(kindPath).contains(QStringLiteral("changed unit identity")));
}

void DiffContractTest::rejectsSemanticDigestTampering() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    auto root = loadFixtureObject();
    auto units = root.value(QStringLiteral("units")).toArray();
    auto changed = units.at(2).toObject();
    auto before = changed.value(QStringLiteral("before")).toObject();
    auto semantic = before.value(QStringLiteral("semantic")).toObject();
    semantic.insert(QStringLiteral("standard"), QStringLiteral("c++99"));
    before.insert(QStringLiteral("semantic"), semantic);
    changed.insert(QStringLiteral("before"), before);
    units.replace(2, changed);
    root.insert(QStringLiteral("units"), units);
    const auto path = writeObject(root, temporary);

    QVERIFY(contractError(path).contains(
        QStringLiteral("semantic_digest does not match semantic configuration")));
}

void DiffContractTest::rejectsOmittedSemanticChange() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    auto root = loadFixtureObject();
    auto units = root.value(QStringLiteral("units")).toArray();
    auto moved = units.at(0).toObject();
    auto changes = moved.value(QStringLiteral("changes")).toArray();
    changes.removeAt(1);
    moved.insert(QStringLiteral("changes"), changes);
    units.replace(0, moved);
    root.insert(QStringLiteral("units"), units);
    auto summary = root.value(QStringLiteral("summary")).toObject();
    summary.insert(QStringLiteral("change_count"), 11);
    summary.insert(QStringLiteral("visible_changes"), 11);
    root.insert(QStringLiteral("summary"), summary);
    const auto path = writeObject(root, temporary);

    QVERIFY(contractError(path).contains(
        QStringLiteral("does not contain the exact semantic change set")));
}

void DiffContractTest::rejectsInvalidSuppressionEvidence() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    auto root = loadFixtureObject();
    auto policy = root.value(QStringLiteral("policy")).toObject();
    policy.insert(QStringLiteral("suppression_rules"),
                  QJsonArray{QJsonObject{{QStringLiteral("category"),
                                          QStringLiteral("standard")},
                                        {QStringLiteral("path"),
                                         QStringLiteral("other/*.cpp")}}});
    root.insert(QStringLiteral("policy"), policy);
    auto units = root.value(QStringLiteral("units")).toArray();
    auto changed = units.at(2).toObject();
    auto changes = changed.value(QStringLiteral("changes")).toArray();
    for (qsizetype index = 0; index < changes.size(); ++index) {
        auto change = changes.at(index).toObject();
        if (change.value(QStringLiteral("category")).toString() ==
            QStringLiteral("standard")) {
            change.insert(QStringLiteral("suppressed"), true);
            change.insert(QStringLiteral("suppression"),
                          QStringLiteral("standard:other/*.cpp"));
            changes.replace(index, change);
        }
    }
    changed.insert(QStringLiteral("changes"), changes);
    units.replace(2, changed);
    root.insert(QStringLiteral("units"), units);
    const auto path = writeObject(root, temporary);

    QVERIFY(contractError(path).contains(
        QStringLiteral("suppression does not match canonical policy and source")));
}

void DiffContractTest::rejectsOmittedSuppressionEvidence() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    auto root = loadFixtureObject();
    auto policy = root.value(QStringLiteral("policy")).toObject();
    policy.insert(QStringLiteral("suppression_rules"),
                  QJsonArray{QJsonObject{{QStringLiteral("category"),
                                          QStringLiteral("standard")},
                                        {QStringLiteral("path"),
                                         QStringLiteral("*.cpp")}}});
    root.insert(QStringLiteral("policy"), policy);
    const auto path = writeObject(root, temporary);

    QVERIFY(contractError(path).contains(
        QStringLiteral("suppression does not match canonical policy and source")));
}

void DiffContractTest::rejectsFinalSymbolicLink() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto target = writeObject(loadFixtureObject(), temporary,
                                    QStringLiteral("target.json"));
    const auto link = temporary.filePath(QStringLiteral("link.json"));
    if (!QFile::link(target, link)) {
        QSKIP("symbolic links are unavailable");
    }

    QVERIFY(contractError(link).contains(QStringLiteral("symbolic links are forbidden")));
}

void DiffContractTest::buildsIssuesFirstTreeModel() {
    buildscope::DiffTreeModel model;
    model.setReport(buildscope::loadDiffFile(QStringLiteral(BUILDSCOPE_SAMPLE_DIFF)));
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    Q_UNUSED(tester);

    QCOMPARE(model.unitCount(), 4);
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.columnCount(), buildscope::DiffTreeModel::ColumnCount);
    const auto moved = model.index(0, buildscope::DiffTreeModel::SourceColumn);
    QCOMPARE(moved.data(Qt::DisplayRole).toString(),
             QStringLiteral("src/move.cpp → renamed/move.cpp"));
    QCOMPARE(model.rowCount(moved), 2);
    const auto movedChange =
        model.index(0, buildscope::DiffTreeModel::CategoryColumn, moved);
    QCOMPARE(movedChange.data(Qt::DisplayRole).toString(), QStringLiteral("moved"));
    QCOMPARE(movedChange.data(buildscope::DiffUnitIndexRole).toLongLong(), 0);
    QCOMPARE(movedChange.data(buildscope::DiffChangeIndexRole).toLongLong(), 0);

    const auto changed = model.index(2, buildscope::DiffTreeModel::SourceColumn);
    QCOMPARE(model.rowCount(changed), 8);
    QVERIFY(changed.data(buildscope::DiffSearchTextRole)
                .toString()
                .contains(QStringLiteral("c++17")));
    QCOMPARE(model.parent(model.index(0, 0, changed)), changed);
}

QTEST_APPLESS_MAIN(DiffContractTest)

#include "test_diff.moc"
