#pragma once

#include <QAbstractTableModel>

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

    const loglens::LogRecord* recordAt(int row) const;
    int totalCount() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    std::vector<loglens::LogRecord> records_;
    std::vector<int> visible_;

    void rebuildVisible(const loglens::Filter* filter);
};
