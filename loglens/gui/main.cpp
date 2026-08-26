#include <QApplication>

#include "main_window.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("loglens"));
    MainWindow window;
    window.show();
    const QStringList args = app.arguments();
    if (args.size() > 1) {
        window.openPath(args.at(1));
    }
    return app.exec();
}
