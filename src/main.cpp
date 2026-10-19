#include "mainwindow.h"
#include <QApplication>

int main([[maybe_unused]] int argc, [[maybe_unused]] char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("YT2Ogg");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("YT2Ogg");
    QApplication::setOrganizationDomain("yt2ogg.dev");

    MainWindow window;
    window.show();

    return QApplication::exec();
}
