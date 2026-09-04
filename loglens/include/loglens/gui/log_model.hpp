#pragma once

#include <QAbstractTableModel>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "loglens/filter_expr.hpp"
#include "loglens/highlight_rules.hpp"
#include "loglens/log_record.hpp"
#include "loglens/ring_buffer.hpp"
#include "loglens/triage.hpp"
#include "loglens/window_analysis.hpp"

// Table model over parsed records.
//
// Filtering keeps an index vector rather than copying records, so switching a
// filter on a large file costs one pass and no reallocation of the log itself.
// QTableView only asks for the rows it paints, which is what makes a very large
// file viewable at all.
class LogModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { ColumnLine = 0, ColumnLevel, ColumnSource, ColumnMessage, ColumnCount };
    explicit LogModel(QObject* parent = nullptr,
                      std::size_t capacity = loglens::kDefaultRecordCapacity);

    void setRecords(std::vector<loglens::LogRecord> records,
                    std::uint64_t generation = 0);
    // Passing nullptr clears the filter and shows everything.
    void setFilter(const loglens::Filter* filter);
    // Case-insensitive raw-text search composed with the structured filter.
    void setSearch(const QString& search);
    // A timeline selection further narrows the same record set shown by the
    // table. nullopt restores the complete filter/search result.
    void setTimeWindow(std::optional<loglens::TimeWindow> window);
    void setTriageState(const loglens::TriageState& state, const QString& sourcePath);

    // Appends records that arrived after the last poll. Rows that pass the
    // current filter are announced as one contiguous insert at the end, which
    // is always correct because appends never land in the middle.
    void appendRecords(const std::vector<loglens::LogRecord>& records);
    void appendRecords(const std::vector<loglens::LogRecord>& records,
                       std::size_t firstRecordIndex,
                       std::uint64_t generation);

    // Drops everything. Used when the source file is truncated or rotated,
    // where the retained rows no longer correspond to anything on disk.
    void resetRecords(std::uint64_t generation = 0);

    // Replaces one already-published record after a continuation line arrives.
    // The method preserves filter semantics and emits the smallest valid model
    // change for visible rows.
    void updateRecord(std::size_t index, const loglens::LogRecord& record);
    void updateRecord(std::size_t index, const loglens::LogRecord& record,
                      std::uint64_t generation);

    const loglens::LogRecord* recordAt(int row) const;
    std::vector<loglens::LogRecord> visibleRecords(bool includeTimeWindow = true) const;
    std::vector<loglens::Span> highlightSpansAt(int row) const;
    int rowForLine(std::size_t lineNumber) const;
    bool bookmarkedAt(int row) const;
    int totalCount() const;
    std::size_t totalSeen() const;
    std::size_t droppedCount() const;
    std::size_t capacity() const;
    std::optional<std::size_t> oldestLine() const;
    std::optional<std::size_t> newestLine() const;
    std::uint64_t generation() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    loglens::RingBuffer records_;
    // Absolute logical record IDs from RecordAssembler, never ring-slot indexes.
    // IDs remain stable when the backing buffer wraps and old records are evicted.
    std::vector<std::size_t> visible_;
    // Held by value, not by pointer: appendRecords needs the predicate later,
    // and the caller's optional<Filter> may be reassigned before then. A Filter
    // is one shared_ptr, so copying it is cheap.
    std::optional<loglens::Filter> filter_;
    QString search_;
    std::optional<loglens::TimeWindow> time_window_;
    loglens::HighlightRules highlight_rules_;
    std::vector<loglens::TriageEntry> triage_entries_;
    std::string source_path_;
    std::uint64_t generation_ = 0;

    void rebuildVisible();
    bool matchesBase(const loglens::LogRecord& record) const;
    bool matches(const loglens::LogRecord& record) const;
};
