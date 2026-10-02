#include "MainWindow.h"
#include "Ui.h"
#include "Audio.h"

#include <QApplication>
#include <QTimer>

#ifdef Q_OS_WIN
#include <QDir>
#include <QSettings>
#include <shlobj.h>

// Associe les fichiers .metro au jeu pour l'utilisateur courant (icône de sauvegarde, ouverture par double-clic).
// Mis à jour seulement si l'exécutable a changé d'emplacement.
static void registerSaveFileType()
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString command = QStringLiteral("\"%1\" \"%2\"").arg(exe, QStringLiteral("%1"));
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"), QSettings::NativeFormat);
    if (classes.value(QStringLiteral(".metro/Default")).toString() == QLatin1String("MetroBuilder.Save")
        && classes.value(QStringLiteral("MetroBuilder.Save/shell/open/command/Default")).toString() == command)
        return;
    classes.setValue(QStringLiteral(".metro/Default"), QStringLiteral("MetroBuilder.Save"));
    classes.setValue(QStringLiteral("MetroBuilder.Save/Default"), QStringLiteral("Partie Metro Builder"));
    classes.setValue(QStringLiteral("MetroBuilder.Save/DefaultIcon/Default"), QStringLiteral("\"%1\",1").arg(exe));
    classes.setValue(QStringLiteral("MetroBuilder.Save/shell/open/command/Default"), command);
    classes.sync();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); // rafraîchit les icônes de l'Explorateur
}
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("MetroBuilder");
    QApplication::setOrganizationName("MetroBuilder");
#ifdef Q_OS_WIN
    registerSaveFileType();
#endif

    Theme::apply();
    Audio::instance().start();
    QObject::connect(&app, &QApplication::aboutToQuit, [] { Audio::instance().stop(); });

    MainWindow w;
    w.show();
    // « metrobuilder partie.metro » : ouverture de la sauvegarde (double-clic dans le gestionnaire de fichiers)
    const QStringList args = app.arguments().mid(1);
    for (const QString &arg : args)
        if (!arg.startsWith(QLatin1Char('-'))) {
            QTimer::singleShot(0, &w, [&w, arg] { w.openFile(arg); });
            break;
        }
    return app.exec();
}
