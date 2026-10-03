#include "buildscope/main_window.hpp"

#include <QApplication>
#include <QIcon>
#include <QThreadPool>
#include <iostream>
#include <string_view>

int main(int argc, char *argv[]) {
    if(argc==2 && std::string_view(argv[1])=="--version") {
        std::cout<<"buildscope-gui " BUILDSCOPE_VERSION "\n";return 0;
    }
    if(argc==2 && std::string_view(argv[1])=="--help") {
        std::cout<<"usage: buildscope-gui [compile_commands.json | snapshot.json | diff.json]\n";return 0;
    }
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BuildScope"));
    app.setApplicationVersion(QStringLiteral(BUILDSCOPE_VERSION));
    QThreadPool::globalInstance()->setMaxThreadCount(2);
    app.setOrganizationName(QStringLiteral("BuildScope"));
    buildscope::MainWindow window;
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/buildscope.svg")));
    if (app.arguments().size() > 1) {
        window.openInputAsync(app.arguments().at(1));
    }
    window.show();
    return app.exec();
}
