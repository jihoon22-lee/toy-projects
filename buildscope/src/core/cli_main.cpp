#include "buildscope/contract.hpp"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    QTextStream output(stdout);
    QTextStream error(stderr);
    if (arguments.size() != 2) {
        error << "usage: buildscope-cli SNAPSHOT.json\n";
        return 2;
    }
    try {
        const auto snapshot = buildscope::loadSnapshotFile(arguments.at(1));
        output << "Compilation entries: " << snapshot.entries.size() << " · source: "
               << snapshot.sourcePath << " · contract: " << snapshot.schemaVersion << "\n";
    } catch (const buildscope::ContractError &contractError) {
        error << "buildscope-cli: " << contractError.what() << "\n";
        return 2;
    }
    return 0;
}
