#include "buildscope/main_window.hpp"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BuildScope"));
    app.setOrganizationName(QStringLiteral("BuildScope"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/buildscope.svg")));

    buildscope::MainWindow window;
    if (app.arguments().size() > 1) {
        window.loadSnapshot(app.arguments().at(1));
    }
    window.show();
    return app.exec();
}
