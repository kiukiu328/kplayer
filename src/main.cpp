#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QUrl>

#include <clocale>
#include <cstdlib>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // libmpv requires the C numeric locale; Qt may change it at startup.
    std::setlocale(LC_NUMERIC, "C");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QCoreApplication::setApplicationName(QStringLiteral("KPlayer"));

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("videoSource"),
         QUrl::fromLocalFile(QStringLiteral(KPLAYER_DUMMY_VIDEO_PATH))}
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
    engine.loadFromModule("KPlayer", "Main");

    return app.exec();
}
