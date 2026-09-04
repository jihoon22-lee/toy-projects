#include "loglens/gui/log_model.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include <QBrush>
#include <QColor>
#include <QFont>

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

std::size_t modelCapacity(std::size_t requested) {
    if (requested > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("log model capacity exceeds Qt row limit");
    }
    return requested;
}

QString columnText(const loglens::LogRecord& record, int column) {
    if (column == LogModel::ColumnLine) {
        return QString::number(record.line_number);
    }
    if (column == LogModel::ColumnLevel) {
        return QString::fromLatin1(loglens::levelName(record.level));
    }
    if (column == LogModel::ColumnSource) {
        return QString::fromUtf8(record.source.data(), static_cast<int>(record.source.size()));
    }
    // Multi-line messages (a folded stack trace) would break row height, so the
    // table shows the first line and the tooltip carries the rest.
    QString message = QString::fromUtf8(record.message.data(),
                                        static_cast<int>(record.message.size()))
                          .section(QLatin1Char('\n'), 0, 0);
    if (record.omitted_bytes > 0) {
        message += QStringLiteral("  [%1 source byte(s) omitted]")
                       .arg(static_cast<qulonglong>(record.omitted_bytes));
    }
    return message;
}

} // namespace

LogModel::LogModel(QObject* parent, std::size_t capacity)
    : QAbstractTableModel(parent), records_(modelCapacity(capacity)) {}

void LogModel::setRecords(std::vector<loglens::LogRecord> records,
                          std::uint64_t generation) {
    beginResetModel();
    records_.clear();
    generation_ = generation;
    for (const loglens::LogRecord& record : records) {
        records_.push(record);
    }
    // setRecords is the legacy replace-all API and deliberately starts with an
    // unfiltered view. Streaming reloads use resetRecords(), which preserves
    // the active filter/search while background batches arrive.
    filter_.reset();
    rebuildVisible();
    endResetModel();
}

void LogModel::setFilter(const loglens::Filter* filter) {
    beginResetModel();
    filter_ = filter == nullptr ? std::nullopt : std::optional<loglens::Filter>(*filter);
    rebuildVisible();
    endResetModel();
}

void LogModel::setSearch(const QString& search) {
    beginResetModel();
    search_ = search.trimmed();
    rebuildVisible();
    endResetModel();
}

void LogModel::setTimeWindow(std::optional<loglens::TimeWindow> window) {
    beginResetModel();
    if (window && window->begin_ms >= window->end_ms) {
        time_window_.reset();
    } else {
        time_window_ = window;
    }
    rebuildVisible();
    endResetModel();
}

void LogModel::setTriageState(const loglens::TriageState& state,
                              const QString& sourcePath) {
    highlight_rules_ = loglens::compileHighlightRules(state);
    triage_entries_ = state.entries;
    const QByteArray path = sourcePath.toUtf8();
    source_path_.assign(path.constData(), static_cast<std::size_t>(path.size()));
    if (rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1),
                         {Qt::DisplayRole, Qt::BackgroundRole, Qt::FontRole,
                          Qt::ToolTipRole});
    }
}

void LogModel::appendRecords(const std::vector<loglens::LogRecord>& records) {
    appendRecords(records, records_.totalPushed(), generation_);
}

void LogModel::appendRecords(const std::vector<loglens::LogRecord>& records,
                             std::size_t firstRecordIndex,
                             std::uint64_t generation) {
    if (records.empty()) {
        return;
    }
    if (generation != generation_ || firstRecordIndex != records_.totalPushed()) {
        return;
    }
    if (records.size() > std::numeric_limits<std::size_t>::max() - firstRecordIndex) {
        return;
    }

    const std::size_t totalAfter = firstRecordIndex + records.size();
    const std::size_t retainedAfter = std::min(records_.capacity(), totalAfter);
    const std::size_t firstRetainedAfter = totalAfter - retainedAfter;
    const auto retainedVisible =
        std::lower_bound(visible_.begin(), visible_.end(), firstRetainedAfter);
    const int removedVisible = static_cast<int>(retainedVisible - visible_.begin());
    if (removedVisible > 0) {
        beginRemoveRows(QModelIndex(), 0, removedVisible - 1);
        visible_.erase(visible_.begin(), retainedVisible);
        endRemoveRows();
    }

    for (const loglens::LogRecord& record : records) {
        records_.push(record);
    }

    const std::size_t firstArriving = std::max(firstRecordIndex, firstRetainedAfter);
    std::vector<std::size_t> arriving;
    arriving.reserve(totalAfter - firstArriving);
    for (std::size_t index = firstArriving; index < totalAfter; ++index) {
        const loglens::LogRecord& record =
            records[index - firstRecordIndex];
        if (matches(record)) {
            arriving.push_back(index);
        }
    }
    if (arriving.empty()) {
        return;
    }

    const int first = static_cast<int>(visible_.size());
    beginInsertRows(QModelIndex(), first, first + static_cast<int>(arriving.size()) - 1);
    visible_.insert(visible_.end(), arriving.begin(), arriving.end());
    endInsertRows();
}

void LogModel::resetRecords(std::uint64_t generation) {
    beginResetModel();
    records_.clear();
    visible_.clear();
    generation_ = generation;
    endResetModel();
}

void LogModel::updateRecord(std::size_t index, const loglens::LogRecord& record) {
    updateRecord(index, record, generation_);
}

void LogModel::updateRecord(std::size_t index, const loglens::LogRecord& record,
                            std::uint64_t generation) {
    const loglens::LogRecord* previous = records_.find(index);
    if (generation != generation_ || previous == nullptr) {
        return;
    }

    const bool isVisible = matches(record);
    auto position = std::lower_bound(visible_.begin(), visible_.end(), index);
    const bool wasVisible = position != visible_.end() && *position == index;

    if (wasVisible && isVisible) {
        records_.replace(index, record);
        const int row = static_cast<int>(position - visible_.begin());
        const QModelIndex first = this->index(row, 0);
        const QModelIndex last = this->index(row, ColumnCount - 1);
        emit dataChanged(first, last,
                         {Qt::DisplayRole, Qt::ForegroundRole, Qt::ToolTipRole});
        return;
    }

    if (wasVisible && !isVisible) {
        const int row = static_cast<int>(position - visible_.begin());
        beginRemoveRows(QModelIndex(), row, row);
        records_.replace(index, record);
        visible_.erase(position);
        endRemoveRows();
        return;
    }

    records_.replace(index, record);
    if (!wasVisible && isVisible) {
        position = std::lower_bound(visible_.begin(), visible_.end(), index);
        const int row = static_cast<int>(position - visible_.begin());
        beginInsertRows(QModelIndex(), row, row);
        visible_.insert(position, index);
        endInsertRows();
    }
}

void LogModel::rebuildVisible() {
    visible_.clear();
    visible_.reserve(records_.size());
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const loglens::LogRecord& record = records_.at(i);
        if (matches(record)) {
            visible_.push_back(records_.firstIndex() + i);
        }
    }
}

bool LogModel::matchesBase(const loglens::LogRecord& record) const {
    if (filter_ && !filter_->matches(record)) {
        return false;
    }
    if (search_.isEmpty()) {
        return true;
    }
    const QString raw = QString::fromUtf8(record.raw.data(), static_cast<int>(record.raw.size()));
    return raw.contains(search_, Qt::CaseInsensitive);
}

bool LogModel::matches(const loglens::LogRecord& record) const {
    if (!matchesBase(record)) return false;
    if (!time_window_) return true;
    return record.timestamp_ms != 0 && record.timestamp_ms >= time_window_->begin_ms
           && record.timestamp_ms < time_window_->end_ms;
}

const loglens::LogRecord* LogModel::recordAt(int row) const {
    if (row < 0 || row >= static_cast<int>(visible_.size())) {
        return nullptr;
    }
    return records_.find(visible_[static_cast<std::size_t>(row)]);
}

std::vector<loglens::LogRecord> LogModel::visibleRecords(bool includeTimeWindow) const {
    std::vector<loglens::LogRecord> result;
    result.reserve(records_.size());
    for (std::size_t index = 0; index < records_.size(); ++index) {
        const loglens::LogRecord& record = records_.at(index);
        if (includeTimeWindow ? matches(record) : matchesBase(record)) {
            result.push_back(record);
        }
    }
    return result;
}

std::vector<loglens::Span> LogModel::highlightSpansAt(int row) const {
    const loglens::LogRecord* record = recordAt(row);
    return record == nullptr ? std::vector<loglens::Span>{}
                             : highlight_rules_.apply(record->message);
}

int LogModel::rowForLine(std::size_t lineNumber) const {
    for (std::size_t row = 0; row < visible_.size(); ++row) {
        const loglens::LogRecord* record = records_.find(visible_[row]);
        if (record != nullptr && record->line_number == lineNumber) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

bool LogModel::bookmarkedAt(int row) const {
    const loglens::LogRecord* record = recordAt(row);
    if (record == nullptr) return false;
    return std::any_of(triage_entries_.begin(), triage_entries_.end(), [&](const auto& entry) {
        return entry.source_path == source_path_ && entry.line_number == record->line_number
               && entry.bookmarked;
    });
}

int LogModel::totalCount() const { return static_cast<int>(records_.size()); }

std::size_t LogModel::totalSeen() const { return records_.totalPushed(); }

std::size_t LogModel::droppedCount() const { return records_.droppedCount(); }

std::size_t LogModel::capacity() const { return records_.capacity(); }

std::optional<std::size_t> LogModel::oldestLine() const {
    if (records_.empty()) {
        return std::nullopt;
    }
    return records_.at(0).line_number;
}

std::optional<std::size_t> LogModel::newestLine() const {
    if (records_.empty()) {
        return std::nullopt;
    }
    return records_.at(records_.size() - 1).line_number;
}

std::uint64_t LogModel::generation() const { return generation_; }

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
    if (role == Qt::BackgroundRole) {
        const std::optional<std::string> rowStyle = highlight_rules_.rowStyle();
        if (rowStyle) {
            QColor color(QString::fromStdString(*rowStyle));
            color.setAlpha(105);
            return QBrush(color);
        }
        if (bookmarkedAt(index.row())) return QBrush(QColor(48, 72, 96, 150));
    }
    if (role == Qt::FontRole && bookmarkedAt(index.row())) {
        QFont font;
        font.setBold(true);
        return font;
    }
    if (role == Qt::ToolTipRole) {
        QString tooltip = QString::fromUtf8(record->raw.data(),
                                            static_cast<int>(record->raw.size()));
        const auto annotation = std::find_if(
            triage_entries_.begin(), triage_entries_.end(), [&](const auto& entry) {
                return entry.source_path == source_path_
                       && entry.line_number == record->line_number;
            });
        if (annotation != triage_entries_.end() && !annotation->annotation.empty()) {
            tooltip += QStringLiteral("\n\nAnnotation: ")
                       + QString::fromUtf8(annotation->annotation.data(),
                                           static_cast<int>(annotation->annotation.size()));
        }
        if (record->omitted_bytes > 0) {
            tooltip += QStringLiteral("\n[%1 source byte(s) omitted]")
                           .arg(static_cast<qulonglong>(record->omitted_bytes));
        }
        return tooltip;
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
