#include <QApplication>
#include <QCommandLineParser>
#include "diskmap/version.hpp"
#include "diskmap/gui/main_window.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("diskmap"));
    app.setApplicationVersion(QString::fromLatin1(diskmap::kVersion));
    QCommandLineParser parser;
    parser.setApplicationDescription("Disk usage, duplicate review and recoverable Trash");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addPositionalArgument("path", "Directory or file to scan", "[path]");
    QCommandLineOption load("load-snapshot", "Open a snapshot for read-only inspection", "file");
    parser.addOption(load); parser.process(app);
    if (parser.positionalArguments().size() > 1 || (parser.isSet(load) && !parser.positionalArguments().empty())) parser.showHelp(2);
    MainWindow window;
    window.show();
    if (parser.isSet(load)) window.loadSnapshotPath(parser.value(load));
    else if (!parser.positionalArguments().empty()) window.scanPath(parser.positionalArguments().front());
    return app.exec();
}
