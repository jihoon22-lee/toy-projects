#pragma once

#include <QAbstractTableModel>

#include <cstddef>
#include <optional>
#include <vector>

#include "loglens/filter_expr.hpp"
#include "loglens/log_record.hpp"

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

    explicit LogModel(QObject* parent = nullptr);

    void setRecords(std::vector<loglens::LogRecord> records);
    // Passing nullptr clears the filter and shows everything.
    void setFilter(const loglens::Filter* filter);

    // Appends records that arrived after the last poll. Rows that pass the
    // current filter are announced as one contiguous insert at the end, which
    // is always correct because appends never land in the middle.
    void appendRecords(const std::vector<loglens::LogRecord>& records);

    // Drops everything. Used when the source file is truncated or rotated,
    // where the retained rows no longer correspond to anything on disk.
    void resetRecords();

    // Replaces one already-published record after a continuation line arrives.
    // The method preserves filter semantics and emits the smallest valid model
    // change for visible rows.
    void updateRecord(std::size_t index, const loglens::LogRecord& record);

    const loglens::LogRecord* recordAt(int row) const;
    int totalCount() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    std::vector<loglens::LogRecord> records_;
    std::vector<int> visible_;
    // Held by value, not by pointer: appendRecords needs the predicate later,
    // and the caller's optional<Filter> may be reassigned before then. A Filter
    // is one shared_ptr, so copying it is cheap.
    std::optional<loglens::Filter> filter_;

    void rebuildVisible(const loglens::Filter* filter);
};
