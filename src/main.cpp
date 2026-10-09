#include <QGuiApplication>
#include <QIcon>
#include <QFontDatabase>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QCommandLineParser>
#include <QSettings>
#include <QTimer>

#include "FileAssociations.h"
#include "AppLanguage.h"

#include <clocale>
#include <cstdlib>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
#ifdef Q_OS_LINUX
    // Minimal WSL installations can reuse the host's installed CJK font.
    if (qEnvironmentVariableIsSet("WSL_DISTRO_NAME")
        && QFontDatabase::families(QFontDatabase::TraditionalChinese).isEmpty()) {
        const int fontId = QFontDatabase::addApplicationFont(
            QStringLiteral("/mnt/c/Windows/Fonts/msjh.ttc"));
        if (fontId >= 0) {
            QFontDatabase::setApplicationFallbackFontFamilies(
                QChar::Script_Han, QFontDatabase::applicationFontFamilies(fontId));
        }
    }
#endif
    // libmpv requires the C numeric locale; Qt may change it at startup.
    std::setlocale(LC_NUMERIC, "C");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QCoreApplication::setApplicationName(QStringLiteral("KPlayer"));
    QCoreApplication::setOrganizationName(QStringLiteral("KPlayer"));
    QGuiApplication::setDesktopFileName(QStringLiteral("kplayer"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/KPlayer/icons/kplayer.svg")));

    // Load the saved language before CLI help and Windows registration run.
    QQmlApplicationEngine engine;
    engine.singletonInstance<AppLanguage *>("KPlayer", "AppLanguage");
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "KPlayer media player"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("media"),
                                QCoreApplication::translate("main", "Files or URLs to open and play"),
                                QStringLiteral("[media…]"));
    const QCommandLineOption registerTypes(QStringLiteral("register-file-types"),
        QCoreApplication::translate("main", "Register video file types and open Windows Default apps"));
    parser.addOption(registerTypes);
    parser.process(app);

    FileAssociations associations;
    if (parser.isSet(registerTypes))
        return associations.openDefaultApps() ? EXIT_SUCCESS : EXIT_FAILURE;
    bool setupFileTypes = false;
    if (associations.supported()) {
        QSettings settings;
        if (associations.registerVideoTypes())
            setupFileTypes = !settings.value(QStringLiteral("integration/defaultAppsOffered"), false).toBool();
        else
            qWarning().noquote() << associations.errorString();
    }

    QList<QUrl> startupFiles;
    for (const QString &argument : parser.positionalArguments())
        startupFiles.append(QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile));
    engine.setInitialProperties({
        {QStringLiteral("startupFiles"), QVariant::fromValue(startupFiles)}
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
    engine.loadFromModule("KPlayer", "Main");
    if (setupFileTypes && !engine.rootObjects().isEmpty()) {
        QTimer::singleShot(750, &app, [&associations] {
            if (associations.openDefaultApps()) {
                QSettings settings;
                settings.setValue(QStringLiteral("integration/defaultAppsOffered"), true);
            }
        });
    }

    return app.exec();
}
