#include <QGuiApplication>
#include <QFontDatabase>
#include <QDir>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

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

    QQmlApplicationEngine engine;
    // Keep the preview idle until Play is pressed. Accept a file or URL as argv[1].
    const QString mediaArgument = app.arguments().value(1);
    const QString samplePath = QDir::current().filePath(QStringLiteral("assets/dummy.mp4"));
    const QUrl mediaSource = !mediaArgument.isEmpty()
        ? QUrl::fromUserInput(mediaArgument, QDir::currentPath(), QUrl::AssumeLocalFile)
        : (QFileInfo::exists(samplePath) ? QUrl::fromLocalFile(samplePath) : QUrl{});
    engine.setInitialProperties({{QStringLiteral("mediaSource"), mediaSource}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
    engine.loadFromModule("KPlayer", "Main");

    return app.exec();
}
