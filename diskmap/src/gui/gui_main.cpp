#include <QApplication>

#include "diskmap/gui/main_window.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("diskmap"));
    MainWindow window;
    window.show();
    // Optional path argument: skip the dialog and scan straight away.
    const QStringList args = app.arguments();
    if (args.size() > 1) {
        window.scanPath(args.at(1));
    }
    return app.exec();
}
