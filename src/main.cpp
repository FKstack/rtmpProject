#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("RtmpMonitor"));
    QApplication::setOrganizationName(QStringLiteral("RtmpProject"));

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
