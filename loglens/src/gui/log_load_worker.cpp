#include "loglens/gui/log_load_worker.hpp"

#include <QTimer>

#include <algorithm>
#include <exception>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace loglens {

LogLoadWorker::LogLoadWorker(QObject* parent) : QObject(parent) {}

void LogLoadWorker::selectJob(quint64 jobId) {
    following_enabled_.store(false, std::memory_order_release);
    selected_job_.store(jobId, std::memory_order_release);
}

void LogLoadWorker::setFollowing(quint64 jobId, bool following) {
    if (selected_job_.load(std::memory_order_acquire) == jobId) {
        following_enabled_.store(following, std::memory_order_release);
    }
}

bool LogLoadWorker::cancelled() const {
    return selected_job_.load(std::memory_order_acquire) != request_.job_id;
}

bool LogLoadWorker::followingEnabled() const {
    return following_enabled_.load(std::memory_order_acquire);
}

void LogLoadWorker::clearState() {
    tailer_.reset();
    initial_snapshot_end_.reset();
    initial_identity_ = FileIdentity{};
    pending_deltas_.clear();
    pending_cursor_ = 0;
    next_sequence_ = 0;
    awaiting_sequence_ = 0;
    selected_offset_ = 0;
    initial_loading_ = false;
    initial_completion_announced_ = false;
    follow_drain_ = false;
    awaiting_ack_ = false;
    reset_pending_ = false;
    validate_initial_identity_ = false;
}

void LogLoadWorker::startLoad(loglens::LoadRequest request) {
    clearState();
    request_ = std::move(request);
    if (cancelled()) {
        return;
    }

    try {
        if (request_.tail_records == 0 || request_.source_chunk_bytes == 0
            || request_.source_chunk_bytes > kMaxSourceChunkBytes) {
            throw std::invalid_argument("background load request is outside supported bounds");
        }
        if (request_.mode == InitialLoadMode::TailRecords) {
            const InitialLoadWindow window = locateTailWindow(
                request_.path.toStdString(), request_.tail_records,
                request_.source_chunk_bytes, [this] { return cancelled(); });
            if (window.cancelled || cancelled()) {
                return;
            }
            if (!window.ok()) {
                publishError(window.error, true);
                return;
            }
            selected_offset_ = window.offset;
            initial_snapshot_end_ = window.snapshot_end;
            initial_identity_ = window.identity;
            validate_initial_identity_ = true;
            assembler_.reset(0, window.first_line_number);
            tailer_ = std::make_unique<FileTailer>(request_.path.toStdString(),
                                                   request_.source_chunk_bytes, window.offset);
        } else {
            assembler_.reset();
            tailer_ = std::make_unique<FileTailer>(request_.path.toStdString(),
                                                   request_.source_chunk_bytes);
        }
    } catch (const std::exception& exception) {
        SourceError error;
        error.kind = SourceErrorKind::ReadFailed;
        error.message = std::string("invalid background load request: ") + exception.what();
        error.retryable = false;
        publishError(error, true);
        return;
    }
    initial_loading_ = true;
    scheduleStep();
}

void LogLoadWorker::poll(quint64 jobId) {
    if (jobId != request_.job_id || cancelled() || !tailer_ || !followingEnabled()) {
        return;
    }
    if (initial_loading_ || awaiting_ack_) {
        follow_drain_ = true;
        return;
    }
    follow_drain_ = true;
    processStep();
}

void LogLoadWorker::acknowledge(quint64 jobId, quint64 sequence) {
    if (jobId != request_.job_id || cancelled() || !awaiting_ack_
        || sequence != awaiting_sequence_) {
        return;
    }
    awaiting_ack_ = false;
    scheduleStep();
}

void LogLoadWorker::scheduleStep() {
    if (awaiting_ack_ || cancelled()) {
        return;
    }
    QTimer::singleShot(0, this, [this] { processStep(); });
}

void LogLoadWorker::processStep() {
    if (awaiting_ack_ || cancelled()) {
        return;
    }
    if (pending_cursor_ < pending_deltas_.size()) {
        publishBatch(initial_loading_ == false && !initial_completion_announced_);
        return;
    }
    pending_deltas_.clear();
    pending_cursor_ = 0;

    if (initial_loading_) {
        processInitialChunk();
        return;
    }
    if (!initial_completion_announced_) {
        initial_completion_announced_ = true;
        publishBatch(true);
        return;
    }
    if (follow_drain_ && !followingEnabled()) {
        follow_drain_ = false;
    }
    if (follow_drain_) {
        processFollowChunk();
    }
}

void LogLoadWorker::processInitialChunk() {
    SourceChunk chunk = initial_snapshot_end_ ? tailer_->pollChunk(*initial_snapshot_end_)
                                              : tailer_->pollChunk();
    if (!chunk.ok()) {
        initial_loading_ = false;
        publishError(chunk.error, true);
        return;
    }
    if (!initial_snapshot_end_) {
        initial_snapshot_end_ = chunk.snapshot_end;
        initial_identity_ = chunk.identity;
        validate_initial_identity_ = true;
    } else if (validate_initial_identity_
               && (chunk.generation_changed || chunk.generation != assembler_.generation()
                   || chunk.identity != initial_identity_)) {
        SourceError error;
        error.kind = SourceErrorKind::ReadFailed;
        error.message = "source changed before the selected tail window could be loaded";
        error.retryable = true;
        initial_loading_ = false;
        publishError(error, true);
        return;
    }
    acceptChunk(chunk, true);
}

void LogLoadWorker::processFollowChunk() {
    if (!followingEnabled()) {
        follow_drain_ = false;
        return;
    }
    follow_drain_ = false;
    const SourceChunk chunk = tailer_->pollChunk();
    if (!chunk.ok()) {
        publishError(chunk.error, false);
        return;
    }
    follow_drain_ = chunk.more_available;
    acceptChunk(chunk, false);
}

void LogLoadWorker::acceptChunk(const SourceChunk& chunk, bool initialPhase) {
    if (cancelled()) {
        return;
    }
    if (!initialPhase && (chunk.generation_changed
                          || chunk.generation != assembler_.generation())) {
        resetForFollowGeneration(chunk);
    }

    pending_deltas_ = assembler_.consumeBytes(chunk.bytes);
    pending_cursor_ = 0;
    if (initialPhase) {
        initial_loading_ = chunk.position < *initial_snapshot_end_;
    }
    continueAfterChunk(initialPhase);
}

void LogLoadWorker::resetForFollowGeneration(const SourceChunk& chunk) {
    assembler_.reset(chunk.generation);
    pending_deltas_.clear();
    pending_cursor_ = 0;
    reset_pending_ = true;
}

void LogLoadWorker::continueAfterChunk(bool initialPhase) {
    if (!pending_deltas_.empty() || reset_pending_) {
        publishBatch(!initial_loading_ && !initial_completion_announced_);
        return;
    }
    if (initialPhase && !initial_loading_ && !initial_completion_announced_) {
        initial_completion_announced_ = true;
        publishBatch(true);
        return;
    }
    if ((initialPhase && initial_loading_) || (!initialPhase && follow_drain_)) {
        scheduleStep();
    }
}

void LogLoadWorker::publishBatch(bool initialComplete) {
    if (cancelled()) {
        return;
    }
    LoadBatch batch;
    batch.job_id = request_.job_id;
    batch.sequence = next_sequence_++;
    batch.generation = assembler_.generation();
    batch.reset_model = reset_pending_;
    reset_pending_ = false;
    batch.initial_phase = !initial_completion_announced_;
    batch.selected_offset = selected_offset_;
    batch.snapshot_end = initial_snapshot_end_.value_or(0);

    const std::size_t end =
        std::min(pending_deltas_.size(), pending_cursor_ + kRecordsPerBatch);
    batch.deltas.insert(batch.deltas.end(),
                        std::make_move_iterator(pending_deltas_.begin()
                                                + static_cast<std::ptrdiff_t>(pending_cursor_)),
                        std::make_move_iterator(pending_deltas_.begin()
                                                + static_cast<std::ptrdiff_t>(end)));
    pending_cursor_ = end;
    const bool pendingRecords = pending_cursor_ < pending_deltas_.size();
    const bool completedNow = initialComplete && !pendingRecords;
    batch.initial_complete = completedNow;
    batch.backlog_pending = pendingRecords || initial_loading_ || follow_drain_;
    if (completedNow) {
        initial_completion_announced_ = true;
        batch.initial_phase = true;
        batch.backlog_pending = pendingRecords || follow_drain_;
    }

    awaiting_ack_ = true;
    awaiting_sequence_ = batch.sequence;
    emit batchReady(std::move(batch));
}

void LogLoadWorker::publishError(const SourceError& error, bool initialPhase) {
    if (cancelled()) {
        return;
    }
    LoadBatch batch;
    batch.job_id = request_.job_id;
    batch.sequence = next_sequence_++;
    batch.generation = assembler_.generation();
    batch.initial_phase = initialPhase;
    batch.retryable = error.retryable;
    batch.error = QString::fromStdString(error.message);
    batch.selected_offset = selected_offset_;
    batch.snapshot_end = initial_snapshot_end_.value_or(0);
    if (initialPhase) {
        initial_completion_announced_ = true;
    }
    awaiting_ack_ = true;
    awaiting_sequence_ = batch.sequence;
    emit batchReady(std::move(batch));
}

} // namespace loglens
