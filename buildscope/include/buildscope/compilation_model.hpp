#pragma once

#include "buildscope/contract.hpp"

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace buildscope {

enum class CompilationNodeKind {
    Source,
    Configuration,
};

enum CompilationDataRole {
    NodeKindRole = Qt::UserRole + 1,
    EntryIndexRole,
    SourcePathRole,
    SourceStatusRole,
    SearchTextRole,
};

class CompilationEntryView final {
public:
    CompilationEntryView() = default;
    explicit CompilationEntryView(const SnapshotEntry *entry);

    bool isValid() const;
    bool isNormalized() const;
    const SnapshotEntry *entry() const;

    QString sourcePath() const;
    QString directoryPath() const;
    QString sourceStatus() const;
    QString compilerLabel() const;
    QString targetLabel() const;
    QString standard() const;
    QString configurationId() const;
    QString invocationSource() const;
    QString structuredArguments() const;
    QString rawCommand() const;
    QString searchText() const;

private:
    const SnapshotEntry *entry_ = nullptr;
};

QString renderArgumentVector(const QStringList &arguments);

class CompilationTreeModel final : public QAbstractItemModel {
public:
    enum Column {
        SourceColumn,
        StatusColumn,
        TargetColumn,
        CompilerColumn,
        StandardColumn,
        ConfigurationColumn,
        ColumnCount,
    };

    explicit CompilationTreeModel(QObject *parent = nullptr);

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void setSnapshot(Snapshot snapshot);
    void clear();
    const Snapshot &snapshot() const;
    int sourceCount() const;
    int entryCount() const;
    std::optional<qsizetype> entryIndex(const QModelIndex &index) const;
    CompilationEntryView entryView(const QModelIndex &index) const;

private:
    struct SourceGroup {
        QString source;
        QString status;
        QString target;
        QString compiler;
        QString standard;
        QVector<qsizetype> entries;
    };

    static quintptr sourceId(qsizetype sourceIndex);
    static quintptr configurationId(qsizetype sourceIndex);
    static bool isConfigurationId(quintptr identifier);
    static qsizetype sourceIndexFromId(quintptr identifier);

    QVariant sourceData(const SourceGroup &group, int column, int role) const;
    QVariant configurationData(qsizetype entryIndex, int column, int role) const;
    void rebuildGroups();

    Snapshot snapshot_;
    QVector<SourceGroup> groups_;
};

}  // namespace buildscope
