#include "MainWindow.h"
#include "Ui.h"
#include "Audio.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("MetroBuilder");
    QApplication::setOrganizationName("MetroBuilder");

    Theme::apply();
    Audio::instance().start();
    QObject::connect(&app, &QApplication::aboutToQuit, [] { Audio::instance().stop(); });

    MainWindow w;
    w.show();
    return app.exec();
}
