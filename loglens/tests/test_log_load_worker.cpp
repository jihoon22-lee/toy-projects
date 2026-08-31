#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "loglens/gui/log_load_worker.hpp"

namespace {

QByteArray recordLine(int number) {
    return QByteArray("2026-08-26T04:15:22.000Z INFO [test] record-")
           + QByteArray::number(number) + '\n';
}

void writeFile(const QString& path, const QByteArray& bytes, bool append = false) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly
                      | (append ? QIODevice::Append : QIODevice::Truncate)));
    QCOMPARE(file.write(bytes), static_cast<qint64>(bytes.size()));
    QVERIFY(file.flush());
}

void recreateFile(const QString& path, const QByteArray& bytes) {
    const QString stagingPath = path + QStringLiteral(".staging");
    writeFile(stagingPath, bytes);
    QVERIFY(!QFile::exists(path));
    QVERIFY(QFile::rename(stagingPath, path));
}

QByteArray numberedFile(int count) {
    QByteArray bytes;
    for (int number = 1; number <= count; ++number) {
        bytes += recordLine(number);
    }
    return bytes;
}

loglens::LoadBatch takeBatch(QSignalSpy& batches) {
    const QList<QVariant> arguments = batches.takeFirst();
    return qvariant_cast<loglens::LoadBatch>(arguments.at(0));
}

class WorkerHarness : public QObject {
    Q_OBJECT

public:
    WorkerHarness() {
        qRegisterMetaType<loglens::LoadRequest>("loglens::LoadRequest");
        qRegisterMetaType<loglens::LoadBatch>("loglens::LoadBatch");

        worker_ = new loglens::LogLoadWorker();
        worker_->moveToThread(&thread_);
        connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
        connect(this, &WorkerHarness::startRequested, worker_,
                &loglens::LogLoadWorker::startLoad, Qt::QueuedConnection);
        connect(this, &WorkerHarness::acknowledgeRequested, worker_,
                &loglens::LogLoadWorker::acknowledge, Qt::QueuedConnection);
        batches_ = std::make_unique<QSignalSpy>(
            worker_, &loglens::LogLoadWorker::batchReady);
        QObject::connect(worker_, &loglens::LogLoadWorker::batchReady, this,
                         [this](const loglens::LoadBatch&) {
                             directBatchCount_.fetch_add(1, std::memory_order_release);
                         },
                         Qt::DirectConnection);
        thread_.start();
    }

    ~WorkerHarness() override {
        // This is intentionally direct: selectJob() is the worker's documented
        // cross-thread cancellation gate and must be safe while a load is in
        // FileTailer/RecordAssembler code.
        worker_->selectJob(0);
        thread_.quit();
        QVERIFY(thread_.wait(5000));
    }

    QSignalSpy& batches() { return *batches_; }

    void start(loglens::LoadRequest request) {
        worker_->selectJob(request.job_id);
        emit startRequested(std::move(request));
    }

    void selectJob(quint64 jobId) { worker_->selectJob(jobId); }

    void setFollowing(quint64 jobId, bool following) {
        worker_->setFollowing(jobId, following);
    }

    void pollBlocking(quint64 jobId) {
        const bool invoked = QMetaObject::invokeMethod(
            worker_, "poll", Qt::BlockingQueuedConnection, Q_ARG(quint64, jobId));
        QVERIFY(invoked);
    }

    void acknowledgeBlocking(quint64 jobId, quint64 sequence) {
        const bool invoked = QMetaObject::invokeMethod(
            worker_, "acknowledge", Qt::BlockingQueuedConnection, Q_ARG(quint64, jobId),
            Q_ARG(quint64, sequence));
        QVERIFY(invoked);
    }

    int directBatchCount() const {
        return directBatchCount_.load(std::memory_order_acquire);
    }

    void acknowledge(quint64 jobId, quint64 sequence) {
        emit acknowledgeRequested(jobId, sequence);
    }

signals:
    void startRequested(loglens::LoadRequest request);
    void acknowledgeRequested(quint64 jobId, quint64 sequence);

private:
    QThread thread_;
    loglens::LogLoadWorker* worker_ = nullptr;
    std::unique_ptr<QSignalSpy> batches_;
    std::atomic<int> directBatchCount_{0};
};

loglens::LoadRequest request(quint64 jobId, const QString& path,
                             loglens::InitialLoadMode mode,
                             std::size_t tailRecords = 1,
                             std::size_t chunkBytes = loglens::kDefaultSourceChunkBytes) {
    loglens::LoadRequest load;
    load.job_id = jobId;
    load.path = path;
    load.mode = mode;
    load.tail_records = tailRecords;
    load.source_chunk_bytes = chunkBytes;
    return load;
}

} // namespace

class TestLogLoadWorker : public QObject {
    Q_OBJECT

private slots:
    void batchesAreBoundedAndWaitForAcknowledgement();
    void acknowledgementDrainsTheCompleteInitialLoad();
    void selectingANewJobCancelsAndSuppressesTheStaleJob();
    void tailSelectionPreservesPhysicalLineNumbers();
    void invalidRequestsPublishNonRetryableInitialErrors();
    void disablingFollowWhileInitialBatchIsAwaitingAckPreventsFollowPoll();
    void sourceRotationBetweenInitialBatchAcksIsRejected();
};

void TestLogLoadWorker::batchesAreBoundedAndWaitForAcknowledgement() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("bounded.log"));
    writeFile(path, numberedFile(600));

    WorkerHarness harness;
    harness.start(request(1, path, loglens::InitialLoadMode::FromStart));

    QSignalSpy& batches = harness.batches();
    QTRY_VERIFY_WITH_TIMEOUT(batches.count() > 0, 5000);
    QCOMPARE(batches.count(), 1);
    const loglens::LoadBatch first = takeBatch(batches);
    QCOMPARE(first.job_id, static_cast<quint64>(1));
    QCOMPARE(first.deltas.size(), static_cast<std::size_t>(512));
    QVERIFY(first.backlog_pending);
    QVERIFY(!first.initial_complete);

    // The worker has one queued batch in flight. Pumping the main event loop
    // without an acknowledgement must not let it publish the rest.
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(batches.count(), 0);

    harness.acknowledge(first.job_id, first.sequence);
    QTRY_COMPARE_WITH_TIMEOUT(batches.count(), 1, 5000);
    const loglens::LoadBatch second = takeBatch(batches);
    QCOMPARE(second.job_id, static_cast<quint64>(1));
    QCOMPARE(second.deltas.size(), static_cast<std::size_t>(88));
    QVERIFY(second.initial_complete);
    QVERIFY(!second.backlog_pending);
    harness.acknowledge(second.job_id, second.sequence);
}

void TestLogLoadWorker::acknowledgementDrainsTheCompleteInitialLoad() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("drain.log"));
    constexpr int kRecordCount = 1300;
    writeFile(path, numberedFile(kRecordCount));

    WorkerHarness harness;
    harness.start(request(7, path, loglens::InitialLoadMode::FromStart));

    QSignalSpy& batches = harness.batches();
    std::size_t totalDeltas = 0;
    std::size_t expectedRecordIndex = 0;
    std::size_t expectedLineNumber = 1;
    quint64 expectedSequence = 0;
    bool complete = false;
    int batchCount = 0;

    while (!complete && batchCount < 8) {
        QTRY_VERIFY_WITH_TIMEOUT(batches.count() > 0, 5000);
        QCOMPARE(batches.count(), 1);
        const loglens::LoadBatch batch = takeBatch(batches);
        QCOMPARE(batch.job_id, static_cast<quint64>(7));
        QCOMPARE(batch.sequence, expectedSequence++);
        QVERIFY(batch.deltas.size() <= static_cast<std::size_t>(512));
        QVERIFY(!batch.deltas.empty());
        for (const loglens::RecordDelta& delta : batch.deltas) {
            QCOMPARE(delta.kind, loglens::RecordDelta::Kind::Append);
            QCOMPARE(delta.record_index, expectedRecordIndex++);
            QCOMPARE(delta.physical_line_number, expectedLineNumber);
            QCOMPARE(delta.record.line_number, expectedLineNumber++);
            QCOMPARE(delta.generation, static_cast<std::uint64_t>(0));
        }
        totalDeltas += batch.deltas.size();
        complete = batch.initial_complete;
        if (complete) {
            QVERIFY(!batch.backlog_pending);
        } else {
            QVERIFY(batch.backlog_pending);
        }
        ++batchCount;
        harness.acknowledge(batch.job_id, batch.sequence);
    }

    QVERIFY(complete);
    QCOMPARE(batchCount, 3);
    QCOMPARE(totalDeltas, static_cast<std::size_t>(kRecordCount));
    QCOMPARE(expectedRecordIndex, static_cast<std::size_t>(kRecordCount));
    QCOMPARE(expectedLineNumber, static_cast<std::size_t>(kRecordCount + 1));
}

void TestLogLoadWorker::selectingANewJobCancelsAndSuppressesTheStaleJob() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString stalePath = directory.filePath(QStringLiteral("stale.log"));
    const QString freshPath = directory.filePath(QStringLiteral("fresh.log"));
    writeFile(stalePath, numberedFile(700));
    writeFile(freshPath, recordLine(900) + recordLine(901));

    WorkerHarness harness;
    harness.start(request(11, stalePath, loglens::InitialLoadMode::FromStart));

    QSignalSpy& batches = harness.batches();
    QTRY_VERIFY_WITH_TIMEOUT(batches.count() > 0, 5000);
    QCOMPARE(batches.count(), 1);
    const loglens::LoadBatch stale = takeBatch(batches);
    QCOMPARE(stale.job_id, static_cast<quint64>(11));

    // Leave job 11 unacknowledged. A queued stale ack must be rejected after
    // the direct cancellation gate selects job 12, and the new request must
    // still be able to publish its first batch.
    harness.selectJob(12);
    harness.acknowledge(stale.job_id, stale.sequence);
    harness.start(request(12, freshPath, loglens::InitialLoadMode::FromStart));

    QTRY_VERIFY_WITH_TIMEOUT(batches.count() > 0, 5000);
    QCOMPARE(batches.count(), 1);
    const loglens::LoadBatch fresh = takeBatch(batches);
    QCOMPARE(fresh.job_id, static_cast<quint64>(12));
    QCOMPARE(fresh.deltas.size(), static_cast<std::size_t>(2));
    QVERIFY(fresh.initial_complete);
    QCOMPARE(fresh.deltas.at(0).record.message, std::string("record-900"));
    QCOMPARE(fresh.deltas.at(1).record.message, std::string("record-901"));
    harness.acknowledge(fresh.job_id, fresh.sequence);

    // No queued continuation from the unacknowledged stale batch may appear.
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(batches.count(), 0);
}

void TestLogLoadWorker::tailSelectionPreservesPhysicalLineNumbers() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("tail.log"));
    const QByteArray first = recordLine(1);
    const QByteArray firstContinuation = QByteArray("  first detail\n");
    const QByteArray second = recordLine(2);
    const QByteArray secondContinuation = QByteArray("at second frame\n");
    const QByteArray third = recordLine(3);
    const QByteArray thirdContinuation = QByteArray("\tthird detail\n");
    const QByteArray fourth = recordLine(4);
    const QByteArray contents = first + firstContinuation + second + secondContinuation
                                + third + thirdContinuation + fourth;
    writeFile(path, contents);

    WorkerHarness harness;
    // Tiny chunks force the locator and parser to cross arbitrary byte
    // boundaries while the selected suffix still starts at physical line 5.
    harness.start(request(21, path, loglens::InitialLoadMode::TailRecords, 2, 7));

    QSignalSpy& batches = harness.batches();
    std::vector<loglens::RecordDelta> deltas;
    std::size_t selectedOffset = 0;
    std::uint64_t snapshotEnd = 0;
    bool complete = false;
    int batchCount = 0;
    while (!complete && batchCount < 100) {
        QTRY_VERIFY_WITH_TIMEOUT(batches.count() > 0, 5000);
        QCOMPARE(batches.count(), 1);
        const loglens::LoadBatch batch = takeBatch(batches);
        QCOMPARE(batch.job_id, static_cast<quint64>(21));
        QVERIFY(batch.deltas.size() <= static_cast<std::size_t>(512));
        if (batchCount == 0) {
            selectedOffset = batch.selected_offset;
            snapshotEnd = batch.snapshot_end;
        } else {
            QCOMPARE(batch.selected_offset, static_cast<std::uint64_t>(selectedOffset));
            QCOMPARE(batch.snapshot_end, snapshotEnd);
        }
        deltas.insert(deltas.end(), batch.deltas.begin(), batch.deltas.end());
        complete = batch.initial_complete;
        ++batchCount;
        harness.acknowledge(batch.job_id, batch.sequence);
    }

    QVERIFY(complete);
    QCOMPARE(selectedOffset,
             static_cast<std::size_t>(contents.indexOf(third)));
    QCOMPARE(snapshotEnd, static_cast<std::uint64_t>(contents.size()));
    QCOMPARE(deltas.size(), static_cast<std::size_t>(3));

    const loglens::RecordDelta& retained = deltas.at(0);
    QCOMPARE(retained.kind, loglens::RecordDelta::Kind::Append);
    QCOMPARE(retained.record_index, static_cast<std::size_t>(0));
    QCOMPARE(retained.physical_line_number, static_cast<std::size_t>(5));
    QCOMPARE(retained.record.line_number, static_cast<std::size_t>(5));
    QCOMPARE(retained.record.message, std::string("record-3"));

    const loglens::RecordDelta& extension = deltas.at(1);
    QCOMPARE(extension.kind, loglens::RecordDelta::Kind::Extend);
    QCOMPARE(extension.record_index, static_cast<std::size_t>(0));
    QCOMPARE(extension.physical_line_number, static_cast<std::size_t>(6));
    QCOMPARE(extension.record.line_number, static_cast<std::size_t>(5));

    const loglens::RecordDelta& last = deltas.at(2);
    QCOMPARE(last.kind, loglens::RecordDelta::Kind::Append);
    QCOMPARE(last.record_index, static_cast<std::size_t>(1));
    QCOMPARE(last.physical_line_number, static_cast<std::size_t>(7));
    QCOMPARE(last.record.line_number, static_cast<std::size_t>(7));
    QCOMPARE(last.record.message, std::string("record-4"));
}

void TestLogLoadWorker::invalidRequestsPublishNonRetryableInitialErrors() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("invalid-request.log"));
    writeFile(path, recordLine(1));

    WorkerHarness harness;
    auto expectInvalidRequest = [&harness](loglens::LoadRequest load) {
        harness.start(std::move(load));
        QSignalSpy& batches = harness.batches();
        QTRY_VERIFY_WITH_TIMEOUT(batches.count() > 0, 5000);
        QCOMPARE(batches.count(), 1);
        const loglens::LoadBatch batch = takeBatch(batches);
        QVERIFY(batch.initial_phase);
        QVERIFY(!batch.initial_complete);
        QVERIFY(!batch.backlog_pending);
        QVERIFY(!batch.retryable);
        QVERIFY(batch.deltas.empty());
        QVERIFY(batch.error.contains(QStringLiteral("invalid background load request")));
        harness.acknowledgeBlocking(batch.job_id, batch.sequence);
    };

    expectInvalidRequest(request(101, path, loglens::InitialLoadMode::TailRecords, 0));

    loglens::LoadRequest zeroChunk =
        request(102, path, loglens::InitialLoadMode::FromStart);
    zeroChunk.source_chunk_bytes = 0;
    expectInvalidRequest(std::move(zeroChunk));

    loglens::LoadRequest oversizedChunk =
        request(103, path, loglens::InitialLoadMode::FromStart);
    oversizedChunk.source_chunk_bytes = loglens::kMaxSourceChunkBytes + 1;
    expectInvalidRequest(std::move(oversizedChunk));
}

void TestLogLoadWorker::disablingFollowWhileInitialBatchIsAwaitingAckPreventsFollowPoll() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("follow-gate.log"));
    writeFile(path, recordLine(1) + recordLine(2));

    WorkerHarness harness;
    harness.start(request(111, path, loglens::InitialLoadMode::FromStart));

    QSignalSpy& batches = harness.batches();
    QTRY_VERIFY_WITH_TIMEOUT(batches.count() > 0, 5000);
    QCOMPARE(batches.count(), 1);
    const loglens::LoadBatch initial = takeBatch(batches);
    QVERIFY(initial.initial_complete);
    QCOMPARE(harness.directBatchCount(), 1);

    // Queue a follow request while the initial batch is still awaiting its
    // acknowledgement. It must be discarded when Follow is disabled before
    // that acknowledgement is released.
    harness.setFollowing(initial.job_id, true);
    writeFile(path, recordLine(3), true);
    harness.pollBlocking(initial.job_id);
    harness.setFollowing(initial.job_id, false);
    harness.acknowledgeBlocking(initial.job_id, initial.sequence);

    // The blocking poll is an event-loop barrier: the zero-delay step queued
    // by acknowledge() is processed before the next worker invocation.
    harness.pollBlocking(initial.job_id);
    harness.pollBlocking(initial.job_id);
    QCOMPARE(harness.directBatchCount(), 1);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(batches.count(), 0);
}

void TestLogLoadWorker::sourceRotationBetweenInitialBatchAcksIsRejected() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("initial-rotation.log"));
    writeFile(path, numberedFile(20));

    WorkerHarness harness;
    harness.start(request(121, path, loglens::InitialLoadMode::FromStart, 1, 7));

    QSignalSpy& batches = harness.batches();
    QTRY_VERIFY_WITH_TIMEOUT(batches.count() > 0, 5000);
    QCOMPARE(batches.count(), 1);
    const loglens::LoadBatch first = takeBatch(batches);
    QVERIFY(first.initial_phase);
    QVERIFY(!first.initial_complete);
    QVERIFY(first.backlog_pending);
    QVERIFY(!first.deltas.empty());

    QVERIFY(QFile::remove(path));
    recreateFile(path, numberedFile(2));

    harness.acknowledgeBlocking(first.job_id, first.sequence);
    QTRY_VERIFY_WITH_TIMEOUT(batches.count() > 0, 5000);
    QCOMPARE(batches.count(), 1);
    const loglens::LoadBatch changed = takeBatch(batches);
    QVERIFY(changed.initial_phase);
    QVERIFY(!changed.initial_complete);
    QVERIFY(changed.retryable);
    QVERIFY(changed.deltas.empty());
    QVERIFY(changed.error.contains(QStringLiteral("source changed")));
    harness.acknowledgeBlocking(changed.job_id, changed.sequence);
}

QTEST_MAIN(TestLogLoadWorker)
#include "test_log_load_worker.moc"
