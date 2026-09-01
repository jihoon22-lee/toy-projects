#include "buildscope/compilation_model.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSortFilterProxyModel>
#include <QTextStream>

#include <utility>

namespace {

buildscope::Snapshot syntheticSnapshot(qsizetype entryCount) {
    constexpr qsizetype kConfigurationsPerSource = 4;
    buildscope::Snapshot snapshot;
    snapshot.schemaVersion = QString::fromLatin1(buildscope::kSnapshotSchemaV2);
    snapshot.producerVersion = QStringLiteral("benchmark");
    snapshot.sourcePath = QStringLiteral("synthetic/compile_commands.json");
    snapshot.projectRoot = QStringLiteral("synthetic");
    snapshot.entries.reserve(entryCount);
    for (qsizetype index = 0; index < entryCount; ++index) {
        const auto sourceIndex = index / kConfigurationsPerSource;
        const auto configurationIndex = index % kConfigurationsPerSource;
        const auto source = QStringLiteral("src/unit_%1.cpp").arg(sourceIndex, 6, 10,
                                                                  QLatin1Char('0'));
        buildscope::SnapshotEntry entry;
        entry.file = source;
        entry.directory = QStringLiteral("build");
        entry.hasNormalized = true;
        entry.normalized.argv =
            QStringList{QStringLiteral("c++"), QStringLiteral("-c"), source};
        entry.normalized.commandStyle = QStringLiteral("posix");
        entry.normalized.invocationSource = QStringLiteral("arguments");
        entry.normalized.compiler.family = QStringLiteral("gcc");
        entry.normalized.compiler.name = QStringLiteral("c++");
        entry.normalized.configuration =
            QStringLiteral("synthetic:%1:%2").arg(sourceIndex).arg(configurationIndex);
        entry.normalized.directory.path = QStringLiteral("build");
        entry.normalized.directory.scope = QStringLiteral("project");
        entry.normalized.directory.style = QStringLiteral("posix");
        entry.normalized.directory.exists = true;
        entry.normalized.language = QStringLiteral("c++");
        entry.normalized.source.path = source;
        entry.normalized.source.scope = QStringLiteral("project");
        entry.normalized.source.style = QStringLiteral("posix");
        entry.normalized.source.exists = true;
        entry.normalized.standard = QStringLiteral("c++20");
        entry.normalized.target.buildTarget =
            QStringLiteral("target_%1").arg(configurationIndex);
        entry.hasState = true;
        entry.state.entryIndex = index;
        entry.state.sourceConfigurationCount = kConfigurationsPerSource;
        entry.state.sourceStatus = index % 97 == 0 ? QStringLiteral("stale")
                                                   : QStringLiteral("present");
        snapshot.entries.append(std::move(entry));
    }
    return snapshot;
}

qsizetype positiveArgument(const QStringList &arguments, int position,
                           qsizetype defaultValue) {
    if (position >= arguments.size()) {
        return defaultValue;
    }
    bool valid = false;
    const auto value = arguments.at(position).toLongLong(&valid);
    return valid && value > 0 ? value : -1;
}

}  // namespace

int main(int argc, char *argv[]) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    const auto entryCount = positiveArgument(arguments, 1, 100000);
    const auto budgetMilliseconds = positiveArgument(arguments, 2, 10000);
    if (entryCount < 1 || budgetMilliseconds < 1) {
        QTextStream(stderr) << "usage: buildscope-model-benchmark [positive-entry-count] "
                               "[positive-budget-ms]\n";
        return 2;
    }

    auto snapshot = syntheticSnapshot(entryCount);
    buildscope::CompilationTreeModel model;
    QElapsedTimer timer;
    timer.start();
    model.setSnapshot(std::move(snapshot));
    const auto elapsedMilliseconds = timer.elapsed();
    QSortFilterProxyModel filter;
    filter.setSourceModel(&model);
    filter.setFilterRole(buildscope::SearchTextRole);
    filter.setFilterKeyColumn(-1);
    filter.setFilterCaseSensitivity(Qt::CaseInsensitive);
    filter.setRecursiveFilteringEnabled(true);
    timer.restart();
    filter.setFilterFixedString(QStringLiteral("unit_024999"));
    const auto filteredSources = filter.rowCount();
    const auto filterMilliseconds = timer.elapsed();
    const auto expectedSources = (entryCount + 3) / 4;
    const auto lastSource = model.index(model.sourceCount() - 1, 0);
    const auto correctness = model.entryCount() == entryCount &&
                             model.sourceCount() == expectedSources &&
                             lastSource.isValid() && model.rowCount(lastSource) > 0 &&
                             (entryCount != 100000 || filteredSources == 1) &&
                             !lastSource.data(buildscope::SourcePathRole).toString().isEmpty();
    QTextStream(stdout) << "{\"entries\":" << entryCount << ",\"sources\":"
                        << model.sourceCount() << ",\"model_build_ms\":"
                        << elapsedMilliseconds << ",\"filter_ms\":"
                        << filterMilliseconds << ",\"filtered_sources\":"
                        << filteredSources << ",\"budget_ms\":"
                        << budgetMilliseconds << ",\"correct\":"
                        << (correctness ? "true" : "false") << "}\n";
    if (!correctness) {
        return 1;
    }
    return elapsedMilliseconds <= budgetMilliseconds &&
                   filterMilliseconds <= budgetMilliseconds
               ? 0
               : 1;
}
