#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>

#include <cstddef>
#include <string>
#include <vector>

#include "loglens/gui/log_model.hpp"
#include "loglens/gui/main_window.hpp"
#include "loglens/log_parser.hpp"
#include "loglens/persistence.hpp"

namespace {

QComboBox* sourceProfile(MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("sourceProfileComboBox"));
}

QComboBox* sourceFormat(MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("sourceFormatComboBox"));
}

QComboBox* multilinePolicy(MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("multilinePolicyComboBox"));
}

QSpinBox* maxRecordBytes(MainWindow& window) {
    return window.findChild<QSpinBox*>(QStringLiteral("maxRecordBytesSpinBox"));
}

QComboBox* savedQuery(MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("savedQueryComboBox"));
}

QLineEdit* filterEdit(MainWindow& window) {
    return window.findChild<QLineEdit*>(QStringLiteral("filterEdit"));
}

QTableView* table(MainWindow& window) {
    return window.findChild<QTableView*>(QStringLiteral("logTable"));
}

QLabel* status(MainWindow& window) {
    return window.findChild<QLabel*>(QStringLiteral("statusLabel"));
}

int rows(MainWindow& window) {
    QTableView* view = table(window);
    return view == nullptr || view->model() == nullptr ? -1 : view->model()->rowCount();
}

QString level(MainWindow& window, int row) {
    QTableView* view = table(window);
    if (view == nullptr || view->model() == nullptr) {
        return QString();
    }
    return view->model()->data(view->model()->index(row, LogModel::ColumnLevel),
                               Qt::DisplayRole)
        .toString();
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), static_cast<qint64>(bytes.size()));
    QVERIFY(file.flush());
}

MainWindowOptions optionsFor(const QTemporaryDir& directory) {
    MainWindowOptions options;
    options.recordCapacity = 64;
    options.sourceChunkBytes = 7;
    options.sourceProfilesPath = directory.filePath(QStringLiteral("profiles.json"));
    options.savedQueriesPath = directory.filePath(QStringLiteral("queries.json"));
    return options;
}

QByteArray jsonLog() {
    return QByteArray("{\"ts\":\"2026-08-26T04:15:22.000Z\",\"level\":\"error\","
                      "\"logger\":\"api\",\"msg\":\"failed\"}\n"
                      "  detail\n"
                      "{\"ts\":\"2026-08-26T04:15:23.000Z\",\"level\":\"info\","
                      "\"logger\":\"api\",\"msg\":\"ok\"}\n");
}

QByteArray isoLog() {
    return QByteArray("2026-08-26T04:15:22.000Z ERROR [api] failed\n"
                      "2026-08-26T04:15:23.000Z INFO  [api] ok\n");
}

} // namespace

class TestGuiPersistence : public QObject {
    Q_OBJECT

private slots:
    void loadsProfilesAndQueriesAndAppliesTheirSemantics();
    void savesEditedProfilesAndQueriesAndAppliesTheSavedQuery();
    void malformedStoresBecomeVisibleErrorsWithoutPartialItems();
    void persistedItemLimitIsEnforcedBeforeGuiStateGrows();
};

void TestGuiPersistence::loadsProfilesAndQueriesAndAppliesTheirSemantics() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const MainWindowOptions options = optionsFor(directory);
    loglens::PersistenceError error;
    const loglens::SourceProfile profile{
        "json-separate", loglens::Format::JsonLine,
        loglens::MultilinePolicy::SeparateLines, 256};
    const loglens::SavedQuery query{"errors", "level>=ERROR"};
    QVERIFY(loglens::saveSourceProfiles(options.sourceProfilesPath.toStdString(), {profile}, error));
    QVERIFY(loglens::saveSavedQueries(options.savedQueriesPath.toStdString(), {query}, error));

    MainWindow window(nullptr, options);
    QComboBox* profiles = sourceProfile(window);
    QComboBox* formats = sourceFormat(window);
    QComboBox* multiline = multilinePolicy(window);
    QSpinBox* recordLimit = maxRecordBytes(window);
    QComboBox* queries = savedQuery(window);
    QVERIFY(profiles != nullptr);
    QVERIFY(formats != nullptr);
    QVERIFY(multiline != nullptr);
    QVERIFY(recordLimit != nullptr);
    QVERIFY(queries != nullptr);
    QCOMPARE(profiles->count(), 1);
    QCOMPARE(profiles->currentText(), QStringLiteral("json-separate"));
    QCOMPARE(formats->currentData().toString(), QStringLiteral("jsonl"));
    QCOMPARE(multiline->currentData().toString(), QStringLiteral("separate-lines"));
    QCOMPARE(recordLimit->value(), 256);
    QCOMPARE(queries->count(), 1);
    QCOMPARE(queries->currentText(), QStringLiteral("errors"));

    const QString path = directory.filePath(QStringLiteral("input.log"));
    writeFile(path, jsonLog());
    window.openPath(path, loglens::InitialLoadMode::TailRecords, 3);

    // Separate-lines is observable: the indented physical line remains its
    // own row instead of extending the preceding JSON record.
    QTRY_COMPARE_WITH_TIMEOUT(rows(window), 3, 2500);
    multiline->setCurrentIndex(multiline->findData(QStringLiteral("fold-continuations")));
    QVERIFY(QMetaObject::invokeMethod(&window, "applySourceProfile", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(rows(window), 2, 2500);
    multiline->setCurrentIndex(multiline->findData(QStringLiteral("separate-lines")));
    QVERIFY(QMetaObject::invokeMethod(&window, "applySourceProfile", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(rows(window), 3, 2500);
    QVERIFY(QMetaObject::invokeMethod(&window, "applySavedQuery", Qt::DirectConnection));
    QTRY_COMPARE(rows(window), 1);
    QCOMPARE(level(window, 0), QStringLiteral("ERROR"));
    QCOMPARE(filterEdit(window)->text(), QStringLiteral("level>=ERROR"));
}

void TestGuiPersistence::savesEditedProfilesAndQueriesAndAppliesTheSavedQuery() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const MainWindowOptions options = optionsFor(directory);
    MainWindow window(nullptr, options);

    QComboBox* profiles = sourceProfile(window);
    QComboBox* formats = sourceFormat(window);
    QComboBox* multiline = multilinePolicy(window);
    QSpinBox* recordLimit = maxRecordBytes(window);
    QComboBox* queries = savedQuery(window);
    QVERIFY(profiles != nullptr);
    QVERIFY(formats != nullptr);
    QVERIFY(multiline != nullptr);
    QVERIFY(recordLimit != nullptr);
    QVERIFY(queries != nullptr);

    profiles->setEditText(QStringLiteral("iso-profile"));
    formats->setCurrentIndex(formats->findData(QStringLiteral("iso")));
    multiline->setCurrentIndex(multiline->findData(QStringLiteral("fold-continuations")));
    recordLimit->setValue(128);
    QVERIFY(QMetaObject::invokeMethod(&window, "saveSourceProfile", Qt::DirectConnection));
    const loglens::SourceProfileLoadResult loadedProfiles =
        loglens::loadSourceProfiles(options.sourceProfilesPath.toStdString());
    QVERIFY(loadedProfiles.ok());
    QCOMPARE(loadedProfiles.profiles.size(), static_cast<std::size_t>(1));
    QCOMPARE(loadedProfiles.profiles[0].name, std::string("iso-profile"));
    QCOMPARE(loadedProfiles.profiles[0].format, loglens::Format::PlainIso);

    const QString path = directory.filePath(QStringLiteral("input.log"));
    writeFile(path, isoLog());
    window.openPath(path, loglens::InitialLoadMode::FromStart, 64);
    QTRY_COMPARE_WITH_TIMEOUT(rows(window), 2, 2500);

    queries->setEditText(QStringLiteral("errors"));
    filterEdit(window)->setText(QStringLiteral("level>=ERROR"));
    QVERIFY(QMetaObject::invokeMethod(&window, "saveSavedQuery", Qt::DirectConnection));
    const loglens::SavedQueryLoadResult loadedQueries =
        loglens::loadSavedQueries(options.savedQueriesPath.toStdString());
    QVERIFY(loadedQueries.ok());
    QCOMPARE(loadedQueries.queries.size(), static_cast<std::size_t>(1));
    QCOMPARE(loadedQueries.queries[0].name, std::string("errors"));

    filterEdit(window)->setText(QStringLiteral("message~missing"));
    QVERIFY(QMetaObject::invokeMethod(&window, "applyFilter", Qt::DirectConnection));
    QCOMPARE(rows(window), 0);
    QVERIFY(QMetaObject::invokeMethod(&window, "applySavedQuery", Qt::DirectConnection));
    QTRY_COMPARE(rows(window), 1);
    QCOMPARE(level(window, 0), QStringLiteral("ERROR"));
    QVERIFY(status(window)->text().contains(QStringLiteral("Applied saved query 'errors'")));
}

void TestGuiPersistence::malformedStoresBecomeVisibleErrorsWithoutPartialItems() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const MainWindowOptions options = optionsFor(directory);
    writeFile(options.sourceProfilesPath, QByteArray("{}"));
    writeFile(options.savedQueriesPath, QByteArray("{}"));

    MainWindow window(nullptr, options);
    QVERIFY(sourceProfile(window) != nullptr);
    QVERIFY(savedQuery(window) != nullptr);
    QCOMPARE(sourceProfile(window)->count(), 0);
    QCOMPARE(savedQuery(window)->count(), 0);
    QVERIFY(status(window)->text().contains(QStringLiteral("Cannot load source profiles")));
    QVERIFY(status(window)->text().contains(QStringLiteral("Cannot load saved queries")));
    QVERIFY(status(window)->text().contains(QStringLiteral("malformed")));

    // A failed save reports a bounded validation error and does not invent an
    // item in the editable combo.
    sourceProfile(window)->setEditText(QString());
    QVERIFY(QMetaObject::invokeMethod(&window, "saveSourceProfile", Qt::DirectConnection));
    QVERIFY(status(window)->text().contains(QStringLiteral("Cannot save source profile")));
    QCOMPARE(sourceProfile(window)->count(), 0);
}

void TestGuiPersistence::persistedItemLimitIsEnforcedBeforeGuiStateGrows() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const MainWindowOptions options = optionsFor(directory);
    std::vector<loglens::SavedQuery> stored;
    stored.reserve(loglens::kMaxPersistedItems);
    for (std::size_t index = 0; index < loglens::kMaxPersistedItems; ++index) {
        stored.push_back(loglens::SavedQuery{"query-" + std::to_string(index), "message~ok"});
    }
    loglens::PersistenceError error;
    QVERIFY(loglens::saveSavedQueries(options.savedQueriesPath.toStdString(), stored, error));

    MainWindow window(nullptr, options);
    QComboBox* profiles = sourceProfile(window);
    QComboBox* queries = savedQuery(window);
    QSpinBox* recordLimit = maxRecordBytes(window);
    QVERIFY(profiles != nullptr);
    QVERIFY(queries != nullptr);
    QVERIFY(recordLimit != nullptr);
    QCOMPARE(queries->count(), static_cast<int>(loglens::kMaxPersistedItems));
    QCOMPARE(queries->maxCount(), static_cast<int>(loglens::kMaxPersistedItems));
    QCOMPARE(profiles->maxCount(), static_cast<int>(loglens::kMaxPersistedItems));
    QCOMPARE(recordLimit->maximum(), static_cast<int>(loglens::kMaxRecordBytes));

    queries->setEditText(QStringLiteral("query-overflow"));
    filterEdit(window)->setText(QStringLiteral("message~ok"));
    QVERIFY(QMetaObject::invokeMethod(&window, "saveSavedQuery", Qt::DirectConnection));
    QCOMPARE(queries->count(), static_cast<int>(loglens::kMaxPersistedItems));
    QVERIFY(status(window)->text().contains(QStringLiteral("128-item limit")));
    const loglens::SavedQueryLoadResult reloaded =
        loglens::loadSavedQueries(options.savedQueriesPath.toStdString());
    QVERIFY(reloaded.ok());
    QCOMPARE(reloaded.queries.size(), loglens::kMaxPersistedItems);
}

QTEST_MAIN(TestGuiPersistence)
#include "test_gui_persistence.moc"
