#include "buildscope/main_window.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeView>
#include <QTreeWidget>
#include <QTimer>
#include <QtTest>

#include "buildscope/compilation_model.hpp"

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
    void loadsSampleSnapshot();
    void reportsMissingSnapshot();
    void openButtonLoadsSelectedSnapshot();
    void destroysThroughWidgetPointer();
    void loadsV2SnapshotAndBuildsTree();
    void populatesV2DetailsAutomatically();
    void filtersV2SourcesAndStructuredFields();
    void reportsMalformedV2ValidationLocation();
};

void MainWindowTest::initTestCase() {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
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

QTEST_MAIN(MainWindowTest)

#include "test_main_window.moc"
