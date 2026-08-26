#include "log_model.hpp"

#include <QBrush>
#include <QColor>

namespace {

// Level colours, table-driven so each entry stays one line.
struct LevelStyle {
    loglens::Level level;
    const char* colour;
};

const LevelStyle kLevelStyles[] = {
    {loglens::Level::Trace, "#8a8f98"}, {loglens::Level::Debug, "#7aa2c8"},
    {loglens::Level::Info, "#cbd5df"},  {loglens::Level::Warn, "#e0b341"},
    {loglens::Level::Error, "#e0645a"}, {loglens::Level::Fatal, "#ff4d4d"},
};

QColor colourFor(loglens::Level level) {
    for (const LevelStyle& style : kLevelStyles) {
        if (style.level == level) {
            return QColor(style.colour);
        }
    }
    return QColor("#8a8f98");
}

QString columnText(const loglens::LogRecord& record, int column) {
    if (column == LogModel::ColumnLine) {
        return QString::number(record.line_number);
    }
    if (column == LogModel::ColumnLevel) {
        return QString::fromLatin1(loglens::levelName(record.level));
    }
    if (column == LogModel::ColumnSource) {
        return QString::fromStdString(record.source);
    }
    // Multi-line messages (a folded stack trace) would break row height, so the
    // table shows the first line and the tooltip carries the rest.
    const QString message = QString::fromStdString(record.message);
    return message.section(QLatin1Char('\n'), 0, 0);
}

} // namespace

LogModel::LogModel(QObject* parent) : QAbstractTableModel(parent) {}

void LogModel::setRecords(std::vector<loglens::LogRecord> records) {
    beginResetModel();
    records_ = std::move(records);
    rebuildVisible(nullptr);
    endResetModel();
}

void LogModel::setFilter(const loglens::Filter* filter) {
    beginResetModel();
    rebuildVisible(filter);
    endResetModel();
}

void LogModel::rebuildVisible(const loglens::Filter* filter) {
    visible_.clear();
    visible_.reserve(records_.size());
    for (std::size_t i = 0; i < records_.size(); ++i) {
        if (filter == nullptr || filter->matches(records_[i])) {
            visible_.push_back(static_cast<int>(i));
        }
    }
}

const loglens::LogRecord* LogModel::recordAt(int row) const {
    if (row < 0 || row >= static_cast<int>(visible_.size())) {
        return nullptr;
    }
    return &records_[static_cast<std::size_t>(visible_[static_cast<std::size_t>(row)])];
}

int LogModel::totalCount() const { return static_cast<int>(records_.size()); }

int LogModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(visible_.size());
}

int LogModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LogModel::data(const QModelIndex& index, int role) const {
    const loglens::LogRecord* record = recordAt(index.row());
    if (record == nullptr) {
        return QVariant();
    }
    if (role == Qt::DisplayRole) {
        return columnText(*record, index.column());
    }
    if (role == Qt::ForegroundRole) {
        return QBrush(colourFor(record->level));
    }
    if (role == Qt::ToolTipRole) {
        return QString::fromStdString(record->raw);
    }
    return QVariant();
}

QVariant LogModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return QVariant();
    }
    static const char* const kHeaders[] = {"#", "Level", "Source", "Message"};
    if (section < 0 || section >= ColumnCount) {
        return QVariant();
    }
    return QString::fromLatin1(kHeaders[section]);
}
