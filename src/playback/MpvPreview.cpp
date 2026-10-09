#include "MpvPreview.h"
#include "MpvErrors.h"

#include <QOpenGLContext>
#include <QLoggingCategory>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickOpenGLUtils>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace {
constexpr quint64 idleQueryMask = quint64{1} << 63;
Q_LOGGING_CATEGORY(previewLog, "kplayer.preview", QtWarningMsg)
}

class MpvPreviewRenderer : public QObject, public QQuickFramebufferObject::Renderer
{
    Q_OBJECT

public:
    explicit MpvPreviewRenderer(std::shared_ptr<mpv_handle> mpv)
        : m_mpv(std::move(mpv)) {}

    ~MpvPreviewRenderer() override
    {
        if (m_context) {
            mpv_render_context_set_update_callback(m_context, nullptr, nullptr);
            mpv_render_context_free(m_context);
        }
    }

    void synchronize(QQuickFramebufferObject *item) override
    {
        m_revision = static_cast<MpvPreview *>(item)->m_renderRevision;
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
                    emit failed(MpvErrors::description(result));
                } else {
                    mpv_render_context_set_update_callback(m_context, [](void *context) {
                        emit static_cast<MpvPreviewRenderer *>(context)->frameReady();
                    }, this);
                    emit initialized();
                }
            }
        }
        if (m_context) {
            auto *fbo = framebufferObject();
            mpv_opengl_fbo target{static_cast<int>(fbo->handle()),
                                  fbo->width(), fbo->height(), 0};
            mpv_render_param params[] = {
                {MPV_RENDER_PARAM_OPENGL_FBO, &target},
                {MPV_RENDER_PARAM_INVALID, nullptr}
            };
            mpv_render_context_update(m_context);
            const int result = mpv_render_context_render(m_context, params);
            if (result < 0)
                emit failed(MpvErrors::description(result));
            else if (m_revision)
                emit frameRendered(m_revision);
        } else {
            auto *gl = QOpenGLContext::currentContext()->functions();
            gl->glClearColor(0, 0, 0, 1);
            gl->glClear(GL_COLOR_BUFFER_BIT);
        }
        QQuickOpenGLUtils::resetOpenGLState();
    }

signals:
    void initialized();
    void frameReady();
    void frameRendered(quint64 revision);
    void failed(const QString &message);

private:
    std::shared_ptr<mpv_handle> m_mpv;
    mpv_render_context *m_context = nullptr;
    bool m_initialized = false;
    quint64 m_revision = 0;
};

MpvPreview::MpvPreview(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_debounce, &QTimer::timeout, this, &MpvPreview::serviceRequest);

    auto *handle = mpv_create();
    if (!handle) {
        setError(tr("Preview is unavailable."));
        return;
    }
    m_mpv = {handle, mpv_terminate_destroy};
    for (const auto &[name, value] : {
             std::pair{"vo", "libmpv"}, std::pair{"terminal", "no"},
             std::pair{"config", "no"}, std::pair{"load-scripts", "no"},
             std::pair{"aid", "no"}, std::pair{"sid", "no"},
             std::pair{"audio-display", "no"}, std::pair{"osd-level", "0"},
             std::pair{"pause", "yes"}, std::pair{"keep-open", "yes"},
             std::pair{"idle", "yes"},
             std::pair{"hr-seek", "yes"}, std::pair{"cache", "no"},
             std::pair{"demuxer-readahead-secs", "0"}}) {
        const int result = mpv_set_option_string(handle, name, value);
        if (result < 0) {
            setError(MpvErrors::description(result));
            m_mpv.reset();
            return;
        }
    }
    const int result = mpv_initialize(handle);
    if (result < 0) {
        setError(MpvErrors::description(result));
        m_mpv.reset();
        return;
    }
    mpv_set_wakeup_callback(handle, [](void *context) {
        auto *preview = static_cast<MpvPreview *>(context);
        QMetaObject::invokeMethod(preview, &MpvPreview::processEvents, Qt::QueuedConnection);
    }, this);
    mpv_observe_property(handle, 0, "video-params/w", MPV_FORMAT_INT64);
    mpv_observe_property(handle, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(handle, 0, "idle-active", MPV_FORMAT_FLAG);
}

MpvPreview::~MpvPreview()
{
    if (m_mpv)
        mpv_set_wakeup_callback(m_mpv.get(), nullptr, nullptr);
}

QQuickFramebufferObject::Renderer *MpvPreview::createRenderer() const
{
    auto *renderer = new MpvPreviewRenderer(m_mpv);
    auto *self = const_cast<MpvPreview *>(this);
    connect(renderer, &MpvPreviewRenderer::frameReady, self, &MpvPreview::update,
            Qt::QueuedConnection);
    connect(renderer, &MpvPreviewRenderer::initialized, self, [self] {
        self->m_rendererReady = true;
        self->serviceRequest();
    }, Qt::QueuedConnection);
    connect(renderer, &MpvPreviewRenderer::failed, self, &MpvPreview::setError,
            Qt::QueuedConnection);
    connect(renderer, &MpvPreviewRenderer::frameRendered, self, [self](quint64 revision) {
        if (self->m_active && self->m_hasVideo && revision == self->m_requestRevision
            && revision == self->m_renderRevision && self->m_loadedSource == self->m_source)
            self->setReady(true);
    }, Qt::QueuedConnection);
    return renderer;
}

void MpvPreview::setSource(const QUrl &source)
{
    if (m_source == source)
        return;
    m_source = source;
    m_failedSource = QUrl{};
    if (m_mpv)
        setError({});
    requestFrame();
    emit sourceChanged();
    // Stop the old source immediately, even if the item is now hidden.
    serviceRequest();
}

void MpvPreview::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    m_failedSource = QUrl{};
    if (active && m_mpv)
        setError({});
    requestFrame();
    emit activeChanged();
    if (!active) {
        m_debounce.stop();
        serviceRequest();
    }
}

void MpvPreview::setPosition(double seconds)
{
    if (!std::isfinite(seconds))
        return;
    seconds = std::max(0.0, seconds);
    if (m_position == seconds)
        return;
    m_position = seconds;
    requestFrame();
    emit positionChanged();
}

void MpvPreview::requestFrame()
{
    ++m_requestRevision;
    m_renderRevision = 0;
    m_seekPending = true;
    setReady(false);
    if (m_active && !m_source.isEmpty()) {
        m_debounce.start();
        update();
    }
}

bool MpvPreview::submitCommand(Command command, const char **arguments)
{
    qCDebug(previewLog) << "Preview command" << arguments[0]
                       << "revision" << m_requestRevision;
    m_command = command;
    const int result = mpv_command_async(m_mpv.get(), ++m_commandId, arguments);
    if (result >= 0)
        return true;
    m_command = Command::None;
    m_failedSource = m_source;
    setError(MpvErrors::description(result));
    return false;
}

void MpvPreview::resetLoadedState()
{
    m_loadedSource = QUrl{};
    m_loading = false;
    m_loaded = false;
    m_stopping = false;
    m_stopAcknowledged = false;
    m_idleQueryId = 0;
    m_seeking = false;
    m_seekStarted = false;
    m_hasVideo = false;
    m_duration = 0;
    m_requestedPosition = -1;
    m_renderRevision = 0;
    setReady(false);
}

void MpvPreview::serviceRequest()
{
    if (!m_mpv || m_command != Command::None || m_stopping)
        return;
    if (!m_loadedSource.isEmpty()
        && (!m_active || m_source.isEmpty() || m_source != m_loadedSource)) {
        const char *stop[] = {"stop", nullptr};
        m_stopping = true;
        m_stopAcknowledged = false;
        if (!submitCommand(Command::Stop, stop))
            m_stopping = false;
        return;
    }
    if (!m_active || m_source.isEmpty() || !m_rendererReady || m_debounce.isActive()
        || m_failedSource == m_source)
        return;
    if (m_loadedSource.isEmpty()) {
        m_loadedSource = m_source;
        m_loading = true;
        m_seekPending = true;
        const QByteArray path = (m_source.isLocalFile() ? m_source.toLocalFile()
                                                       : m_source.toString()).toUtf8();
        const char *load[] = {"loadfile", path.constData(), "replace", nullptr};
        if (!submitCommand(Command::Load, load))
            resetLoadedState();
        return;
    }
    if (!m_loaded || m_loading || m_seeking || !m_seekPending)
        return;
    m_requestedPosition = m_position;
    if (m_duration > 0)
        m_requestedPosition = std::min(m_requestedPosition, m_duration);
    m_seekRevision = m_requestRevision;
    const QByteArray target = QByteArray::number(m_requestedPosition, 'f', 3);
    const char *seek[] = {"seek", target.constData(), "absolute+exact", nullptr};
    m_seekPending = false;
    m_seekStarted = false;
    m_seeking = submitCommand(Command::Seek, seek);
}

void MpvPreview::setReady(bool ready)
{
    if (m_ready == ready)
        return;
    m_ready = ready;
    qCDebug(previewLog) << "Preview ready" << ready << "revision" << m_requestRevision;
    emit readyChanged();
}

void MpvPreview::setError(const QString &message)
{
    if (m_errorString == message)
        return;
    m_errorString = message;
    emit errorStringChanged();
}

void MpvPreview::processEvents()
{
    if (!m_mpv)
        return;
    while (const auto *event = mpv_wait_event(m_mpv.get(), 0)) {
        if (event->event_id == MPV_EVENT_NONE)
            break;
        if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->reply_userdata == m_commandId) {
            const Command completed = std::exchange(m_command, Command::None);
            if (completed == Command::Stop) {
                // Stop's command reply precedes actual unloading. Wait until
                // mpv is idle before associating events with a new source.
                m_stopAcknowledged = true;
                m_idleQueryId = idleQueryMask | m_commandId;
                mpv_get_property_async(m_mpv.get(), m_idleQueryId, "idle-active", MPV_FORMAT_FLAG);
            } else if (event->error < 0) {
                m_failedSource = m_loadedSource;
                m_loading = false;
                m_seeking = false;
                m_seekStarted = false;
                setError(MpvErrors::description(event->error));
                setReady(false);
            }
        } else if (event->event_id == MPV_EVENT_GET_PROPERTY_REPLY
                   && m_idleQueryId && event->reply_userdata == m_idleQueryId) {
            const auto *property = static_cast<mpv_event_property *>(event->data);
            if (event->error >= 0 && property->data && property->format == MPV_FORMAT_FLAG
                && *static_cast<int *>(property->data) && m_stopping && m_stopAcknowledged)
                resetLoadedState();
        } else if (event->event_id == MPV_EVENT_FILE_LOADED && !m_loadedSource.isEmpty()) {
            m_loading = false;
            m_loaded = true;
        } else if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            const auto *property = static_cast<mpv_event_property *>(event->data);
            if (std::strcmp(property->name, "idle-active") == 0) {
                if (property->data && property->format == MPV_FORMAT_FLAG
                    && *static_cast<int *>(property->data) && m_stopping && m_stopAcknowledged)
                    resetLoadedState();
            } else if (std::strcmp(property->name, "video-params/w") == 0) {
                m_hasVideo = property->data && property->format == MPV_FORMAT_INT64
                    && *static_cast<int64_t *>(property->data) > 0;
                if (!m_hasVideo)
                    setReady(false);
                else if (m_renderRevision == m_requestRevision)
                    update();
            } else if (std::strcmp(property->name, "duration") == 0) {
                m_duration = property->data && property->format == MPV_FORMAT_DOUBLE
                    ? *static_cast<double *>(property->data) : 0;
            }
        } else if (event->event_id == MPV_EVENT_SEEK && m_seeking) {
            m_seekStarted = true;
        } else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART
                   && m_seeking && m_seekStarted) {
            m_seeking = false;
            m_seekStarted = false;
            if (m_active && m_seekRevision == m_requestRevision
                && m_loadedSource == m_source) {
                // Reveal only after the render thread has presented this seek's frame.
                m_renderRevision = m_seekRevision;
                update();
            }
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            const auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR && m_loadedSource == m_source) {
                m_failedSource = m_loadedSource;
                m_loading = false;
                m_loaded = false;
                m_seeking = false;
                m_seekStarted = false;
                setError(tr("No preview available: %1")
                             .arg(MpvErrors::description(end->error)));
                setReady(false);
            }
        }
    }
    // Wait for replies before submitting another command; libmpv can reorder
    // concurrently pending async commands. Pointer movement only keeps the latest seek.
    serviceRequest();
}

#include "MpvPreview.moc"
