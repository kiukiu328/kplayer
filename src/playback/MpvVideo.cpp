#include "MpvVideo.h"

#include <QDebug>
#include <QFileInfo>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickOpenGLUtils>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
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
        // Qt blocks the GUI thread here, so reading playback state is safe.
        const auto *video = static_cast<MpvVideo *>(item);
        m_source = video->source();
        m_loadRevision = video->loadRevision();
        m_stopped = video->stopped();
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
            if (m_loadRevision != m_loadedRevision) {
                m_loadedRevision = m_loadRevision;
                const QByteArray path = (m_source.isLocalFile() ? m_source.toLocalFile()
                                                               : m_source.toString()).toUtf8();
                const char *load[] = {"loadfile", path.constData(), nullptr};
                const char *stop[] = {"stop", nullptr};
                // Async commands are safe on the render thread; synchronous ones can deadlock.
                const int result = mpv_command_async(m_mpv.get(), 0,
                                                     m_stopped || path.isEmpty() ? stop : load);
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
            gl->glClearColor(16.0f / 255, 16.0f / 255, 16.0f / 255, 1);
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
    bool m_stopped = true;
    quint64 m_loadRevision = 0;
    quint64 m_loadedRevision = 0;
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
             std::pair{"background", "color"},
             std::pair{"background-color", "#101010"},
             std::pair{"osd-level", "0"},
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
    mpv_observe_property(m_mpv.get(), 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv.get(), 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv.get(), 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv.get(), 0, "seekable", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv.get(), 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv.get(), 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv.get(), 0, "speed", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv.get(), 0, "sub-visibility", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv.get(), 0, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv.get(), 0, "sid", MPV_FORMAT_INT64);
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
    stop();
    m_source = source;
    setPosition(0);
    setDuration(0);
    setSeekable(false);
    if (m_mpv)
        setError({});
    emit sourceChanged();
    update();
}

void MpvVideo::setPlaybackState(bool stopped, bool paused)
{
    if (m_stopped == stopped && m_paused == paused)
        return;
    m_stopped = stopped;
    m_paused = paused;
    emit playbackStateChanged();
}

bool MpvVideo::setPaused(bool paused)
{
    if (!m_mpv)
        return false;
    int value = paused ? 1 : 0;
    const int result = mpv_set_property_async(m_mpv.get(), 0, "pause", MPV_FORMAT_FLAG, &value);
    if (result < 0) {
        setError(QString::fromUtf8(mpv_error_string(result)));
        return false;
    }
    setPlaybackState(m_stopped, paused);
    return true;
}

void MpvVideo::play()
{
    if (!m_mpv || m_source.isEmpty())
        return;
    setError({});
    if (!setPaused(false))
        return;
    if (m_stopped) {
        setPosition(0);
        setPlaybackState(false, false);
        // A revision also reloads the same source after Stop.
        ++m_loadRevision;
        update();
    }
}

void MpvVideo::stop()
{
    clearSubtitleTracks();
    if (m_stopped)
        return;
    if (m_mpv) {
        const char *command[] = {"stop", nullptr};
        const int result = mpv_command_async(m_mpv.get(), 0, command);
        if (result < 0)
            setError(QString::fromUtf8(mpv_error_string(result)));
    }
    setPlaybackState(true, false);
    setPosition(0);
    setSeekable(false);
    ++m_loadRevision;
    update();
}

void MpvVideo::togglePause()
{
    if (m_stopped || m_paused)
        play();
    else
        setPaused(true);
}

void MpvVideo::setMuted(bool muted)
{
    if (!m_mpv || m_muted == muted)
        return;
    int value = muted ? 1 : 0;
    const int result = mpv_set_property_async(m_mpv.get(), 0, "mute", MPV_FORMAT_FLAG, &value);
    if (result < 0) {
        setError(QString::fromUtf8(mpv_error_string(result)));
        return;
    }
    m_muted = muted;
    emit mutedChanged();
}

void MpvVideo::setVolume(double volume)
{
    if (!m_mpv || !std::isfinite(volume))
        return;
    volume = std::clamp(volume, 0.0, 100.0);
    if (m_volume == volume)
        return;
    const int result = mpv_set_property_async(m_mpv.get(), 0, "volume", MPV_FORMAT_DOUBLE, &volume);
    if (result < 0) {
        setError(QString::fromUtf8(mpv_error_string(result)));
        return;
    }
    m_volume = volume;
    emit volumeChanged();
}

void MpvVideo::setSpeed(double speed)
{
    if (!m_mpv || !std::isfinite(speed))
        return;
    speed = std::clamp(speed, 0.2, 5.0);
    if (qFuzzyCompare(m_speed, speed))
        return;
    const int result = mpv_set_property_async(m_mpv.get(), 0, "speed", MPV_FORMAT_DOUBLE, &speed);
    if (result < 0) {
        setError(QString::fromUtf8(mpv_error_string(result)));
        return;
    }
    m_speed = speed;
    emit speedChanged();
}

void MpvVideo::setSubtitlesVisible(bool visible)
{
    if (!m_mpv || m_subtitlesVisible == visible)
        return;
    int value = visible ? 1 : 0;
    const int result = mpv_set_property_async(m_mpv.get(), 0, "sub-visibility", MPV_FORMAT_FLAG, &value);
    if (result < 0) {
        setError(QString::fromUtf8(mpv_error_string(result)));
        return;
    }
    m_subtitlesVisible = visible;
    emit subtitlesVisibleChanged();
}

void MpvVideo::setSubtitleTrack(int id)
{
    if (!m_mpv || !m_mediaLoaded)
        return;
    if (id != -1 && std::none_of(m_subtitleTracks.cbegin(), m_subtitleTracks.cend(),
                                [id](const QVariant &track) {
                                    return track.toMap().value("id").toInt() == id;
                                }))
        return;
    const QByteArray value = id < 0 ? QByteArray("no") : QByteArray::number(id);
    const char *command[] = {"set", "sid", value.constData(), nullptr};
    const int result = mpv_command_async(m_mpv.get(), 0, command);
    if (result < 0) {
        setError(QString::fromUtf8(mpv_error_string(result)));
        return;
    }
    if (id >= 0)
        setSubtitlesVisible(true);
}

bool MpvVideo::addSubtitle(const QUrl &url)
{
    if (!m_mpv || !m_mediaLoaded || !url.isLocalFile()
        || !QFileInfo(url.toLocalFile()).isFile())
        return false;
    const QByteArray path = url.toLocalFile().toUtf8();
    const char *command[] = {"sub-add", path.constData(), "select", nullptr};
    const int result = mpv_command_async(m_mpv.get(), 0, command);
    if (result < 0) {
        setError(QString::fromUtf8(mpv_error_string(result)));
        return false;
    }
    setSubtitlesVisible(true);
    return true;
}

void MpvVideo::cycleSubtitle()
{
    if (m_subtitleTracks.isEmpty())
        return;
    int index = -1;
    for (int i = 0; i < m_subtitleTracks.size(); ++i) {
        if (m_subtitleTracks.at(i).toMap().value("id").toInt() == m_subtitleTrack)
            index = i;
    }
    setSubtitleTrack(m_subtitleTracks.at((index + 1) % m_subtitleTracks.size())
                         .toMap().value("id").toInt());
}

void MpvVideo::stepFrame(bool forward)
{
    if (!m_mpv || !m_mediaLoaded || !m_seekable)
        return;
    const char *command[] = {forward ? "frame-step" : "frame-back-step", nullptr};
    const int result = mpv_command_async(m_mpv.get(), 0, command);
    if (result < 0)
        setError(QString::fromUtf8(mpv_error_string(result)));
}

void MpvVideo::clearSubtitleTracks()
{
    if (m_mediaLoaded) {
        m_mediaLoaded = false;
        emit mediaLoadedChanged();
    }
    if (!m_subtitleTracks.isEmpty()) {
        m_subtitleTracks.clear();
        emit subtitleTracksChanged();
    }
    if (m_subtitleTrack != -1) {
        m_subtitleTrack = -1;
        emit subtitleTrackChanged();
    }
}

void MpvVideo::updateSubtitleTracks(const mpv_node &node)
{
    QVariantList tracks;
    if (!m_stopped && node.format == MPV_FORMAT_NODE_ARRAY) {
        for (int i = 0; i < node.u.list->num; ++i) {
            const mpv_node &entry = node.u.list->values[i];
            if (entry.format != MPV_FORMAT_NODE_MAP)
                continue;
            QVariantMap track;
            for (int j = 0; j < entry.u.list->num; ++j) {
                const QString key = QString::fromUtf8(entry.u.list->keys[j]);
                const mpv_node &value = entry.u.list->values[j];
                if (value.format == MPV_FORMAT_STRING)
                    track.insert(key, QString::fromUtf8(value.u.string));
                else if (value.format == MPV_FORMAT_INT64)
                    track.insert(key, qlonglong(value.u.int64));
                else if (value.format == MPV_FORMAT_FLAG)
                    track.insert(key, bool(value.u.flag));
            }
            if (track.value("type").toString() != "sub")
                continue;
            QStringList label;
            label << tr("Subtitle %1").arg(track.value("id").toInt());
            for (const QString &key : {QStringLiteral("title"), QStringLiteral("lang")}) {
                if (!track.value(key).toString().isEmpty())
                    label << track.value(key).toString();
            }
            if (track.value("external").toBool()) {
                const QString filename = QFileInfo(track.value("external-filename").toString()).fileName();
                if (!filename.isEmpty() && !label.contains(filename))
                    label << filename;
            }
            track.insert("label", label.join(QStringLiteral(" · ")));
            tracks.append(track);
        }
    }
    if (tracks != m_subtitleTracks) {
        m_subtitleTracks = tracks;
        emit subtitleTracksChanged();
    }
}

void MpvVideo::seek(double seconds)
{
    if (!m_mpv || m_stopped || !m_seekable || !std::isfinite(seconds))
        return;
    seconds = std::max(0.0, seconds);
    if (m_duration > 0)
        seconds = std::min(seconds, m_duration);
    const QByteArray value = QByteArray::number(seconds, 'f', 3);
    const char *command[] = {"seek", value.constData(), "absolute+exact", nullptr};
    const int result = mpv_command_async(m_mpv.get(), 0, command);
    if (result < 0)
        setError(QString::fromUtf8(mpv_error_string(result)));
}

void MpvVideo::seekRelative(double seconds)
{
    if (std::isfinite(seconds))
        seek(m_position + seconds);
}

void MpvVideo::setPosition(double seconds)
{
    seconds = std::isfinite(seconds) ? std::max(0.0, seconds) : 0;
    if (m_position == seconds)
        return;
    m_position = seconds;
    emit positionChanged();
}

void MpvVideo::setDuration(double seconds)
{
    seconds = std::isfinite(seconds) ? std::max(0.0, seconds) : 0;
    if (m_duration == seconds)
        return;
    m_duration = seconds;
    emit durationChanged();
}

void MpvVideo::setSeekable(bool seekable)
{
    if (m_seekable == seekable)
        return;
    m_seekable = seekable;
    emit seekableChanged();
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
    if (!m_mpv)
        return;
    while (const auto *event = mpv_wait_event(m_mpv.get(), 0)) {
        if (event->event_id == MPV_EVENT_NONE)
            break;
        if ((event->event_id == MPV_EVENT_COMMAND_REPLY
             || event->event_id == MPV_EVENT_SET_PROPERTY_REPLY) && event->error < 0)
            setError(QString::fromUtf8(mpv_error_string(event->error)));
        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            const auto *property = static_cast<mpv_event_property *>(event->data);
            if (std::strcmp(property->name, "sid") == 0) {
                const int id = !m_stopped && property->data && property->format == MPV_FORMAT_INT64
                    ? int(*static_cast<int64_t *>(property->data)) : -1;
                if (m_subtitleTrack != id) {
                    m_subtitleTrack = id;
                    emit subtitleTrackChanged();
                }
            }
            if (!property->data)
                continue;
            if (std::strcmp(property->name, "track-list") == 0 && property->format == MPV_FORMAT_NODE)
                updateSubtitleTracks(*static_cast<mpv_node *>(property->data));
            if (property->format == MPV_FORMAT_FLAG) {
                const bool value = *static_cast<int *>(property->data) != 0;
                if (std::strcmp(property->name, "pause") == 0)
                    setPlaybackState(m_stopped, value);
                else if (std::strcmp(property->name, "seekable") == 0)
                    setSeekable(!m_stopped && value);
                else if (std::strcmp(property->name, "sub-visibility") == 0 && m_subtitlesVisible != value) {
                    m_subtitlesVisible = value;
                    emit subtitlesVisibleChanged();
                }
                else if (std::strcmp(property->name, "mute") == 0 && m_muted != value) {
                    m_muted = value;
                    emit mutedChanged();
                }
            } else if (property->format == MPV_FORMAT_DOUBLE) {
                const double value = *static_cast<double *>(property->data);
                if (std::strcmp(property->name, "time-pos") == 0 && !m_stopped)
                    setPosition(value);
                else if (std::strcmp(property->name, "duration") == 0 && !m_stopped)
                    setDuration(value);
                else if (std::strcmp(property->name, "speed") == 0 && m_speed != value) {
                    m_speed = value;
                    emit speedChanged();
                }
                else if (std::strcmp(property->name, "volume") == 0 && m_volume != value) {
                    m_volume = value;
                    emit volumeChanged();
                }
            }
        }
        if (event->event_id == MPV_EVENT_FILE_LOADED && !m_stopped) {
            m_mediaLoaded = true;
            emit mediaLoadedChanged();
        }
        if (event->event_id == MPV_EVENT_END_FILE) {
            const auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR) {
                clearSubtitleTracks();
                setPlaybackState(true, false);
                setPosition(0);
                setDuration(0);
                setSeekable(false);
                setError(QStringLiteral("Cannot play %1: %2")
                             .arg(m_source.toDisplayString(),
                                  QString::fromUtf8(mpv_error_string(end->error))));
            } else if (end->reason == MPV_END_FILE_REASON_EOF) {
                clearSubtitleTracks();
                setPlaybackState(true, false);
                setSeekable(false);
                emit endOfFile();
            }
        }
    }
}

#include "MpvVideo.moc"
