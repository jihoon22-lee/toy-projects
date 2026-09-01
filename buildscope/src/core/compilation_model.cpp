#include "buildscope/compilation_model.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QVariant>

#include <utility>

namespace buildscope {
namespace {

QString collapsedLabel(const QSet<QString> &values, const QString &multipleLabel) {
    if (values.isEmpty()) {
        return {};
    }
    if (values.size() == 1) {
        return *values.constBegin();
    }
    return multipleLabel.arg(values.size());
}

int statusPriority(const QString &status) {
    if (status == QLatin1String("missing")) {
        return 3;
    }
    if (status == QLatin1String("stale")) {
        return 2;
    }
    if (status == QLatin1String("present")) {
        return 1;
    }
    return 0;
}

QString shortConfiguration(const QString &configuration) {
    constexpr qsizetype kDigestPrefixLength = 7;
    constexpr qsizetype kVisibleDigestCharacters = 12;
    if (configuration.startsWith(QLatin1String("sha256:")) &&
        configuration.size() > kDigestPrefixLength + kVisibleDigestCharacters) {
        return configuration.left(kDigestPrefixLength + kVisibleDigestCharacters) +
               QChar(0x2026);
    }
    return configuration;
}

}  // namespace

CompilationEntryView::CompilationEntryView(const SnapshotEntry *entry) : entry_(entry) {}

bool CompilationEntryView::isValid() const {
    return entry_ != nullptr;
}

bool CompilationEntryView::isNormalized() const {
    return isValid() && entry_->hasNormalized;
}

const SnapshotEntry *CompilationEntryView::entry() const {
    return entry_;
}

QString CompilationEntryView::sourcePath() const {
    if (!isValid()) {
        return {};
    }
    return isNormalized() ? entry_->normalized.source.path : entry_->file;
}

QString CompilationEntryView::directoryPath() const {
    if (!isValid()) {
        return {};
    }
    return isNormalized() ? entry_->normalized.directory.path : entry_->directory;
}

QString CompilationEntryView::sourceStatus() const {
    if (!isValid() || !entry_->hasState) {
        return QStringLiteral("unknown");
    }
    return entry_->state.sourceStatus;
}

QString CompilationEntryView::compilerLabel() const {
    if (!isNormalized()) {
        return {};
    }
    const auto &compiler = entry_->normalized.compiler;
    if (!compiler.name.isEmpty()) {
        return compiler.name;
    }
    return compiler.family;
}

QString CompilationEntryView::targetLabel() const {
    if (!isNormalized()) {
        return {};
    }
    const auto &target = entry_->normalized.target;
    if (!target.buildTarget.isEmpty() && !target.triple.isEmpty()) {
        return target.buildTarget + QStringLiteral(" · ") + target.triple;
    }
    return target.buildTarget.isEmpty() ? target.triple : target.buildTarget;
}

QString CompilationEntryView::standard() const {
    return isNormalized() ? entry_->normalized.standard : QString();
}

QString CompilationEntryView::configurationId() const {
    return isNormalized() ? entry_->normalized.configuration : QString();
}

QString CompilationEntryView::invocationSource() const {
    if (isNormalized()) {
        return entry_->normalized.invocationSource;
    }
    return entry_->arguments.isEmpty() ? QStringLiteral("command")
                                       : QStringLiteral("arguments");
}

QString CompilationEntryView::structuredArguments() const {
    if (!isValid()) {
        return {};
    }
    if (isNormalized()) {
        return renderArgumentVector(entry_->normalized.argv);
    }
    return renderArgumentVector(entry_->arguments);
}

QString CompilationEntryView::rawCommand() const {
    return isValid() ? entry_->command : QString();
}

QString CompilationEntryView::searchText() const {
    if (!isValid()) {
        return {};
    }
    QStringList fields = {sourcePath(), directoryPath(), sourceStatus(), compilerLabel(),
                          targetLabel(), standard(), configurationId(), invocationSource(),
                          structuredArguments(), rawCommand()};
    if (isNormalized()) {
        for (const auto &define : entry_->normalized.defines) {
            fields.append(define.name);
            if (define.value.has_value()) {
                fields.append(*define.value);
            }
        }
        for (const auto &include : entry_->normalized.includePaths) {
            fields.append(include.path);
            fields.append(include.kind);
            fields.append(include.scope);
        }
        for (const auto &diagnostic : entry_->diagnostics) {
            fields.append(diagnostic.code);
            fields.append(diagnostic.message);
            fields.append(diagnostic.severity);
        }
    }
    return fields.join(QLatin1Char('\n'));
}

QString renderArgumentVector(const QStringList &arguments) {
    QJsonArray values;
    for (const auto &argument : arguments) {
        values.append(argument);
    }
    return QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact));
}

CompilationTreeModel::CompilationTreeModel(QObject *parent) : QAbstractItemModel(parent) {}

QModelIndex CompilationTreeModel::index(int row, int column,
                                        const QModelIndex &parentIndex) const {
    if (row < 0 || column < 0 || column >= ColumnCount) {
        return {};
    }
    if (!parentIndex.isValid()) {
        if (row >= groups_.size()) {
            return {};
        }
        return createIndex(row, column, sourceId(row));
    }
    if (parentIndex.column() != 0 || isConfigurationId(parentIndex.internalId())) {
        return {};
    }
    const auto sourceIndex = sourceIndexFromId(parentIndex.internalId());
    if (sourceIndex < 0 || sourceIndex >= groups_.size() ||
        row >= groups_.at(sourceIndex).entries.size()) {
        return {};
    }
    return createIndex(row, column, configurationId(sourceIndex));
}

QModelIndex CompilationTreeModel::parent(const QModelIndex &child) const {
    if (!child.isValid() || !isConfigurationId(child.internalId())) {
        return {};
    }
    const auto sourceIndex = sourceIndexFromId(child.internalId());
    if (sourceIndex < 0 || sourceIndex >= groups_.size()) {
        return {};
    }
    return createIndex(sourceIndex, 0, sourceId(sourceIndex));
}

int CompilationTreeModel::rowCount(const QModelIndex &parentIndex) const {
    if (!parentIndex.isValid()) {
        return groups_.size();
    }
    if (parentIndex.column() != 0 || isConfigurationId(parentIndex.internalId())) {
        return 0;
    }
    const auto sourceIndex = sourceIndexFromId(parentIndex.internalId());
    if (sourceIndex < 0 || sourceIndex >= groups_.size()) {
        return 0;
    }
    return groups_.at(sourceIndex).entries.size();
}

int CompilationTreeModel::columnCount(const QModelIndex &) const {
    return ColumnCount;
}

QVariant CompilationTreeModel::data(const QModelIndex &modelIndex, int role) const {
    if (!modelIndex.isValid()) {
        return {};
    }
    const auto sourceIndex = sourceIndexFromId(modelIndex.internalId());
    if (sourceIndex < 0 || sourceIndex >= groups_.size()) {
        return {};
    }
    if (!isConfigurationId(modelIndex.internalId())) {
        return sourceData(groups_.at(sourceIndex), modelIndex.column(), role);
    }
    const auto &entries = groups_.at(sourceIndex).entries;
    if (modelIndex.row() < 0 || modelIndex.row() >= entries.size()) {
        return {};
    }
    return configurationData(entries.at(modelIndex.row()), modelIndex.column(), role);
}

QVariant CompilationTreeModel::headerData(int section, Qt::Orientation orientation,
                                          int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case SourceColumn:
        return tr("Source / configuration");
    case StatusColumn:
        return tr("Status");
    case TargetColumn:
        return tr("Target");
    case CompilerColumn:
        return tr("Compiler");
    case StandardColumn:
        return tr("Standard");
    case ConfigurationColumn:
        return tr("Configuration ID");
    default:
        return {};
    }
}

Qt::ItemFlags CompilationTreeModel::flags(const QModelIndex &modelIndex) const {
    if (!modelIndex.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void CompilationTreeModel::setSnapshot(Snapshot snapshot) {
    beginResetModel();
    snapshot_ = std::move(snapshot);
    rebuildGroups();
    endResetModel();
}

void CompilationTreeModel::clear() {
    setSnapshot({});
}

const Snapshot &CompilationTreeModel::snapshot() const {
    return snapshot_;
}

int CompilationTreeModel::sourceCount() const {
    return groups_.size();
}

int CompilationTreeModel::entryCount() const {
    return snapshot_.entries.size();
}

std::optional<qsizetype> CompilationTreeModel::entryIndex(
    const QModelIndex &modelIndex) const {
    if (!modelIndex.isValid()) {
        return std::nullopt;
    }
    const auto sourceIndex = sourceIndexFromId(modelIndex.internalId());
    if (sourceIndex < 0 || sourceIndex >= groups_.size()) {
        return std::nullopt;
    }
    const auto &entries = groups_.at(sourceIndex).entries;
    if (!isConfigurationId(modelIndex.internalId())) {
        if (entries.size() == 1) {
            return entries.front();
        }
        return std::nullopt;
    }
    if (modelIndex.row() < 0 || modelIndex.row() >= entries.size()) {
        return std::nullopt;
    }
    return entries.at(modelIndex.row());
}

CompilationEntryView CompilationTreeModel::entryView(const QModelIndex &modelIndex) const {
    const auto entry = entryIndex(modelIndex);
    if (!entry.has_value() || *entry < 0 || *entry >= snapshot_.entries.size()) {
        return {};
    }
    return CompilationEntryView(&snapshot_.entries.at(*entry));
}

quintptr CompilationTreeModel::sourceId(qsizetype sourceIndex) {
    return static_cast<quintptr>((sourceIndex + 1) << 1);
}

quintptr CompilationTreeModel::configurationId(qsizetype sourceIndex) {
    return sourceId(sourceIndex) | quintptr(1);
}

bool CompilationTreeModel::isConfigurationId(quintptr identifier) {
    return (identifier & quintptr(1)) != 0;
}

qsizetype CompilationTreeModel::sourceIndexFromId(quintptr identifier) {
    if (identifier < 2) {
        return -1;
    }
    return static_cast<qsizetype>(identifier >> 1) - 1;
}

QVariant CompilationTreeModel::sourceData(const SourceGroup &group, int column,
                                          int role) const {
    if (role == NodeKindRole) {
        return static_cast<int>(CompilationNodeKind::Source);
    }
    if (role == EntryIndexRole) {
        return group.entries.size() == 1 ? QVariant::fromValue(group.entries.front())
                                         : QVariant();
    }
    if (role == SourcePathRole) {
        return group.source;
    }
    if (role == SourceStatusRole) {
        return group.status;
    }
    if (role == SearchTextRole) {
        return QStringList({group.source, group.status, group.target, group.compiler,
                            group.standard})
            .join(QLatin1Char('\n'));
    }
    if (role == Qt::ToolTipRole) {
        return tr("%1 configuration(s) for %2").arg(group.entries.size()).arg(group.source);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (column) {
    case SourceColumn:
        return group.source + tr(" (%1)").arg(group.entries.size());
    case StatusColumn:
        return group.status;
    case TargetColumn:
        return group.target;
    case CompilerColumn:
        return group.compiler;
    case StandardColumn:
        return group.standard;
    case ConfigurationColumn:
        return group.entries.size() == 1
                   ? shortConfiguration(
                         CompilationEntryView(&snapshot_.entries.at(group.entries.front()))
                             .configurationId())
                   : tr("%1 configurations").arg(group.entries.size());
    default:
        return {};
    }
}

QVariant CompilationTreeModel::configurationData(qsizetype snapshotEntryIndex, int column,
                                                 int role) const {
    const CompilationEntryView view(&snapshot_.entries.at(snapshotEntryIndex));
    if (role == NodeKindRole) {
        return static_cast<int>(CompilationNodeKind::Configuration);
    }
    if (role == EntryIndexRole) {
        return QVariant::fromValue(snapshotEntryIndex);
    }
    if (role == SourcePathRole) {
        return view.sourcePath();
    }
    if (role == SourceStatusRole) {
        return view.sourceStatus();
    }
    if (role == SearchTextRole) {
        return view.searchText();
    }
    if (role == Qt::ToolTipRole) {
        return view.searchText();
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (column) {
    case SourceColumn:
        return tr("Configuration %1").arg(snapshotEntryIndex + 1);
    case StatusColumn:
        return view.sourceStatus();
    case TargetColumn:
        return view.targetLabel();
    case CompilerColumn:
        return view.compilerLabel();
    case StandardColumn:
        return view.standard();
    case ConfigurationColumn:
        return shortConfiguration(view.configurationId());
    default:
        return {};
    }
}

void CompilationTreeModel::rebuildGroups() {
    groups_.clear();
    groups_.reserve(snapshot_.entries.size());
    QHash<QString, qsizetype> sourceIndexes;
    for (qsizetype entryIndex = 0; entryIndex < snapshot_.entries.size(); ++entryIndex) {
        const CompilationEntryView view(&snapshot_.entries.at(entryIndex));
        const auto source = view.sourcePath();
        auto found = sourceIndexes.constFind(source);
        if (found == sourceIndexes.cend()) {
            const auto newIndex = groups_.size();
            SourceGroup group;
            group.source = source;
            groups_.append(std::move(group));
            sourceIndexes.insert(source, newIndex);
            found = sourceIndexes.constFind(source);
        }
        groups_[*found].entries.append(entryIndex);
    }

    for (auto &group : groups_) {
        QSet<QString> targets;
        QSet<QString> compilers;
        QSet<QString> standards;
        auto strongestStatus = QStringLiteral("unknown");
        for (const auto entryIndex : group.entries) {
            const CompilationEntryView view(&snapshot_.entries.at(entryIndex));
            if (!view.targetLabel().isEmpty()) {
                targets.insert(view.targetLabel());
            }
            if (!view.compilerLabel().isEmpty()) {
                compilers.insert(view.compilerLabel());
            }
            if (!view.standard().isEmpty()) {
                standards.insert(view.standard());
            }
            if (statusPriority(view.sourceStatus()) > statusPriority(strongestStatus)) {
                strongestStatus = view.sourceStatus();
            }
        }
        group.status = strongestStatus;
        group.target = collapsedLabel(targets, tr("%1 targets"));
        group.compiler = collapsedLabel(compilers, tr("%1 compilers"));
        group.standard = collapsedLabel(standards, tr("%1 standards"));
    }
}

}  // namespace buildscope
