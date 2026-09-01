#pragma once

#include "buildscope/diff.hpp"

#include <QAbstractItemModel>
#include <QModelIndex>

#include <optional>

namespace buildscope {

enum DiffDataRole {
    DiffUnitIndexRole = Qt::UserRole + 101,
    DiffChangeIndexRole,
    DiffSourcePathRole,
    DiffSearchTextRole,
};

class DiffTreeModel final : public QAbstractItemModel {
public:
    enum Column {
        SourceColumn,
        KindColumn,
        CategoryColumn,
        BeforeColumn,
        AfterColumn,
        SuppressionColumn,
        ColumnCount,
    };

    explicit DiffTreeModel(QObject *parent = nullptr);

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void setReport(DiffReport report);
    void clear();
    const DiffReport &report() const;
    int unitCount() const;
    std::optional<qsizetype> unitIndex(const QModelIndex &index) const;
    std::optional<qsizetype> changeIndex(const QModelIndex &index) const;

private:
    static quintptr unitId(qsizetype unitIndex);
    static quintptr changeId(qsizetype unitIndex);
    static bool isChangeId(quintptr identifier);
    static qsizetype unitIndexFromId(quintptr identifier);
    QString searchText(const DiffUnit &unit) const;

    DiffReport report_;
};

}  // namespace buildscope
