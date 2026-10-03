#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include "loglens/gui/main_window.hpp"
#include "loglens/version.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral(LOGLENS_VERSION));
    app.setApplicationName(QStringLiteral("loglens"));
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Log investigation with bounded retention and full-file search"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("session"), QStringLiteral("Open an investigation session"),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("format-plugin"),
                      QStringLiteral("Use a declarative parser plugin"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("smoke-exit-ms"),
                      QStringLiteral("Exit after a bounded smoke interval"), QStringLiteral("ms")});
    parser.addPositionalArgument(QStringLiteral("log"), QStringLiteral("Log source path"),
                                 QStringLiteral("[log]"));
    parser.process(app);
    if (parser.positionalArguments().size() > 1 ||
        (parser.isSet(QStringLiteral("session")) && !parser.positionalArguments().isEmpty()))
        return 2;
    MainWindow window;
    if (parser.isSet(QStringLiteral("format-plugin")) &&
        !window.setFormatPluginPath(parser.value(QStringLiteral("format-plugin"))))
        return 2;
    window.show();
    if (parser.isSet(QStringLiteral("session"))) {
        if (!window.openSession(parser.value(QStringLiteral("session"))))
            return 2;
    } else if (!parser.positionalArguments().isEmpty())
        window.openPath(parser.positionalArguments().front());
    if (parser.isSet(QStringLiteral("smoke-exit-ms"))) {
        bool ok = false;
        const int interval = parser.value(QStringLiteral("smoke-exit-ms")).toInt(&ok);
        if (!ok || interval < 1 || interval > 60000)
            return 2;
        QTimer::singleShot(interval, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
