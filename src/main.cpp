#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName("StardewTranslator");
    QCoreApplication::setApplicationVersion("1.0");
    QCoreApplication::setOrganizationName("StardewTranslator");

    MainWindow window;
    window.show();
    return application.exec();
}
