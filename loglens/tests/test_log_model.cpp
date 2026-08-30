#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

#include <initializer_list>
#include <optional>
#include <vector>

#include "fake_source.hpp"
#include "loglens/gui/log_model.hpp"
#include "loglens/filter_expr.hpp"

using loglens::Level;

namespace {

std::vector<loglens::LogRecord> batch(std::initializer_list<Level> levels) {
    std::vector<loglens::LogRecord> records;
    for (Level level : levels) {
        records.push_back(makeRecord(level, "svc", "message"));
    }
    return records;
}

loglens::Filter errorsOnly() {
    loglens::ParseError error;
    std::optional<loglens::Filter> filter = loglens::Filter::parse("level >= error", error);
    Q_ASSERT(filter.has_value());
    return *filter;
}

} // namespace

class TestLogModel : public QObject {
    Q_OBJECT

private slots:
    void appendWithoutFilterInsertsAtTheEnd();
    void appendWithFilterInsertsOnlyMatches();
    void appendThatMatchesNothingEmitsNoInsert();
    void resetClearsEverything();
};

// QAbstractItemModelTester asserts the whole QAbstractItemModel contract on
// every signal: begin/end pairing, row counts that agree with the ranges
// announced, index validity. It is the reason this test cannot live in core —
// there is no pure function to extract, and the class does not link without moc.
void TestLogModel::appendWithoutFilterInsertsAtTheEnd() {
    LogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    model.setRecords(batch({Level::Info, Level::Warn}));
    QCOMPARE(model.rowCount(), 2);

    QSignalSpy spy(&model, &QAbstractItemModel::rowsInserted);
    model.appendRecords(batch({Level::Error}));

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(spy.count(), 1);
    // Appends land at the end, so the inserted rows are always one contiguous
    // range — first == old row count, last == new row count - 1.
    QCOMPARE(spy.at(0).at(1).toInt(), 2);
    QCOMPARE(spy.at(0).at(2).toInt(), 2);
}

void TestLogModel::appendWithFilterInsertsOnlyMatches() {
    LogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    model.setRecords(batch({Level::Info, Level::Error}));
    const loglens::Filter filter = errorsOnly();
    model.setFilter(&filter);
    QCOMPARE(model.rowCount(), 1);

    QSignalSpy spy(&model, &QAbstractItemModel::rowsInserted);
    model.appendRecords(batch({Level::Debug, Level::Fatal, Level::Info}));

    // Only Fatal passes `level >= error`, so one row appears even though three
    // records arrived. The model still holds all five.
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.totalCount(), 5);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toInt(), 1);
    QCOMPARE(spy.at(0).at(2).toInt(), 1);
}

void TestLogModel::appendThatMatchesNothingEmitsNoInsert() {
    LogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    const loglens::Filter filter = errorsOnly();
    model.setFilter(&filter);

    QSignalSpy spy(&model, &QAbstractItemModel::rowsInserted);
    model.appendRecords(batch({Level::Info, Level::Debug}));

    // beginInsertRows with an empty range is a contract violation, so the model
    // must not announce an insert it is not making.
    QCOMPARE(spy.count(), 0);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.totalCount(), 2);
}

void TestLogModel::resetClearsEverything() {
    LogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    model.setRecords(batch({Level::Info, Level::Error}));
    QSignalSpy spy(&model, &QAbstractItemModel::modelReset);

    model.resetRecords();

    // Rotation and truncation drop the old content entirely; a reset is the
    // only honest signal for that.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.totalCount(), 0);
}

QTEST_MAIN(TestLogModel)
#include "test_log_model.moc"
