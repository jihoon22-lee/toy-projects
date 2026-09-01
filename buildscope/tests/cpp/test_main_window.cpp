#include "buildscope/main_window.hpp"

#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeView>
#include <QTreeWidget>
#include <QTimer>
#include <QtTest>

#include "buildscope/compilation_model.hpp"
#include "buildscope/diff_model.hpp"

namespace {

template <typename Widget>
Widget *findWidget(QObject *root, const char *name) {
    return root->findChild<Widget *>(QString::fromLatin1(name));
}

void processGuiEvents() {
    QApplication::processEvents();
}

}  // namespace

class MainWindowTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void generatedQtArtifactsAreLinked();
    void loadsSampleSnapshot();
    void reportsMissingSnapshot();
    void openButtonLoadsSelectedSnapshot();
    void destroysThroughWidgetPointer();
    void loadsV2SnapshotAndBuildsTree();
    void populatesV2DetailsAutomatically();
    void populatesV3IncludeExplanation();
    void filtersV2SourcesAndStructuredFields();
    void reportsMalformedV2ValidationLocation();
    void loadsDiffReportAndShowsIssuesFirstDetails();
    void failedSnapshotClearsDiffMode();
};

void MainWindowTest::initTestCase() {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
}

void MainWindowTest::cleanup() {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void MainWindowTest::generatedQtArtifactsAreLinked() {
    buildscope::MainWindow window;

    QVERIFY(findWidget<QPushButton>(&window, "openButton") != nullptr);
    QVERIFY(QFile::exists(QStringLiteral(":/icons/buildscope.svg")));
    QVERIFY(window.metaObject()->indexOfSlot("chooseSnapshot()") >= 0);
}

void MainWindowTest::loadsSampleSnapshot() {
    buildscope::MainWindow window;

    QVERIFY(window.loadSnapshot(QStringLiteral(BUILDSCOPE_SAMPLE_SNAPSHOT)));
    QCOMPARE(window.entryCount(), 2);
    QVERIFY(window.statusText().contains(QStringLiteral("buildscope.snapshot/v1")));
}

void MainWindowTest::reportsMissingSnapshot() {
    buildscope::MainWindow window;

    QVERIFY(!window.loadSnapshot(QStringLiteral("missing-snapshot.json")));
    QCOMPARE(window.entryCount(), 0);
    QVERIFY(window.statusText().startsWith(QStringLiteral("Could not load snapshot:")));
}

void MainWindowTest::openButtonLoadsSelectedSnapshot() {
    buildscope::MainWindow window;
    auto *button = window.findChild<QPushButton *>(QStringLiteral("openButton"));
    QVERIFY(button != nullptr);
    QTimer::singleShot(0, [] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QFileDialog *>(widget)) {
                dialog->selectFile(QStringLiteral(BUILDSCOPE_SAMPLE_SNAPSHOT));
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        }
    });

    QTest::mouseClick(button, Qt::LeftButton);

    QCOMPARE(window.entryCount(), 2);
}

void MainWindowTest::destroysThroughWidgetPointer() {
    QWidget *window = new buildscope::MainWindow;
    delete window;
}

void MainWindowTest::loadsV2SnapshotAndBuildsTree() {
    buildscope::MainWindow window;

    QVERIFY(window.loadSnapshot(QStringLiteral(BUILDSCOPE_V2_SNAPSHOT)));
    QCOMPARE(window.entryCount(), 2);
    QVERIFY(window.statusText().contains(QStringLiteral("buildscope.snapshot/v2")));
    QVERIFY(window.statusText().contains(QStringLiteral("producer 0.3.0")));

    auto *tree = findWidget<QTreeView>(&window, "sourceTree");
    QVERIFY(tree != nullptr);
    auto *model = tree->model();
    QVERIFY(model != nullptr);
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->columnCount(), buildscope::CompilationTreeModel::ColumnCount);

    const auto present = model->index(0, buildscope::CompilationTreeModel::SourceColumn);
    const auto missing = model->index(1, buildscope::CompilationTreeModel::SourceColumn);
    QVERIFY(present.isValid());
    QVERIFY(missing.isValid());
    QCOMPARE(present.data(Qt::DisplayRole).toString(), QStringLiteral("src/main.cpp (1)"));
    QCOMPARE(missing.data(Qt::DisplayRole).toString(), QStringLiteral("src/missing.cpp (1)"));
    QCOMPARE(present.data(buildscope::NodeKindRole).toInt(),
             static_cast<int>(buildscope::CompilationNodeKind::Source));
    QCOMPARE(missing.data(buildscope::SourceStatusRole).toString(),
             QStringLiteral("missing"));
    QCOMPARE(model->data(model->index(0, buildscope::CompilationTreeModel::StatusColumn),
                         Qt::DisplayRole)
                 .toString(),
             QStringLiteral("present"));
    QCOMPARE(model->data(model->index(1, buildscope::CompilationTreeModel::StatusColumn),
                         Qt::DisplayRole)
                 .toString(),
             QStringLiteral("missing"));
    QCOMPARE(model->rowCount(present), 1);
    QCOMPARE(model->rowCount(missing), 1);
    QVERIFY(tree->currentIndex().isValid());
    QCOMPARE(tree->currentIndex().data(buildscope::SourcePathRole).toString(),
             QStringLiteral("src/main.cpp"));
}

void MainWindowTest::populatesV2DetailsAutomatically() {
    buildscope::MainWindow window;

    QVERIFY(window.loadSnapshot(QStringLiteral(BUILDSCOPE_V2_SNAPSHOT)));
    processGuiEvents();

    auto *selection = findWidget<QLabel>(&window, "selectionLabel");
    auto *source = findWidget<QLabel>(&window, "sourceValue");
    auto *directory = findWidget<QLabel>(&window, "directoryValue");
    auto *sourceStatus = findWidget<QLabel>(&window, "sourceStatusValue");
    auto *target = findWidget<QLabel>(&window, "targetValue");
    auto *compiler = findWidget<QLabel>(&window, "compilerValue");
    auto *standard = findWidget<QLabel>(&window, "standardValue");
    auto *configuration = findWidget<QLabel>(&window, "configurationValue");
    auto *invocationSource = findWidget<QLabel>(&window, "invocationSourceValue");
    auto *arguments = findWidget<QPlainTextEdit>(&window, "argumentsEdit");
    auto *rawCommand = findWidget<QPlainTextEdit>(&window, "rawCommandEdit");
    auto *defines = findWidget<QTableWidget>(&window, "defineTable");
    auto *includes = findWidget<QTableWidget>(&window, "includeTable");
    auto *diagnostics = findWidget<QTreeWidget>(&window, "diagnosticTree");
    QVERIFY(selection != nullptr);
    QVERIFY(source != nullptr);
    QVERIFY(directory != nullptr);
    QVERIFY(sourceStatus != nullptr);
    QVERIFY(target != nullptr);
    QVERIFY(compiler != nullptr);
    QVERIFY(standard != nullptr);
    QVERIFY(configuration != nullptr);
    QVERIFY(invocationSource != nullptr);
    QVERIFY(arguments != nullptr);
    QVERIFY(rawCommand != nullptr);
    QVERIFY(defines != nullptr);
    QVERIFY(includes != nullptr);
    QVERIFY(diagnostics != nullptr);

    QCOMPARE(selection->text(), QStringLiteral("src/main.cpp"));
    QCOMPARE(source->text(), QStringLiteral("src/main.cpp"));
    QCOMPARE(directory->text(), QStringLiteral("build"));
    QCOMPARE(sourceStatus->text(), QStringLiteral("present"));
    QCOMPARE(target->text(), QStringLiteral("buildscope-app · x86_64-linux-gnu"));
    QCOMPARE(compiler->text(), QStringLiteral("c++"));
    QCOMPARE(standard->text(), QStringLiteral("c++20"));
    QCOMPARE(configuration->text(),
             QStringLiteral("sha256:1111111111111111111111111111111111111111111111111111111111111111"));
    QCOMPARE(invocationSource->text(), QStringLiteral("Invocation source: arguments"));

    const auto structured = arguments->toPlainText();
    QCOMPARE(structured,
             QStringLiteral(
                 R"(["/usr/bin/c++","-DFEATURE=hello world","-std=c++20","-Iinclude","-c","src/main.cpp"])"));
    QCOMPARE(rawCommand->toPlainText(),
             QStringLiteral(
                 "/usr/bin/c++ -DFEATURE='hello world' -std=c++20 -Iinclude -c src/main.cpp"));
    QVERIFY(structured != rawCommand->toPlainText());

    QCOMPARE(defines->rowCount(), 1);
    for (int column = 0; column < defines->columnCount(); ++column) {
        QVERIFY(defines->item(0, column) != nullptr);
    }
    QCOMPARE(defines->item(0, 0)->text(), QStringLiteral("define"));
    QCOMPARE(defines->item(0, 1)->text(), QStringLiteral("FEATURE"));
    QCOMPARE(defines->item(0, 2)->text(), QStringLiteral("hello world"));

    QCOMPARE(includes->rowCount(), 1);
    for (int column = 0; column < includes->columnCount(); ++column) {
        QVERIFY(includes->item(0, column) != nullptr);
    }
    QCOMPARE(includes->item(0, 0)->text(), QStringLiteral("0"));
    QCOMPARE(includes->item(0, 1)->text(), QStringLiteral("include"));
    QCOMPARE(includes->item(0, 2)->text(), QStringLiteral("project"));
    QCOMPARE(includes->item(0, 3)->text(), QStringLiteral("yes"));
    QCOMPARE(includes->item(0, 4)->text(), QStringLiteral("include"));

    QCOMPARE(diagnostics->topLevelItemCount(), 1);
    const auto *diagnostic = diagnostics->topLevelItem(0);
    QVERIFY(diagnostic != nullptr);
    QCOMPARE(diagnostic->text(0), QStringLiteral("warning"));
    QCOMPARE(diagnostic->text(1), QStringLiteral("Wshadow"));
    QCOMPARE(diagnostic->text(2), QStringLiteral("declaration shadows a parameter"));
}

void MainWindowTest::populatesV3IncludeExplanation() {
    QFile fixture(QStringLiteral(BUILDSCOPE_V2_SNAPSHOT));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    auto document = QJsonDocument::fromJson(fixture.readAll());
    auto root = document.object();
    root.insert(QStringLiteral("schema_version"), QStringLiteral("buildscope.snapshot/v3"));
    auto entries = root.value(QStringLiteral("entries")).toArray();
    for (qsizetype index = 0; index < entries.size(); ++index) {
        auto entry = entries.at(index).toObject();
        QJsonObject analysis;
        if (index == 0) {
            analysis = QJsonDocument::fromJson(R"json({
              "command":["/usr/bin/c++","-E","-H","src/main.cpp"],
              "diagnostics":[{"code":"trace-note","message":"Measured safely.","severity":"info"}],
              "duration_ms":8,
              "evidence":"compiler-measured",
              "edges":[{
                "alternatives":["include/second/common.hpp"],
                "classification":"project",
                "delimiter":"quote",
                "evidence":"compiler-measured",
                "line":3,
                "location_evidence":"source-scan",
                "parent":"src/main.cpp",
                "requested":"common.hpp",
                "resolved":"include/first/common.hpp",
                "search":[
                  {"candidate":"src/common.hpp","exists":false,"kind":"current","order":0,"selected":false},
                  {"candidate":"include/first/common.hpp","exists":true,"kind":"include","order":1,"selected":true},
                  {"candidate":"include/second/common.hpp","exists":true,"kind":"include","order":2,"selected":false}
                ]
              }]
            })json")
                           .object();
        } else {
            analysis = QJsonDocument::fromJson(R"json({
              "command":[],
              "diagnostics":[{"code":"include-analysis-unavailable","message":"No compiler.","severity":"warning"}],
              "duration_ms":0,
              "edges":[],
              "evidence":"unavailable"
            })json")
                           .object();
        }
        entry.insert(QStringLiteral("include_analysis"), analysis);
        entries.replace(index, entry);
    }
    root.insert(QStringLiteral("entries"), entries);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto path = temporary.filePath(QStringLiteral("sample-v3.json"));
    QFile output(path);
    QVERIFY(output.open(QIODevice::WriteOnly));
    const auto payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QCOMPARE(output.write(payload), payload.size());
    output.close();

    buildscope::MainWindow window;
    QVERIFY(window.loadSnapshot(path));
    processGuiEvents();

    auto *evidence = findWidget<QLabel>(&window, "includeEvidenceLabel");
    auto *tree = findWidget<QTreeWidget>(&window, "includeEdgeTree");
    auto *details = findWidget<QPlainTextEdit>(&window, "includeEdgeEdit");
    auto *replay = findWidget<QPlainTextEdit>(&window, "includeReplayEdit");
    auto *commandButton = findWidget<QPushButton>(&window, "showCommandButton");
    auto *tabs = findWidget<QTabWidget>(&window, "detailTabs");
    auto *commandTab = window.findChild<QWidget *>(QStringLiteral("commandTab"));
    QVERIFY(evidence != nullptr);
    QVERIFY(tree != nullptr);
    QVERIFY(details != nullptr);
    QVERIFY(replay != nullptr);
    QVERIFY(commandButton != nullptr);
    QVERIFY(tabs != nullptr);
    QVERIFY(commandTab != nullptr);
    QVERIFY(window.statusText().contains(QStringLiteral("buildscope.snapshot/v3")));
    QVERIFY(evidence->text().contains(QStringLiteral("compiler-measured")));
    QCOMPARE(tree->topLevelItemCount(), 1);
    auto *edge = tree->topLevelItem(0);
    QVERIFY(edge != nullptr);
    QCOMPARE(edge->text(1), QStringLiteral("common.hpp"));
    QCOMPARE(edge->text(2), QStringLiteral("include/first/common.hpp"));
    QCOMPARE(edge->text(3), QStringLiteral("project"));
    QCOMPARE(edge->childCount(), 3);
    QVERIFY(replay->toPlainText().contains(QStringLiteral("-H")));

    QVERIFY(QMetaObject::invokeMethod(&window, "showIncludeEdge", Qt::DirectConnection,
                                      Q_ARG(QTreeWidgetItem *, edge), Q_ARG(int, 0)));
    QVERIFY(details->toPlainText().contains(QStringLiteral("source-scan")));
    QVERIFY(details->toPlainText().contains(QStringLiteral("second/common.hpp")));
    QTest::mouseClick(commandButton, Qt::LeftButton);
    QCOMPARE(tabs->currentWidget(), commandTab);
}

void MainWindowTest::filtersV2SourcesAndStructuredFields() {
    buildscope::MainWindow window;

    QVERIFY(window.loadSnapshot(QStringLiteral(BUILDSCOPE_V2_SNAPSHOT)));
    auto *filter = findWidget<QLineEdit>(&window, "filterEdit");
    auto *tree = findWidget<QTreeView>(&window, "sourceTree");
    QVERIFY(filter != nullptr);
    QVERIFY(tree != nullptr);
    QVERIFY(tree->model() != nullptr);

    filter->setText(QStringLiteral("missing"));
    processGuiEvents();
    QCOMPARE(tree->model()->rowCount(), 1);
    QCOMPARE(tree->model()->index(0, buildscope::CompilationTreeModel::SourceColumn)
                 .data(buildscope::SourcePathRole)
                 .toString(),
             QStringLiteral("src/missing.cpp"));

    filter->setText(QStringLiteral("src/main.cpp"));
    processGuiEvents();
    QCOMPARE(tree->model()->rowCount(), 1);
    QCOMPARE(tree->model()->index(0, buildscope::CompilationTreeModel::SourceColumn)
                 .data(buildscope::SourcePathRole)
                 .toString(),
             QStringLiteral("src/main.cpp"));

    filter->setText(QStringLiteral("FEATURE"));
    processGuiEvents();
    QCOMPARE(tree->model()->rowCount(), 1);
    QCOMPARE(tree->model()->index(0, buildscope::CompilationTreeModel::SourceColumn)
                 .data(buildscope::SourcePathRole)
                 .toString(),
             QStringLiteral("src/main.cpp"));

    filter->clear();
    processGuiEvents();
    QCOMPARE(tree->model()->rowCount(), 2);
}

void MainWindowTest::reportsMalformedV2ValidationLocation() {
    QFile fixture(QStringLiteral(BUILDSCOPE_V2_SNAPSHOT));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    auto payload = fixture.readAll();
    QVERIFY(payload.contains(QByteArrayLiteral("\"source_status\": \"missing\"")));
    payload.replace(QByteArrayLiteral("\"source_status\": \"missing\""),
                    QByteArrayLiteral("\"source_status\": \"corrupt\""));

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto malformedPath = temporary.filePath(QStringLiteral("malformed-v2.json"));
    QFile malformed(malformedPath);
    QVERIFY(malformed.open(QIODevice::WriteOnly));
    QCOMPARE(malformed.write(payload), payload.size());
    malformed.close();

    buildscope::MainWindow window;
    QVERIFY(!window.loadSnapshot(malformedPath));
    QCOMPARE(window.entryCount(), 0);
    QVERIFY(window.statusText().contains(QStringLiteral("entries[1].state.source_status")));
    auto *selection = findWidget<QLabel>(&window, "selectionLabel");
    QVERIFY(selection != nullptr);
    QVERIFY(selection->text().contains(QStringLiteral("entries[1].state.source_status")));
}

void MainWindowTest::loadsDiffReportAndShowsIssuesFirstDetails() {
    buildscope::MainWindow window;

    QVERIFY(window.loadDiff(QStringLiteral(BUILDSCOPE_SAMPLE_DIFF)));
    QCOMPARE(window.entryCount(), 4);
    QVERIFY(window.statusText().contains(QStringLiteral("4 visible")));
    QVERIFY(window.statusText().contains(QStringLiteral("buildscope.diff/v1")));

    auto *tree = findWidget<QTreeView>(&window, "sourceTree");
    auto *filter = findWidget<QLineEdit>(&window, "filterEdit");
    auto *summary = findWidget<QLabel>(&window, "diffSummaryLabel");
    auto *changes = findWidget<QTableWidget>(&window, "diffChangeTable");
    auto *tabs = findWidget<QTabWidget>(&window, "detailTabs");
    auto *diffTab = window.findChild<QWidget *>(QStringLiteral("diffTab"));
    QVERIFY(tree != nullptr);
    QVERIFY(filter != nullptr);
    QVERIFY(summary != nullptr);
    QVERIFY(changes != nullptr);
    QVERIFY(tabs != nullptr);
    QVERIFY(diffTab != nullptr);
    QCOMPARE(tree->model()->rowCount(), 4);
    QCOMPARE(tree->model()->columnCount(), buildscope::DiffTreeModel::ColumnCount);
    QCOMPARE(tree->model()->index(0, buildscope::DiffTreeModel::SourceColumn)
                 .data(Qt::DisplayRole)
                 .toString(),
             QStringLiteral("src/move.cpp → renamed/move.cpp"));
    QCOMPARE(changes->rowCount(), 2);
    QCOMPARE(changes->item(0, 0)->text(), QStringLiteral("moved"));
    QCOMPARE(tabs->currentWidget(), diffTab);

    const auto changed =
        tree->model()->index(2, buildscope::DiffTreeModel::SourceColumn);
    tree->setCurrentIndex(changed);
    processGuiEvents();
    QCOMPARE(changes->rowCount(), 8);
    QVERIFY(summary->text().contains(QStringLiteral("visible drift")));
    QCOMPARE(changes->item(0, 0)->text(), QStringLiteral("compiler"));

    filter->setText(QStringLiteral("MODE=release"));
    processGuiEvents();
    QCOMPARE(tree->model()->rowCount(), 1);
    QCOMPARE(tree->model()->index(0, 0).data(Qt::DisplayRole).toString(),
             QStringLiteral("src/common.cpp"));
}

void MainWindowTest::failedSnapshotClearsDiffMode() {
    buildscope::MainWindow window;

    QVERIFY(window.loadDiff(QStringLiteral(BUILDSCOPE_SAMPLE_DIFF)));
    QCOMPARE(window.entryCount(), 4);
    QVERIFY(!window.loadSnapshot(QStringLiteral("missing-after-diff.json")));
    QCOMPARE(window.entryCount(), 0);
    QVERIFY(window.statusText().startsWith(QStringLiteral("Could not load snapshot:")));

    auto *tree = findWidget<QTreeView>(&window, "sourceTree");
    QVERIFY(tree != nullptr);
    QCOMPARE(tree->model()->columnCount(), buildscope::CompilationTreeModel::ColumnCount);
}

QTEST_MAIN(MainWindowTest)

#include "test_main_window.moc"
