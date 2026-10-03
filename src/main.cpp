#include <QGuiApplication>
#include <QFontDatabase>
#include <QDir>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QCommandLineParser>
#include <QSettings>
#include <QTimer>

#include "FileAssociations.h"

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

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("KPlayer media player"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("media"),
                                QStringLiteral("Files or URLs to open and play"),
                                QStringLiteral("[media…]"));
    const QCommandLineOption registerTypes(QStringLiteral("register-file-types"),
        QStringLiteral("Register video file types and open Windows Default apps"));
    const QCommandLineOption noScan(QStringLiteral("no-scan"),
        QStringLiteral("Skip automatic video discovery for this launch"));
    const QCommandLineOption scanFolder(QStringLiteral("scan-folder"),
        QStringLiteral("Search this folder instead of all local drives (repeatable)"),
        QStringLiteral("folder"));
    parser.addOptions({registerTypes, noScan, scanFolder});
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

    QQmlApplicationEngine engine;
    QList<QUrl> startupFiles;
    for (const QString &argument : parser.positionalArguments())
        startupFiles.append(QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile));
    QList<QUrl> scanRoots;
    for (const QString &folder : parser.values(scanFolder))
        scanRoots.append(QUrl::fromLocalFile(QFileInfo(folder).absoluteFilePath()));
    // The optional sample stays idle. Files opened from Explorer start immediately.
    const QString samplePath = QDir::current().filePath(QStringLiteral("assets/dummy.mp4"));
    const QUrl mediaSource = startupFiles.isEmpty() && QFileInfo::exists(samplePath)
        ? QUrl::fromLocalFile(samplePath) : QUrl{};
    engine.setInitialProperties({
        {QStringLiteral("mediaSource"), mediaSource},
        {QStringLiteral("startupFiles"), QVariant::fromValue(startupFiles)},
        {QStringLiteral("discoverVideosOnStartup"), !parser.isSet(noScan)},
        {QStringLiteral("videoSearchRoots"), QVariant::fromValue(scanRoots)}
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
