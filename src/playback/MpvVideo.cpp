#include "MpvVideo.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickOpenGLUtils>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <utility>

class MpvRenderer : public QObject, public QQuickFramebufferObject::Renderer
{
    Q_OBJECT

public:
    explicit MpvRenderer(std::shared_ptr<mpv_handle> mpv) : m_mpv(std::move(mpv)) {}

    ~MpvRenderer() override
    {
        if (m_context) {
            mpv_render_context_set_update_callback(m_context, nullptr, nullptr);
            mpv_render_context_free(m_context);
        }
    }

    void synchronize(QQuickFramebufferObject *item) override
    {
        // Qt blocks the GUI thread here, so reading the item's source is safe.
        m_source = static_cast<MpvVideo *>(item)->source();
    }

    void render() override
    {
        QQuickOpenGLUtils::resetOpenGLState();
        if (!m_initialized) {
            m_initialized = true;
            if (m_mpv) {
                mpv_opengl_init_params glInit{[](void *, const char *name) -> void * {
                    return reinterpret_cast<void *>(QOpenGLContext::currentContext()
                                                        ->getProcAddress(name));
                }, nullptr};
                mpv_render_param params[] = {
                    {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                    {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
                    {MPV_RENDER_PARAM_INVALID, nullptr}
                };
                const int result = mpv_render_context_create(&m_context, m_mpv.get(), params);
                if (result < 0) {
                    emit failed(QStringLiteral("Cannot initialize video rendering: %1")
                                    .arg(QString::fromUtf8(mpv_error_string(result))));
                } else {
                    mpv_render_context_set_update_callback(m_context, [](void *ctx) {
                        // Only queue a GUI update from mpv's worker thread.
                        emit static_cast<MpvRenderer *>(ctx)->frameReady();
                    }, this);
                }
            }
        }

        if (m_context) {
            if (m_source != m_loadedSource) {
                m_loadedSource = m_source;
                const QByteArray path = (m_source.isLocalFile() ? m_source.toLocalFile()
                                                               : m_source.toString()).toUtf8();
                const char *load[] = {"loadfile", path.constData(), nullptr};
                const char *stop[] = {"stop", nullptr};
                // Async commands are safe on the render thread; synchronous ones can deadlock.
                const int result = mpv_command_async(m_mpv.get(), 0,
                                                     path.isEmpty() ? stop : load);
                if (result < 0)
                    emit failed(QString::fromUtf8(mpv_error_string(result)));
            }
            auto *fbo = framebufferObject();
            mpv_opengl_fbo target{static_cast<int>(fbo->handle()),
                                  fbo->width(), fbo->height(), 0};
            mpv_render_param params[] = {
                {MPV_RENDER_PARAM_OPENGL_FBO, &target},
                {MPV_RENDER_PARAM_INVALID, nullptr}
            };
            mpv_render_context_update(m_context);
            mpv_render_context_render(m_context, params);
        } else {
            auto *gl = QOpenGLContext::currentContext()->functions();
            gl->glClearColor(0, 0, 0, 1);
            gl->glClear(GL_COLOR_BUFFER_BIT);
        }
        QQuickOpenGLUtils::resetOpenGLState();
    }

signals:
    void frameReady();
    void failed(const QString &message);

private:
    std::shared_ptr<mpv_handle> m_mpv;
    mpv_render_context *m_context = nullptr;
    bool m_initialized = false;
    QUrl m_source;
    QUrl m_loadedSource;
};

MpvVideo::MpvVideo(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
    auto *handle = mpv_create();
    if (!handle) {
        setError(QStringLiteral("Cannot create the mpv player."));
        return;
    }
    m_mpv = {handle, mpv_terminate_destroy};
    for (const auto &[name, value] : {
             std::pair{"vo", "libmpv"},
             std::pair{"loop-file", "inf"},
             std::pair{"terminal", "no"}}) {
        const int result = mpv_set_option_string(m_mpv.get(), name, value);
        if (result < 0) {
            setError(QString::fromUtf8(mpv_error_string(result)));
            m_mpv.reset();
            return;
        }
    }
    const int result = mpv_initialize(m_mpv.get());
    if (result < 0) {
        setError(QStringLiteral("Cannot initialize mpv: %1")
                     .arg(QString::fromUtf8(mpv_error_string(result))));
        m_mpv.reset();
        return;
    }
    mpv_set_wakeup_callback(m_mpv.get(), [](void *ctx) {
        auto *video = static_cast<MpvVideo *>(ctx);
        QMetaObject::invokeMethod(video, &MpvVideo::processEvents, Qt::QueuedConnection);
    }, this);
}

MpvVideo::~MpvVideo()
{
    if (m_mpv)
        mpv_set_wakeup_callback(m_mpv.get(), nullptr, nullptr);
}

QQuickFramebufferObject::Renderer *MpvVideo::createRenderer() const
{
    auto *renderer = new MpvRenderer(m_mpv);
    auto *self = const_cast<MpvVideo *>(this);
    connect(renderer, &MpvRenderer::frameReady, self, &MpvVideo::update, Qt::QueuedConnection);
    connect(renderer, &MpvRenderer::failed, self, &MpvVideo::setError, Qt::QueuedConnection);
    return renderer;
}

void MpvVideo::setSource(const QUrl &source)
{
    if (m_source == source)
        return;
    m_source = source;
    if (m_mpv)
        setError({});
    emit sourceChanged();
    update();
}

void MpvVideo::setError(const QString &message)
{
    if (m_errorString == message)
        return;
    m_errorString = message;
    if (!message.isEmpty())
        qWarning().noquote() << message;
    emit errorStringChanged();
}

void MpvVideo::processEvents()
{
    while (const auto *event = mpv_wait_event(m_mpv.get(), 0)) {
        if (event->event_id == MPV_EVENT_NONE)
            break;
        if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->error < 0)
            setError(QString::fromUtf8(mpv_error_string(event->error)));
        if (event->event_id == MPV_EVENT_END_FILE) {
            const auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR)
                setError(QStringLiteral("Cannot play %1: %2. Generate assets/dummy.mp4 using the README command.")
                             .arg(m_source.toDisplayString(),
                                  QString::fromUtf8(mpv_error_string(end->error))));
        }
    }
}

#include "MpvVideo.moc"
