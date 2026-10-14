#include <LayerShellQt/Window>
#include <QBackingStore>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWindow>

// This is a passive view of the test display, never a keyboard or mouse target.
class TestMonitor : public QWindow
{
public:
    TestMonitor(const QString &framePath, const QString &readyPath, QScreen *targetScreen)
        : m_store(this), m_framePath(framePath), m_readyPath(readyPath),
          m_outputName(targetScreen->name())
    {
        setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus
                 | Qt::WindowTransparentForInput);
        setTitle(QStringLiteral("KPlayer automated tests"));
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] {
            const QImage frame(m_framePath);
            if (!frame.isNull())
                m_frame = frame;
            if (isExposed())
                render();
        });
        m_timer.start(250);
    }

protected:
    void exposeEvent(QExposeEvent *) override
    {
        if (isExposed()) {
            render();
            QFile ready(m_readyPath);
            if (ready.open(QIODevice::WriteOnly))
                ready.write(m_outputName.toUtf8());
        }
    }

private:
    void render()
    {
        m_store.resize(size());
        const QRect area(QPoint{}, size());
        m_store.beginPaint(area);
        {
            QPainter painter(m_store.paintDevice());
            painter.fillRect(area, QColor("#101010"));
            painter.setPen(Qt::white);
            painter.drawText(area.adjusted(16, 8, -16, -8), Qt::AlignTop | Qt::AlignLeft,
                             QStringLiteral("KPlayer tests — isolated display"));
            if (!m_frame.isNull()) {
                const QRect viewport = area.adjusted(16, 36, -16, -16);
                const QSize scaled = m_frame.size().scaled(viewport.size(), Qt::KeepAspectRatio);
                const QRect target(viewport.center() - QPoint(scaled.width() / 2, scaled.height() / 2),
                                   scaled);
                painter.drawImage(target, m_frame);
            }
        }
        m_store.endPaint();
        m_store.flush(area);
    }

    QBackingStore m_store;
    QString m_framePath;
    QString m_readyPath;
    QString m_outputName;
    QImage m_frame;
    QTimer m_timer;
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 3 || app.platformName() != QStringLiteral("wayland")) {
        qCritical("Test preview requires Wayland, a frame path, and a ready path");
        return 1;
    }
    const QString requestedScreen = qEnvironmentVariable("KPLAYER_TEST_SCREEN");
    QScreen *second = nullptr;
    for (auto *screen : app.screens()) {
        if ((!requestedScreen.isEmpty() && screen->name() == requestedScreen)
            || (requestedScreen.isEmpty() && screen != app.primaryScreen())) {
            second = screen;
            break;
        }
    }
    // Wayland's Qt screen ordering need not match KDE's output priorities. An
    // explicitly selected output takes precedence over Qt's primary screen.
    if (!second || app.screens().size() < 2) {
        qCritical("No requested second monitor found; refusing to launch the preview");
        return 1;
    }
    TestMonitor window(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]), second);
    window.setScreen(second);
    auto *layer = LayerShellQt::Window::get(&window);
    layer->setScreen(second);
    layer->setScope(QStringLiteral("kplayer-test-preview"));
    layer->setLayer(LayerShellQt::Window::LayerTop);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                      | LayerShellQt::Window::AnchorBottom
                      | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight);
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setActivateOnShow(false);
    QObject::connect(&app, &QGuiApplication::screenRemoved, &app, [second](QScreen *screen) {
        if (screen == second)
            QCoreApplication::exit(1);
    });
    qInfo("Test preview on %s; keyboard focus disabled", qPrintable(second->name()));
    window.show();
    return app.exec();
}
