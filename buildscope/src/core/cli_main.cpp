#include "buildscope/contract.hpp"
#include "buildscope/diff.hpp"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    QTextStream output(stdout);
    QTextStream error(stderr);
    const auto diffMode = arguments.size() == 3 && arguments.at(1) == QLatin1String("--diff");
    if (arguments.size() != 2 && !diffMode) {
        error << "usage: buildscope-cli SNAPSHOT.json\n"
              << "       buildscope-cli --diff DIFF.json\n";
        return 2;
    }
    try {
        if (diffMode) {
            const auto report = buildscope::loadDiffFile(arguments.at(2));
            output << "Configuration changes: " << report.summary.visibleUnits
                   << " visible · " << report.summary.suppressedUnits
                   << " suppressed · " << report.summary.unchanged
                   << " unchanged · contract: " << report.schemaVersion << "\n";
            return 0;
        }
        const auto snapshot = buildscope::loadSnapshotFile(arguments.at(1));
        output << "Compilation entries: " << snapshot.entries.size() << " · source: "
               << snapshot.sourcePath << " · contract: " << snapshot.schemaVersion << "\n";
    } catch (const buildscope::ContractError &contractError) {
        error << "buildscope-cli: " << contractError.what() << "\n";
        return 2;
    }
    return 0;
}
