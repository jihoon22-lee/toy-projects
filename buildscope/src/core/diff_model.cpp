#include "buildscope/diff_model.hpp"

#include <utility>

namespace buildscope {
namespace {

QString compactDigest(const std::optional<DiffConfiguration> &configuration) {
    if (!configuration.has_value()) {
        return QStringLiteral("—");
    }
    constexpr qsizetype kVisibleDigestChars = 12;
    const auto &digest = configuration->semanticDigest;
    return digest.size() > kVisibleDigestChars
               ? digest.left(kVisibleDigestChars) + QChar(0x2026)
               : digest;
}

QString sourceLabel(const DiffSource &source) {
    if (source.before.has_value() && source.after.has_value() &&
        *source.before != *source.after) {
        return *source.before + QStringLiteral(" → ") + *source.after;
    }
    return source.after.has_value() ? *source.after : source.before.value_or(QString());
}

}  // namespace

DiffTreeModel::DiffTreeModel(QObject *parent) : QAbstractItemModel(parent) {}

QModelIndex DiffTreeModel::index(int row, int column,
                                 const QModelIndex &parentIndex) const {
    if (row < 0 || column < 0 || column >= ColumnCount) {
        return {};
    }
    if (!parentIndex.isValid()) {
        if (row >= report_.units.size()) {
            return {};
        }
        return createIndex(row, column, unitId(row));
    }
    if (parentIndex.column() != 0 || isChangeId(parentIndex.internalId())) {
        return {};
    }
    const auto unit = unitIndexFromId(parentIndex.internalId());
    if (unit < 0 || unit >= report_.units.size() ||
        row >= report_.units.at(unit).changes.size()) {
        return {};
    }
    return createIndex(row, column, changeId(unit));
}

QModelIndex DiffTreeModel::parent(const QModelIndex &child) const {
    if (!child.isValid() || !isChangeId(child.internalId())) {
        return {};
    }
    const auto unit = unitIndexFromId(child.internalId());
    if (unit < 0 || unit >= report_.units.size()) {
        return {};
    }
    return createIndex(unit, 0, unitId(unit));
}

int DiffTreeModel::rowCount(const QModelIndex &parentIndex) const {
    if (!parentIndex.isValid()) {
        return report_.units.size();
    }
    if (parentIndex.column() != 0 || isChangeId(parentIndex.internalId())) {
        return 0;
    }
    const auto unit = unitIndexFromId(parentIndex.internalId());
    if (unit < 0 || unit >= report_.units.size()) {
        return 0;
    }
    return report_.units.at(unit).changes.size();
}

int DiffTreeModel::columnCount(const QModelIndex &) const {
    return ColumnCount;
}

QVariant DiffTreeModel::data(const QModelIndex &modelIndex, int role) const {
    const auto unitIndexValue = unitIndex(modelIndex);
    if (!unitIndexValue.has_value()) {
        return {};
    }
    const auto &unit = report_.units.at(*unitIndexValue);
    const auto changeIndexValue = changeIndex(modelIndex);
    if (role == DiffUnitIndexRole) {
        return *unitIndexValue;
    }
    if (role == DiffChangeIndexRole) {
        return changeIndexValue.has_value() ? QVariant(*changeIndexValue) : QVariant();
    }
    if (role == DiffSourcePathRole) {
        return sourceLabel(unit.source);
    }
    if (role == DiffSearchTextRole) {
        return searchText(unit);
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }
    if (!changeIndexValue.has_value()) {
        switch (modelIndex.column()) {
        case SourceColumn:
            return sourceLabel(unit.source);
        case KindColumn:
            return unit.kind;
        case CategoryColumn:
            return tr("%1 change(s)").arg(unit.changes.size());
        case BeforeColumn:
            return compactDigest(unit.before);
        case AfterColumn:
            return compactDigest(unit.after);
        case SuppressionColumn:
            return unit.suppressed ? tr("suppressed") : tr("visible");
        default:
            return {};
        }
    }
    const auto &change = unit.changes.at(*changeIndexValue);
    switch (modelIndex.column()) {
    case SourceColumn:
        return {};
    case KindColumn:
        return tr("field");
    case CategoryColumn:
        return change.category;
    case BeforeColumn:
        return renderDiffValue(change.before);
    case AfterColumn:
        return renderDiffValue(change.after);
    case SuppressionColumn:
        return change.suppression.has_value() ? *change.suppression : tr("visible");
    default:
        return {};
    }
}

QVariant DiffTreeModel::headerData(int section, Qt::Orientation orientation,
                                   int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case SourceColumn:
        return tr("Source / change");
    case KindColumn:
        return tr("Kind");
    case CategoryColumn:
        return tr("Category");
    case BeforeColumn:
        return tr("Before");
    case AfterColumn:
        return tr("After");
    case SuppressionColumn:
        return tr("Suppression");
    default:
        return {};
    }
}

Qt::ItemFlags DiffTreeModel::flags(const QModelIndex &modelIndex) const {
    return modelIndex.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                : Qt::NoItemFlags;
}

void DiffTreeModel::setReport(DiffReport report) {
    beginResetModel();
    report_ = std::move(report);
    endResetModel();
}

void DiffTreeModel::clear() {
    setReport({});
}

const DiffReport &DiffTreeModel::report() const {
    return report_;
}

int DiffTreeModel::unitCount() const {
    return report_.units.size();
}

std::optional<qsizetype> DiffTreeModel::unitIndex(
    const QModelIndex &modelIndex) const {
    if (!modelIndex.isValid()) {
        return std::nullopt;
    }
    const auto unit = unitIndexFromId(modelIndex.internalId());
    if (unit < 0 || unit >= report_.units.size()) {
        return std::nullopt;
    }
    return unit;
}

std::optional<qsizetype> DiffTreeModel::changeIndex(
    const QModelIndex &modelIndex) const {
    const auto unit = unitIndex(modelIndex);
    if (!unit.has_value() || !isChangeId(modelIndex.internalId()) ||
        modelIndex.row() < 0 || modelIndex.row() >= report_.units.at(*unit).changes.size()) {
        return std::nullopt;
    }
    return modelIndex.row();
}

quintptr DiffTreeModel::unitId(qsizetype unitIndexValue) {
    return static_cast<quintptr>((unitIndexValue + 1) << 1);
}

quintptr DiffTreeModel::changeId(qsizetype unitIndexValue) {
    return unitId(unitIndexValue) | quintptr(1);
}

bool DiffTreeModel::isChangeId(quintptr identifier) {
    return (identifier & quintptr(1)) != 0;
}

qsizetype DiffTreeModel::unitIndexFromId(quintptr identifier) {
    return static_cast<qsizetype>((identifier >> 1) - 1);
}

QString DiffTreeModel::searchText(const DiffUnit &unit) const {
    QStringList fields = {sourceLabel(unit.source), unit.kind};
    if (unit.before.has_value()) {
        fields.append(unit.before->semanticDigest);
    }
    if (unit.after.has_value()) {
        fields.append(unit.after->semanticDigest);
    }
    for (const auto &change : unit.changes) {
        fields.append({change.category, renderDiffValue(change.before),
                       renderDiffValue(change.after)});
        if (change.suppression.has_value()) {
            fields.append(*change.suppression);
        }
    }
    return fields.join(QLatin1Char('\n'));
}

}  // namespace buildscope
