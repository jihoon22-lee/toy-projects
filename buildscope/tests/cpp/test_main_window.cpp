#include "buildscope/main_window.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QPushButton>
#include <QTimer>
#include <QtTest>

class MainWindowTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void loadsSampleSnapshot();
    void reportsMissingSnapshot();
    void openButtonLoadsSelectedSnapshot();
    void destroysThroughWidgetPointer();
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

QTEST_MAIN(MainWindowTest)

#include "test_main_window.moc"
