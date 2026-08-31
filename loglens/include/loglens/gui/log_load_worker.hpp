#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "loglens/initial_load.hpp"
#include "loglens/log_parser.hpp"
#include "loglens/log_source.hpp"

namespace loglens {

struct LoadRequest {
    quint64 job_id = 0;
    QString path;
    InitialLoadMode mode = InitialLoadMode::TailRecords;
    std::size_t tail_records = 1;
    std::size_t source_chunk_bytes = kDefaultSourceChunkBytes;
};

struct LoadBatch {
    quint64 job_id = 0;
    quint64 sequence = 0;
    std::uint64_t generation = 0;
    std::vector<RecordDelta> deltas;
    bool reset_model = false;
    bool initial_phase = false;
    bool initial_complete = false;
    bool backlog_pending = false;
    bool retryable = false;
    QString error;
    std::uint64_t selected_offset = 0;
    std::uint64_t snapshot_end = 0;
};

// All source I/O and parsing lives on one dedicated thread. At most one batch
// is in Qt's queued connection at a time; the GUI explicitly acknowledges it
// before the worker reads or publishes more data.
class LogLoadWorker : public QObject {
    Q_OBJECT

public:
    explicit LogLoadWorker(QObject* parent = nullptr);

    // Direct-thread-safe cancellation gate. MainWindow calls this before it
    // queues a newer request or asks the thread to stop.
    void selectJob(quint64 jobId);
    void setFollowing(quint64 jobId, bool following);

public slots:
    void startLoad(loglens::LoadRequest request);
    void poll(quint64 jobId);
    void acknowledge(quint64 jobId, quint64 sequence);

signals:
    void batchReady(loglens::LoadBatch batch);

private:
    static constexpr std::size_t kRecordsPerBatch = 512;

    std::atomic<quint64> selected_job_{0};
    std::atomic<bool> following_enabled_{false};
    LoadRequest request_;
    std::unique_ptr<FileTailer> tailer_;
    RecordAssembler assembler_;
    std::optional<std::uint64_t> initial_snapshot_end_;
    FileIdentity initial_identity_;
    std::vector<RecordDelta> pending_deltas_;
    std::size_t pending_cursor_ = 0;
    quint64 next_sequence_ = 0;
    quint64 awaiting_sequence_ = 0;
    std::uint64_t selected_offset_ = 0;
    bool initial_loading_ = false;
    bool initial_completion_announced_ = false;
    bool follow_drain_ = false;
    bool awaiting_ack_ = false;
    bool reset_pending_ = false;
    bool validate_initial_identity_ = false;

    bool cancelled() const;
    bool followingEnabled() const;
    void clearState();
    void scheduleStep();
    void processStep();
    void processInitialChunk();
    void processFollowChunk();
    void acceptChunk(const SourceChunk& chunk, bool initialPhase);
    void publishBatch(bool initialComplete = false);
    void publishError(const SourceError& error, bool initialPhase);
};

} // namespace loglens

Q_DECLARE_METATYPE(loglens::LoadRequest)
Q_DECLARE_METATYPE(loglens::LoadBatch)
