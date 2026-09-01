#include "buildscope/compilation_model.hpp"

#include <QAbstractItemModelTester>
#include <QJsonDocument>
#include <QJsonValue>
#include <QtTest>

#include <initializer_list>
#include <optional>

namespace {

using buildscope::Snapshot;
using buildscope::SnapshotEntry;

SnapshotEntry normalizedEntry(const QString &source, const QString &configuration,
                              const QString &status, const QString &target,
                              const QString &compiler, const QString &standard = "c++20") {
    SnapshotEntry entry;
    entry.file = source;
    entry.directory = QStringLiteral("build");
    entry.arguments = QStringList{compiler, QStringLiteral("-std=") + standard,
                                  QStringLiteral("-c"), source};
    entry.command = entry.arguments.join(QLatin1Char(' '));
    entry.hasNormalized = true;
    entry.normalized.argv = entry.arguments;
    entry.normalized.commandStyle = QStringLiteral("posix");
    entry.normalized.invocationSource = QStringLiteral("arguments");
    entry.normalized.compiler.family = compiler.startsWith(QStringLiteral("clang"))
                                           ? QStringLiteral("clang")
                                           : QStringLiteral("gcc");
    entry.normalized.compiler.name = compiler;
    entry.normalized.compiler.path = QStringLiteral("/usr/bin/") + compiler;
    entry.normalized.configuration = configuration;
    entry.normalized.directory.path = entry.directory;
    entry.normalized.directory.scope = QStringLiteral("project");
    entry.normalized.directory.style = QStringLiteral("posix");
    entry.normalized.directory.exists = true;
    entry.normalized.language = QStringLiteral("c++");
    entry.normalized.source.path = source;
    entry.normalized.source.scope = QStringLiteral("project");
    entry.normalized.source.style = QStringLiteral("posix");
    entry.normalized.source.exists = true;
    entry.normalized.standard = standard;
    entry.normalized.target.buildTarget = target;
    entry.normalized.target.triple = QStringLiteral("x86_64-linux-gnu");
    entry.hasState = true;
    entry.state.sourceStatus = status;
    return entry;
}

Snapshot normalizedSnapshot(std::initializer_list<SnapshotEntry> entries) {
    Snapshot snapshot;
    snapshot.schemaVersion = QString::fromLatin1(buildscope::kSnapshotSchemaV2);
    snapshot.producerVersion = QStringLiteral("0.2.0");
    snapshot.sourcePath = QStringLiteral("compile_commands.json");
    snapshot.projectRoot = QStringLiteral("/project");
    for (const auto &entry : entries) {
        snapshot.entries.append(entry);
    }
    return snapshot;
}

SnapshotEntry legacyEntry(const QString &source) {
    SnapshotEntry entry;
    entry.file = source;
    entry.directory = QStringLiteral("legacy-build");
    entry.arguments = QStringList{QStringLiteral("c++"), QStringLiteral("-DNAME=value"),
                                  QString(), QStringLiteral("-c"), source};
    entry.command = QStringLiteral("c++ -DNAME=value -c ") + source;
    return entry;
}

QVariant displayData(const QModelIndex &index, int column) {
    return index.sibling(index.row(), column).data(Qt::DisplayRole);
}

}  // namespace

class CompilationModelTest final : public QObject {
    Q_OBJECT

private slots:
    void renderArgumentVectorPreservesArguments();
    void normalizedGroupingExposesTreeContract();
    void legacyV1ProjectionUsesRawInvocation();
    void statusAggregationUsesStrongestState();
};

void CompilationModelTest::renderArgumentVectorPreservesArguments() {
    const QStringList arguments = {QStringLiteral("c++"),
                                   QStringLiteral("-DNAME=hello world"),
                                   QStringLiteral("a\"b"),
                                   QString(),
                                   QStringLiteral("path with spaces"),
                                   QStringLiteral("'single'")};

    const auto rendered = buildscope::renderArgumentVector(arguments);
    const auto expected = QString::fromUtf8(
        R"(["c++","-DNAME=hello world","a\"b","","path with spaces","'single'"])"
    );
    QCOMPARE(rendered, expected);

    const auto document = QJsonDocument::fromJson(rendered.toUtf8());
    QVERIFY(!document.isNull());
    QVERIFY(document.isArray());
    const auto values = document.array();
    QCOMPARE(values.size(), arguments.size());
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        QCOMPARE(values.at(index).toString(), arguments.at(index));
    }
}

void CompilationModelTest::normalizedGroupingExposesTreeContract() {
    const auto digestA = QStringLiteral(
        "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    const auto digestB = QStringLiteral(
        "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    auto first = normalizedEntry(QStringLiteral("src/shared.cpp"), digestA,
                                 QStringLiteral("present"), QStringLiteral("app"),
                                 QStringLiteral("clang++"));
    auto second = normalizedEntry(QStringLiteral("src/shared.cpp"), digestB,
                                  QStringLiteral("missing"), QStringLiteral("tool"),
                                  QStringLiteral("g++"));
    second.normalized.defines.append(
        {QStringLiteral("define"), QStringLiteral("FEATURE"), QStringLiteral("enabled")});
    second.normalized.includePaths.append(
        {QStringLiteral("include"), QStringLiteral("project"), QStringLiteral("posix"), true,
         QStringLiteral("include"), 0});
    second.diagnostics.append(
        {QStringLiteral("Wshadow"), QStringLiteral("shadowed declaration"),
         QStringLiteral("warning")});
    second.hasIncludeAnalysis = true;
    second.includeAnalysis.evidence = QStringLiteral("compiler-measured");
    second.includeAnalysis.command =
        QStringList{QStringLiteral("/usr/bin/g++"), QStringLiteral("-H")};
    buildscope::SnapshotIncludeEdge includeEdge;
    includeEdge.parent = QStringLiteral("src/shared.cpp");
    includeEdge.requested = QStringLiteral("common.hpp");
    includeEdge.resolved = QStringLiteral("include/first/common.hpp");
    includeEdge.classification = QStringLiteral("project");
    includeEdge.delimiter = QStringLiteral("quote");
    includeEdge.evidence = QStringLiteral("compiler-measured");
    includeEdge.locationEvidence = QStringLiteral("source-scan");
    includeEdge.line = 4;
    includeEdge.alternatives =
        QStringList{QStringLiteral("include/second/common.hpp")};
    includeEdge.search.append({QStringLiteral("include/first/common.hpp"), true,
                               QStringLiteral("include"), 0, true});
    second.includeAnalysis.edges.append(includeEdge);
    const auto other = normalizedEntry(QStringLiteral("src/other.cpp"), digestA,
                                       QStringLiteral("present"), QStringLiteral("app"),
                                       QStringLiteral("clang++"));
    auto snapshot = normalizedSnapshot({first, second, other});

    buildscope::CompilationTreeModel model;
    model.setSnapshot(snapshot);
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    Q_UNUSED(tester);

    QCOMPARE(model.sourceCount(), 2);
    QCOMPARE(model.entryCount(), 3);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.columnCount(), buildscope::CompilationTreeModel::ColumnCount);
    QCOMPARE(model.headerData(buildscope::CompilationTreeModel::SourceColumn,
                              Qt::Horizontal),
             QVariant(QStringLiteral("Source / configuration")));
    QVERIFY(!model.headerData(0, Qt::Vertical).isValid());

    const auto shared = model.index(0, buildscope::CompilationTreeModel::SourceColumn);
    const auto otherIndex = model.index(1, buildscope::CompilationTreeModel::SourceColumn);
    QVERIFY(shared.isValid());
    QVERIFY(otherIndex.isValid());
    QVERIFY(!model.parent(shared).isValid());
    QCOMPARE(model.rowCount(shared), 2);
    QCOMPARE(model.rowCount(model.index(0, buildscope::CompilationTreeModel::StatusColumn)), 0);
    QCOMPARE(shared.data(Qt::DisplayRole).toString(), QStringLiteral("src/shared.cpp (2)"));
    QCOMPARE(shared.data(buildscope::NodeKindRole).toInt(),
             static_cast<int>(buildscope::CompilationNodeKind::Source));
    QVERIFY(!shared.data(buildscope::EntryIndexRole).isValid());
    QCOMPARE(shared.data(buildscope::SourcePathRole).toString(),
             QStringLiteral("src/shared.cpp"));
    QCOMPARE(shared.data(buildscope::SourceStatusRole).toString(), QStringLiteral("missing"));
    QCOMPARE(displayData(shared, buildscope::CompilationTreeModel::StatusColumn).toString(),
             QStringLiteral("missing"));
    QCOMPARE(displayData(shared, buildscope::CompilationTreeModel::TargetColumn).toString(),
             QStringLiteral("2 targets"));
    QCOMPARE(displayData(shared, buildscope::CompilationTreeModel::CompilerColumn).toString(),
             QStringLiteral("2 compilers"));
    QCOMPARE(displayData(shared, buildscope::CompilationTreeModel::StandardColumn).toString(),
             QStringLiteral("c++20"));
    QCOMPARE(displayData(shared, buildscope::CompilationTreeModel::ConfigurationColumn)
                 .toString(),
             QStringLiteral("2 configurations"));
    QVERIFY(shared.data(buildscope::SearchTextRole).toString().contains(
        QStringLiteral("src/shared.cpp")));
    QVERIFY(shared.data(Qt::ToolTipRole).toString().contains(QStringLiteral("2 configuration")));

    const auto sharedFirst =
        model.index(0, buildscope::CompilationTreeModel::SourceColumn, shared);
    const auto sharedSecond =
        model.index(1, buildscope::CompilationTreeModel::SourceColumn, shared);
    QVERIFY(sharedFirst.isValid());
    QVERIFY(sharedSecond.isValid());
    QVERIFY(model.parent(sharedFirst) == shared);
    QVERIFY(model.parent(sharedSecond) == shared);
    QCOMPARE(model.rowCount(sharedFirst), 0);
    QCOMPARE(sharedFirst.data(Qt::DisplayRole).toString(), QStringLiteral("Configuration 1"));
    QCOMPARE(sharedSecond.data(Qt::DisplayRole).toString(), QStringLiteral("Configuration 2"));
    QCOMPARE(sharedFirst.data(buildscope::NodeKindRole).toInt(),
             static_cast<int>(buildscope::CompilationNodeKind::Configuration));
    QCOMPARE(sharedSecond.data(buildscope::EntryIndexRole).toLongLong(), 1LL);
    QCOMPARE(sharedSecond.data(buildscope::SourcePathRole).toString(),
             QStringLiteral("src/shared.cpp"));
    QCOMPARE(sharedSecond.data(buildscope::SourceStatusRole).toString(),
             QStringLiteral("missing"));
    QCOMPARE(displayData(sharedFirst, buildscope::CompilationTreeModel::ConfigurationColumn)
                 .toString(),
             QStringLiteral("sha256:aaaaaaaaaaaa…"));
    QCOMPARE(displayData(sharedSecond, buildscope::CompilationTreeModel::ConfigurationColumn)
                 .toString(),
             QStringLiteral("sha256:bbbbbbbbbbbb…"));
    QVERIFY(sharedSecond.data(Qt::ToolTipRole).toString().contains(QStringLiteral("FEATURE")));
    QVERIFY(sharedSecond.data(buildscope::SearchTextRole).toString().contains(
        QStringLiteral("Wshadow")));

    const auto sharedSecondStatus =
        model.index(1, buildscope::CompilationTreeModel::StatusColumn, shared);
    const auto firstEntryIndex = model.entryIndex(sharedFirst);
    QVERIFY(firstEntryIndex.has_value());
    QCOMPARE(*firstEntryIndex, qsizetype(0));
    const auto secondEntryIndex = model.entryIndex(sharedSecondStatus);
    QVERIFY(secondEntryIndex.has_value());
    QCOMPARE(*secondEntryIndex, qsizetype(1));
    QVERIFY(!model.entryIndex(shared).has_value());
    QVERIFY(!model.entryView(shared).isValid());
    const auto secondView = model.entryView(sharedSecond);
    QVERIFY(secondView.isValid());
    QVERIFY(secondView.isNormalized());
    QCOMPARE(secondView.sourcePath(), QStringLiteral("src/shared.cpp"));
    QCOMPARE(secondView.directoryPath(), QStringLiteral("build"));
    QCOMPARE(secondView.sourceStatus(), QStringLiteral("missing"));
    QCOMPARE(secondView.compilerLabel(), QStringLiteral("g++"));
    QCOMPARE(secondView.targetLabel(), QStringLiteral("tool · x86_64-linux-gnu"));
    QCOMPARE(secondView.standard(), QStringLiteral("c++20"));
    QCOMPARE(secondView.configurationId(), digestB);
    QCOMPARE(secondView.invocationSource(), QStringLiteral("arguments"));
    QCOMPARE(secondView.structuredArguments(),
             buildscope::renderArgumentVector(second.arguments));
    QCOMPARE(secondView.rawCommand(), second.command);
    QVERIFY(secondView.searchText().contains(QStringLiteral("enabled")));
    QVERIFY(secondView.searchText().contains(QStringLiteral("shadowed declaration")));
    QVERIFY(secondView.searchText().contains(QStringLiteral("compiler-measured")));
    QVERIFY(secondView.searchText().contains(QStringLiteral("include/second/common.hpp")));

    QCOMPARE(otherIndex.data(Qt::DisplayRole).toString(), QStringLiteral("src/other.cpp (1)"));
    QCOMPARE(otherIndex.data(buildscope::EntryIndexRole).toLongLong(), 2LL);
    QCOMPARE(otherIndex.data(buildscope::SourceStatusRole).toString(),
             QStringLiteral("present"));
    QCOMPARE(displayData(otherIndex, buildscope::CompilationTreeModel::ConfigurationColumn)
                 .toString(),
             QStringLiteral("sha256:aaaaaaaaaaaa…"));
    const auto otherEntryIndex = model.entryIndex(otherIndex);
    QVERIFY(otherEntryIndex.has_value());
    QCOMPARE(*otherEntryIndex, qsizetype(2));
    QCOMPARE(model.entryView(otherIndex).sourcePath(), QStringLiteral("src/other.cpp"));

    QVERIFY(!model.index(0, buildscope::CompilationTreeModel::ColumnCount).isValid());
    QVERIFY(!model.index(0, buildscope::CompilationTreeModel::SourceColumn,
                         model.index(0, buildscope::CompilationTreeModel::StatusColumn))
                  .isValid());
}

void CompilationModelTest::legacyV1ProjectionUsesRawInvocation() {
    Snapshot snapshot;
    snapshot.schemaVersion = QString::fromLatin1(buildscope::kSnapshotSchemaV1);
    snapshot.sourcePath = QStringLiteral("compile_commands.json");
    snapshot.entries.append(legacyEntry(QStringLiteral("legacy/main.cpp")));

    buildscope::CompilationTreeModel model;
    model.setSnapshot(snapshot);
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    Q_UNUSED(tester);

    QCOMPARE(model.sourceCount(), 1);
    QCOMPARE(model.entryCount(), 1);
    const auto source = model.index(0, buildscope::CompilationTreeModel::SourceColumn);
    QVERIFY(source.isValid());
    QCOMPARE(source.data(Qt::DisplayRole).toString(), QStringLiteral("legacy/main.cpp (1)"));
    QCOMPARE(source.data(buildscope::SourcePathRole).toString(),
             QStringLiteral("legacy/main.cpp"));
    QCOMPARE(source.data(buildscope::SourceStatusRole).toString(), QStringLiteral("unknown"));
    QVERIFY(displayData(source, buildscope::CompilationTreeModel::TargetColumn)
                .toString()
                .isEmpty());
    QVERIFY(displayData(source, buildscope::CompilationTreeModel::CompilerColumn)
                .toString()
                .isEmpty());
    QVERIFY(displayData(source, buildscope::CompilationTreeModel::StandardColumn)
                .toString()
                .isEmpty());
    QVERIFY(displayData(source, buildscope::CompilationTreeModel::ConfigurationColumn)
                .toString()
                .isEmpty());
    QCOMPARE(source.data(buildscope::EntryIndexRole).toLongLong(), 0LL);

    const auto view = model.entryView(source);
    QVERIFY(view.isValid());
    QVERIFY(!view.isNormalized());
    QCOMPARE(view.sourcePath(), QStringLiteral("legacy/main.cpp"));
    QCOMPARE(view.directoryPath(), QStringLiteral("legacy-build"));
    QCOMPARE(view.sourceStatus(), QStringLiteral("unknown"));
    QVERIFY(view.compilerLabel().isEmpty());
    QVERIFY(view.targetLabel().isEmpty());
    QVERIFY(view.standard().isEmpty());
    QVERIFY(view.configurationId().isEmpty());
    QCOMPARE(view.invocationSource(), QStringLiteral("arguments"));
    QCOMPARE(view.structuredArguments(),
             QStringLiteral(R"(["c++","-DNAME=value","","-c","legacy/main.cpp"])"));
    QCOMPARE(view.rawCommand(), QStringLiteral("c++ -DNAME=value -c legacy/main.cpp"));
    QVERIFY(view.searchText().contains(QStringLiteral("legacy/main.cpp")));

    const auto configuration =
        model.index(0, buildscope::CompilationTreeModel::ConfigurationColumn, source);
    QVERIFY(configuration.isValid());
    const auto configurationEntryIndex = model.entryIndex(configuration);
    QVERIFY(configurationEntryIndex.has_value());
    QCOMPARE(*configurationEntryIndex, qsizetype(0));
    QCOMPARE(model.entryView(configuration).structuredArguments(), view.structuredArguments());
}

void CompilationModelTest::statusAggregationUsesStrongestState() {
    const auto source = QStringLiteral("src/status.cpp");
    const auto digest = QStringLiteral(
        "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    const auto makeSnapshot = [&](const QStringList &statuses) {
        Snapshot snapshot;
        snapshot.schemaVersion = QString::fromLatin1(buildscope::kSnapshotSchemaV2);
        for (qsizetype index = 0; index < statuses.size(); ++index) {
            auto entry = normalizedEntry(source, digest + QString::number(index), statuses.at(index),
                                         QStringLiteral("app"), QStringLiteral("g++"));
            if (statuses.at(index) == QLatin1String("unknown")) {
                entry.hasState = false;
            }
            snapshot.entries.append(entry);
        }
        return snapshot;
    };

    buildscope::CompilationTreeModel model;
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    Q_UNUSED(tester);

    const QList<QPair<QStringList, QString>> cases = {
        {{QStringLiteral("unknown")}, QStringLiteral("unknown")},
        {{QStringLiteral("unknown"), QStringLiteral("present")}, QStringLiteral("present")},
        {{QStringLiteral("present"), QStringLiteral("stale")}, QStringLiteral("stale")},
        {{QStringLiteral("unknown"), QStringLiteral("present"), QStringLiteral("stale"),
          QStringLiteral("missing")},
         QStringLiteral("missing")},
    };
    for (const auto &testCase : cases) {
        model.setSnapshot(makeSnapshot(testCase.first));
        const auto index = model.index(0, buildscope::CompilationTreeModel::SourceColumn);
        QVERIFY(index.isValid());
        QCOMPARE(index.data(buildscope::SourceStatusRole).toString(), testCase.second);
        QCOMPARE(displayData(index, buildscope::CompilationTreeModel::StatusColumn).toString(),
                 testCase.second);
    }
}

QTEST_MAIN(CompilationModelTest)

#include "test_compilation_model.moc"
