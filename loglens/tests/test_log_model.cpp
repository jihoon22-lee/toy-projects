#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

#include <cstddef>
#include <cstdint>
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

loglens::LogRecord numberedRecord(Level level, std::size_t line, const char* message) {
    loglens::LogRecord record = makeRecord(level, "svc", message);
    record.line_number = line;
    return record;
}

} // namespace

class TestLogModel : public QObject {
    Q_OBJECT

private slots:
    void appendWithoutFilterInsertsAtTheEnd();
    void appendWithFilterInsertsOnlyMatches();
    void appendWithFilterPreservesBackingIndexes();
    void appendThatMatchesNothingEmitsNoInsert();
    void resetClearsEverything();
    void updateVisibleRecordEmitsDataChanged();
    void updateRespectsFilterMembership();
    void boundedAppendEmitsRemovalAndInsertionForRepeatedEviction();
    void appendBatchCrossingFullCapacityEmitsContiguousRanges();
    void filteredEvictionRemovesVisibleRowsForNonMatchingArrivals();
    void oversizedBatchRetainsOnlyItsSuffix();
    void staleGenerationOrIndexAppendIsIgnored();
    void evictedExtensionIsIgnored();
    void retainedUpdatesRespectFilterAfterWrap();
    void omittedBytesAreVisibleInMessageAndTooltip();
    void caseInsensitiveRawSearchComposesWithStructuredFilter();
    void searchMembershipSurvivesAppendUpdateAndEviction();
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

void TestLogModel::appendWithFilterPreservesBackingIndexes() {
    LogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    const loglens::Filter filter = errorsOnly();
    model.setFilter(&filter);

    const std::vector<loglens::LogRecord> incoming = {
        makeRecord(Level::Debug, "svc", "leading non-match"),
        makeRecord(Level::Error, "svc", "first match"),
        makeRecord(Level::Fatal, "svc", "second match"),
        makeRecord(Level::Info, "svc", "trailing non-match"),
    };
    model.appendRecords(incoming);

    QCOMPARE(model.rowCount(), 2);
    const loglens::LogRecord* first = model.recordAt(0);
    const loglens::LogRecord* second = model.recordAt(1);
    QVERIFY(first != nullptr);
    QVERIFY(second != nullptr);
    QVERIFY(first->level == Level::Error);
    QCOMPARE(first->message, std::string("first match"));
    QVERIFY(second->level == Level::Fatal);
    QCOMPARE(second->message, std::string("second match"));
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

void TestLogModel::updateVisibleRecordEmitsDataChanged() {
    LogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    model.setRecords(batch({Level::Error}));
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
    loglens::LogRecord updated = makeRecord(Level::Fatal, "svc", "boom\n  detail");
    updated.line_number = 1;
    model.updateRecord(0, updated);

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.recordAt(0)->message, std::string("boom\n  detail"));
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(1));
}

void TestLogModel::updateRespectsFilterMembership() {
    LogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.setRecords(batch({Level::Info, Level::Error}));
    const loglens::Filter filter = errorsOnly();
    model.setFilter(&filter);
    QCOMPARE(model.rowCount(), 1);

    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    model.updateRecord(0, makeRecord(Level::Fatal, "svc", "now visible"));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(inserted.count(), 1);

    model.updateRecord(1, makeRecord(Level::Info, "svc", "now hidden"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(model.recordAt(0)->message, std::string("now visible"));
}

void TestLogModel::boundedAppendEmitsRemovalAndInsertionForRepeatedEviction() {
    LogModel model(nullptr, 2);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.resetRecords(7);

    model.appendRecords(
        {numberedRecord(Level::Info, 1, "one"), numberedRecord(Level::Info, 2, "two")}, 0, 7);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(2));
    QCOMPARE(model.droppedCount(), static_cast<std::size_t>(0));
    QVERIFY(model.oldestLine().has_value());
    QVERIFY(model.newestLine().has_value());
    QCOMPARE(*model.oldestLine(), static_cast<std::size_t>(1));
    QCOMPARE(*model.newestLine(), static_cast<std::size_t>(2));

    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);

    model.appendRecords({numberedRecord(Level::Info, 3, "three")}, 2, 7);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(removed.at(0).at(1).toInt(), 0);
    QCOMPARE(removed.at(0).at(2).toInt(), 0);
    QCOMPARE(inserted.at(0).at(1).toInt(), 1);
    QCOMPARE(inserted.at(0).at(2).toInt(), 1);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(2));
    QCOMPARE(model.recordAt(1)->line_number, static_cast<std::size_t>(3));
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(3));
    QCOMPARE(model.droppedCount(), static_cast<std::size_t>(1));
    QCOMPARE(*model.oldestLine(), static_cast<std::size_t>(2));
    QCOMPARE(*model.newestLine(), static_cast<std::size_t>(3));

    model.appendRecords({numberedRecord(Level::Info, 4, "four")}, 3, 7);
    QCOMPARE(removed.count(), 2);
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(3));
    QCOMPARE(model.recordAt(1)->line_number, static_cast<std::size_t>(4));
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(4));
    QCOMPARE(model.droppedCount(), static_cast<std::size_t>(2));
    QCOMPARE(*model.oldestLine(), static_cast<std::size_t>(3));
    QCOMPARE(*model.newestLine(), static_cast<std::size_t>(4));
    QCOMPARE(model.generation(), static_cast<std::uint64_t>(7));
}

void TestLogModel::appendBatchCrossingFullCapacityEmitsContiguousRanges() {
    LogModel model(nullptr, 3);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.resetRecords(17);

    model.appendRecords(
        {numberedRecord(Level::Info, 1, "one"), numberedRecord(Level::Info, 2, "two"),
         numberedRecord(Level::Info, 3, "three")},
        0, 17);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(3));
    QCOMPARE(model.droppedCount(), static_cast<std::size_t>(0));

    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    model.appendRecords(
        {numberedRecord(Level::Info, 4, "four"), numberedRecord(Level::Info, 5, "five")},
        3, 17);

    // A single batch evicts the first two visible rows and appends both new
    // records. Each change must be announced as one contiguous range.
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.at(0).at(1).toInt(), 0);
    QCOMPARE(removed.at(0).at(2).toInt(), 1);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.at(0).at(1).toInt(), 1);
    QCOMPARE(inserted.at(0).at(2).toInt(), 2);

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(3));
    QCOMPARE(model.recordAt(1)->line_number, static_cast<std::size_t>(4));
    QCOMPARE(model.recordAt(2)->line_number, static_cast<std::size_t>(5));
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(5));
    QCOMPARE(model.droppedCount(), static_cast<std::size_t>(2));
}

void TestLogModel::filteredEvictionRemovesVisibleRowsForNonMatchingArrivals() {
    LogModel model(nullptr, 2);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.resetRecords(3);
    const loglens::Filter filter = errorsOnly();
    model.setFilter(&filter);

    model.appendRecords(
        {numberedRecord(Level::Error, 1, "one"), numberedRecord(Level::Error, 2, "two")}, 0, 3);
    QCOMPARE(model.rowCount(), 2);

    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    model.appendRecords({numberedRecord(Level::Info, 3, "not visible")}, 2, 3);

    // The arriving record does not match, but line 1 was evicted and must still
    // be removed from the visible index vector.
    QCOMPARE(removed.count(), 1);
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(removed.at(0).at(1).toInt(), 0);
    QCOMPARE(removed.at(0).at(2).toInt(), 0);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(2));
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(3));
    QCOMPARE(model.droppedCount(), static_cast<std::size_t>(1));

    model.appendRecords({numberedRecord(Level::Info, 4, "also hidden")}, 3, 3);
    QCOMPARE(removed.count(), 2);
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(4));
    QCOMPARE(model.droppedCount(), static_cast<std::size_t>(2));
}

void TestLogModel::oversizedBatchRetainsOnlyItsSuffix() {
    LogModel model(nullptr, 2);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.resetRecords(11);

    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    model.appendRecords(
        {numberedRecord(Level::Info, 1, "one"), numberedRecord(Level::Info, 2, "two"),
         numberedRecord(Level::Info, 3, "three"), numberedRecord(Level::Info, 4, "four"),
         numberedRecord(Level::Info, 5, "five")},
        0, 11);

    QCOMPARE(removed.count(), 0);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.at(0).at(1).toInt(), 0);
    QCOMPARE(inserted.at(0).at(2).toInt(), 1);
    QCOMPARE(model.capacity(), static_cast<std::size_t>(2));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.totalCount(), 2);
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(5));
    QCOMPARE(model.droppedCount(), static_cast<std::size_t>(3));
    QCOMPARE(*model.oldestLine(), static_cast<std::size_t>(4));
    QCOMPARE(*model.newestLine(), static_cast<std::size_t>(5));
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(4));
    QCOMPARE(model.recordAt(1)->line_number, static_cast<std::size_t>(5));
    QCOMPARE(model.generation(), static_cast<std::uint64_t>(11));
}

void TestLogModel::staleGenerationOrIndexAppendIsIgnored() {
    LogModel model(nullptr, 2);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.resetRecords(5);

    model.appendRecords({numberedRecord(Level::Info, 1, "one")}, 0, 5);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);

    model.appendRecords({numberedRecord(Level::Info, 2, "wrong index")}, 0, 5);
    model.appendRecords({numberedRecord(Level::Info, 2, "wrong generation")}, 1, 6);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(1));
    QCOMPARE(model.droppedCount(), static_cast<std::size_t>(0));
    QCOMPARE(model.generation(), static_cast<std::uint64_t>(5));
    QCOMPARE(model.recordAt(0)->message, std::string("one"));

    model.appendRecords({numberedRecord(Level::Info, 2, "accepted")}, 1, 5);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(2));
    QCOMPARE(model.recordAt(1)->message, std::string("accepted"));
}

void TestLogModel::evictedExtensionIsIgnored() {
    LogModel model(nullptr, 2);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.resetRecords(9);

    model.appendRecords(
        {numberedRecord(Level::Error, 1, "first"), numberedRecord(Level::Info, 2, "second")},
        0, 9);
    model.appendRecords({numberedRecord(Level::Info, 3, "third")}, 2, 9);
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(3));
    QCOMPARE(model.droppedCount(), static_cast<std::size_t>(1));

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    model.updateRecord(0, numberedRecord(Level::Fatal, 1, "evicted extension"), 9);

    QCOMPARE(changed.count(), 0);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.recordAt(0)->message, std::string("second"));
    QCOMPARE(model.recordAt(1)->message, std::string("third"));
}

void TestLogModel::retainedUpdatesRespectFilterAfterWrap() {
    LogModel model(nullptr, 2);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.resetRecords(13);
    const loglens::Filter filter = errorsOnly();

    model.appendRecords(
        {numberedRecord(Level::Error, 1, "evicted"), numberedRecord(Level::Info, 2, "hidden"),
         numberedRecord(Level::Error, 3, "visible")},
        0, 13);
    model.setFilter(&filter);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(3));

    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    // Absolute index 1 is retained but initially filtered out, so it enters at
    // the front of the visible set before absolute index 2.
    model.updateRecord(1, numberedRecord(Level::Error, 2, "now visible"), 13);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.at(0).at(1).toInt(), 0);
    QCOMPARE(inserted.at(0).at(2).toInt(), 0);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(2));
    QCOMPARE(model.recordAt(1)->line_number, static_cast<std::size_t>(3));

    // The other retained record leaves the filter and is removed from row 1.
    model.updateRecord(2, numberedRecord(Level::Info, 3, "now hidden"), 13);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.at(0).at(1).toInt(), 1);
    QCOMPARE(removed.at(0).at(2).toInt(), 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(2));

    // A retained visible update changes data without changing membership.
    model.updateRecord(1, numberedRecord(Level::Fatal, 2, "still visible"), 13);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.recordAt(0)->level, Level::Fatal);
    QCOMPARE(model.recordAt(0)->message, std::string("still visible"));
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(3));
    QCOMPARE(model.droppedCount(), static_cast<std::size_t>(1));
    QCOMPARE(*model.oldestLine(), static_cast<std::size_t>(2));
    QCOMPARE(*model.newestLine(), static_cast<std::size_t>(3));
}

void TestLogModel::omittedBytesAreVisibleInMessageAndTooltip() {
    LogModel model;
    loglens::LogRecord record = numberedRecord(Level::Warn, 8, "retained prefix");
    record.raw = "raw retained prefix";
    record.input_bytes = record.raw.size() + 17;
    record.omitted_bytes = 17;
    model.setRecords({record});

    const QModelIndex message = model.index(0, LogModel::ColumnMessage);
    QCOMPARE(model.data(message, Qt::DisplayRole).toString(),
             QStringLiteral("retained prefix  [17 source byte(s) omitted]"));
    QCOMPARE(model.data(message, Qt::ToolTipRole).toString(),
             QStringLiteral("raw retained prefix\n[17 source byte(s) omitted]"));
}

void TestLogModel::caseInsensitiveRawSearchComposesWithStructuredFilter() {
    LogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    loglens::LogRecord errorMatch = numberedRecord(Level::Error, 1, "completed request");
    errorMatch.raw = "2026-08-26 ERROR [api] TIMEOUT from upstream";
    loglens::LogRecord infoMatch = numberedRecord(Level::Info, 2, "completed request");
    infoMatch.raw = "2026-08-26 INFO [api] timeout from upstream";
    loglens::LogRecord errorMiss = numberedRecord(Level::Error, 3, "connection reset");
    errorMiss.raw = "2026-08-26 ERROR [api] connection reset";
    model.setRecords({errorMatch, infoMatch, errorMiss});

    const loglens::Filter filter = errorsOnly();
    model.setFilter(&filter);
    model.setSearch(QStringLiteral("TiMeOuT"));

    // Search is against raw source text and is case-insensitive. The
    // structured predicate still applies, so the INFO timeout is excluded.
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.recordAt(0) != nullptr);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(1));

    model.setSearch(QString());
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(1));
    QCOMPARE(model.recordAt(1)->line_number, static_cast<std::size_t>(3));
}

void TestLogModel::searchMembershipSurvivesAppendUpdateAndEviction() {
    LogModel model(nullptr, 3);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.resetRecords(4);
    model.setSearch(QStringLiteral("needle"));

    model.appendRecords(
        {numberedRecord(Level::Info, 1, "NEEDLE first"),
         numberedRecord(Level::Info, 2, "ordinary"),
         numberedRecord(Level::Error, 3, "needle second")},
        0, 4);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(1));
    QCOMPARE(model.recordAt(1)->line_number, static_cast<std::size_t>(3));

    // An update can enter the search result without changing its absolute
    // logical record ID or its position relative to other visible records.
    model.updateRecord(1, numberedRecord(Level::Warn, 2, "NeEdLe updated"), 4);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(1));
    QCOMPARE(model.recordAt(1)->line_number, static_cast<std::size_t>(2));
    QCOMPARE(model.recordAt(2)->line_number, static_cast<std::size_t>(3));

    // A non-matching append still evicts the oldest matching row from the
    // visible index. The next non-matching append evicts the next match.
    model.appendRecords({numberedRecord(Level::Info, 4, "ordinary tail")}, 3, 4);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(2));
    QCOMPARE(model.recordAt(1)->line_number, static_cast<std::size_t>(3));

    model.appendRecords({numberedRecord(Level::Info, 5, "another ordinary tail")}, 4, 4);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.recordAt(0)->line_number, static_cast<std::size_t>(3));
    QCOMPARE(model.totalSeen(), static_cast<std::size_t>(5));
    QCOMPARE(model.droppedCount(), static_cast<std::size_t>(2));
}

QTEST_MAIN(TestLogModel)
#include "test_log_model.moc"
