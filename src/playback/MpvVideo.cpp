#include "MpvVideo.h"
#include "MpvErrors.h"
#include "MpvOpenGL.h"

#include <QDebug>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QTimer>

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
        m_startPosition = video->startPosition();
    }

    void render() override
    {
        MpvOpenGL::resetState();
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
                    emit failed(tr("Cannot initialize video rendering: %1")
                                    .arg(MpvErrors::description(result)));
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
                const QByteArray options = "start=" + QByteArray::number(m_startPosition, 'f', 3);
                const char *load[] = {"loadfile", path.constData(), "replace", "-1",
                                      options.constData(), nullptr};
                const char *stop[] = {"stop", nullptr};
                // Async commands are safe on the render thread; synchronous ones can deadlock.
                const int result = mpv_command_async(m_mpv.get(), 0,
                                                     m_stopped || path.isEmpty() ? stop : load);
                if (result < 0)
                    emit failed(MpvErrors::description(result));
            }
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
        } else {
            framebufferObject()->bind();
            auto *gl = QOpenGLContext::currentContext()->functions();
            gl->glClearColor(16.0f / 255, 16.0f / 255, 16.0f / 255, 1);
            gl->glClear(GL_COLOR_BUFFER_BIT);
        }
        MpvOpenGL::resetState();
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
    double m_startPosition = 0;
    quint64 m_loadRevision = 0;
    quint64 m_loadedRevision = 0;
};

namespace {
constexpr quint64 speedRequestMask = quint64{1} << 63;

int subtitleLanguageMatchScore(const QString &trackLanguage, const QString &requestedLanguage)
{
    const QString code = trackLanguage.trimmed().toLower().replace('_', '-');
    const QString requested = requestedLanguage.trimmed().toLower().replace('_', '-');
    static const QRegularExpression syntax(QStringLiteral("^[a-z]{2,3}(?:-[a-z0-9]+)*$"));
    const auto usable = [](const QString &value) {
        const QString base = value.section('-', 0, 0);
        return syntax.match(value).hasMatch() && base != "und" && base != "zxx" && base != "mul";
    };
    if (!usable(code) || !usable(requested))
        return 0;
    const auto trackParts = code.split('-');
    const auto requestedParts = requested.split('-');
    const auto scriptTag = [](const QStringList &parts) {
        const QString tag = parts.value(1);
        return tag.size() == 4 ? tag : QString{};
    };
    const auto regionTag = [](const QStringList &parts) {
        const QString tag = parts.value(parts.value(1).size() == 4 ? 2 : 1);
        static const QRegularExpression region(QStringLiteral("^(?:[a-z]{2}|[0-9]{3})$"));
        return region.match(tag).hasMatch() ? tag : QString{};
    };
    const QString requestedScript = scriptTag(requestedParts);
    const QString requestedRegion = regionTag(requestedParts);
    const bool specific = !requestedScript.isEmpty() || !requestedRegion.isEmpty();
    if (code == requested)
        return specific ? 3 : 1;
    const auto language = QLocale::codeToLanguage(requestedParts.first());
    if (language == QLocale::AnyLanguage || language == QLocale::C
        || language != QLocale::codeToLanguage(trackParts.first()))
        return 0;

    const QString trackScript = scriptTag(trackParts);
    const QString trackRegion = regionTag(trackParts);
    const auto resolvedScript = [&scriptTag, &regionTag](const QStringList &parts) {
        const QString script = scriptTag(parts);
        if (!script.isEmpty())
            return QLocale::codeToScript(script);
        const QString region = regionTag(parts);
        // A bare language tag has no script metadata. QLocale's default script
        // must not turn an unspecified Chinese track into a Hans or Hant match.
        return region.isEmpty() ? QLocale::AnyScript
                               : QLocale(parts.first() + '-' + region).script();
    };
    const auto candidateScript = resolvedScript(trackParts);
    if (!requestedScript.isEmpty()) {
        const auto script = QLocale::codeToScript(requestedScript);
        if (script == QLocale::AnyScript || script != candidateScript)
            return 0;
    } else if (!requestedRegion.isEmpty() && candidateScript != QLocale::AnyScript
               && resolvedScript(requestedParts) != candidateScript) {
        return 0;
    }
    if (!requestedRegion.isEmpty() && !trackRegion.isEmpty() && requestedRegion != trackRegion)
        return 0;
    if (!specific)
        return 1;
    // Prefer the requested variant over a compatible script/region or a bare
    // language tag, without allowing a lower-priority rule to win.
    if (requestedScript == trackScript && requestedRegion == trackRegion)
        return 3;
    return candidateScript == QLocale::AnyScript && trackRegion.isEmpty() ? 1 : 2;
}

const std::pair<const char *, const char *> savedPlayerProperties[] = {
    {"playback/maximumSpeed", "maximumSpeed"},
    {"playback/volume", "volume"},
    {"playback/muted", "muted"},
    {"video/aspectRatio", "aspectRatio"},
    {"video/brightness", "brightness"},
    {"video/contrast", "contrast"},
    {"video/saturation", "saturation"},
    {"video/hue", "hue"},
    {"video/soften", "soften"},
    {"video/sharpen", "sharpen"},
    {"video/deblock", "deblock"},
    {"audio/equalizerBands", "equalizerBands"},
    {"audio/preamp", "preamp"},
    {"audio/equalizerEnabled", "equalizerEnabled"},
    {"subtitles/defaultLanguage", "defaultSubtitleLanguage"},
    {"subtitles/defaultRules", "defaultSubtitleRules"},
    {"subtitles/defaultTrack", "defaultSubtitleTrack"},
    {"subtitles/defaultTrackName", "defaultSubtitleTrackName"},
    {"subtitles/visible", "subtitlesVisible"},
    {"subtitles/styleOverride", "subtitleStyleOverride"},
    {"subtitles/font", "subtitleFont"},
    {"subtitles/fontSize", "subtitleFontSize"},
    {"subtitles/position", "subtitlePosition"},
    {"subtitles/alignment", "subtitleAlignment"},
};
}

void MpvVideo::setPersistent(bool persistent)
{
    if (m_persistent == persistent)
        return;
    m_persistent = persistent;
    if (persistent) {
        m_restoringPreferences = true;
        const QSettings settings;
        for (const auto &[key, name] : savedPlayerProperties) {
            if (settings.contains(key))
                setProperty(name, settings.value(key));
        }
        const double last = settings.value("playback/lastSpeed", m_lastSpeed).toDouble();
        if (std::isfinite(last))
            m_lastSpeed = std::clamp(last, 0.2, m_maximumSpeed);
        m_restoringPreferences = false;
    }
    emit persistentChanged();
}

void MpvVideo::savePreferences()
{
    if (!m_persistent || m_restoringPreferences)
        return;
    QSettings settings;
    for (const auto &[key, name] : savedPlayerProperties)
        settings.setValue(key, property(name));
    // Each new player starts at 1x; only Z's remembered speed is durable.
    settings.remove("playback/speed");
    settings.setValue("playback/lastSpeed", m_lastSpeed);
}

MpvVideo::MpvVideo(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
    auto *handle = mpv_create();
    if (!handle) {
        setError(tr("Cannot create the mpv player."));
        return;
    }
    m_mpv = {handle, mpv_terminate_destroy};
    for (const auto &[name, value] : {
             std::pair{"vo", "libmpv"},
             std::pair{"background", "color"},
             std::pair{"background-color", "#101010"},
             std::pair{"osd-level", "0"},
             std::pair{"sub-ass-override", "scale"},
             std::pair{"sub-font", "Segoe UI"},
             std::pair{"sub-font-size", "18"},
             // Explicit pitch correction replaces mpv's automatic filter, whose default
             // max-speed=8 mutes faster playback. Zero removes that cutoff.
             std::pair{"af", "@kplayer_tempo:scaletempo2=min-speed=0.2:max-speed=0"},
             std::pair{"terminal", "no"}}) {
        const int result = mpv_set_option_string(m_mpv.get(), name, value);
        if (result < 0) {
            setError(MpvErrors::description(result));
            m_mpv.reset();
            return;
        }
    }
    const int result = mpv_initialize(m_mpv.get());
    if (result < 0) {
        setError(tr("Cannot initialize mpv: %1")
                     .arg(MpvErrors::description(result)));
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
    for (auto signal : {&MpvVideo::speedChanged, &MpvVideo::maximumSpeedChanged,
                        &MpvVideo::volumeChanged, &MpvVideo::mutedChanged,
                        &MpvVideo::aspectRatioChanged, &MpvVideo::videoAdjustmentsChanged,
                        &MpvVideo::audioControlsChanged, &MpvVideo::videoFiltersChanged,
                        &MpvVideo::subtitleStyleChanged, &MpvVideo::subtitlesVisibleChanged,
                        &MpvVideo::defaultSubtitleLanguageChanged, &MpvVideo::defaultSubtitleTrackChanged,
                        &MpvVideo::defaultSubtitleTrackNameChanged, &MpvVideo::defaultSubtitleRulesChanged})
        connect(this, signal, this, &MpvVideo::savePreferences);
    connect(this, &QQuickItem::heightChanged, this, &MpvVideo::updateSubtitlePosition);
    auto *resumeTimer = new QTimer(this);
    resumeTimer->setInterval(5000);
    connect(resumeTimer, &QTimer::timeout, this, &MpvVideo::saveResumePosition);
    resumeTimer->start();
}

MpvVideo::~MpvVideo()
{
    saveResumePosition();
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
    setSubtitleDelay(0);
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
    m_playbackInhibitor.setPlaying(playing());
    emit playbackStateChanged();
}

bool MpvVideo::setPaused(bool paused)
{
    if (!m_mpv)
        return false;
    int value = paused ? 1 : 0;
    const int result = mpv_set_property_async(m_mpv.get(), 0, "pause", MPV_FORMAT_FLAG, &value);
    if (result < 0) {
        setError(MpvErrors::description(result));
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
        m_startPosition = resumePosition();
        // Manual track choices belong to the current file; new loads use the saved subtitle preferences.
        const char *selection = "auto";
        const int result = mpv_set_property_async(m_mpv.get(), 0, "sid", MPV_FORMAT_STRING, &selection);
        if (result < 0) {
            setError(MpvErrors::description(result));
            return;
        }
        setPosition(0);
        setPlaybackState(false, false);
        // A revision also reloads the same source after Stop.
        ++m_loadRevision;
        update();
    }
}

void MpvVideo::stop()
{
    saveResumePosition();
    m_seekHintPending = false;
    clearLoop();
    clearSubtitleTracks();
    setPosition(0);
    setDuration(0);
    setSeekable(false);
    if (m_stopped)
        return;
    if (m_mpv) {
        const char *command[] = {"stop", nullptr};
        const int result = mpv_command_async(m_mpv.get(), 0, command);
        if (result < 0)
            setError(MpvErrors::description(result));
    }
    setPlaybackState(true, false);
    ++m_loadRevision;
    update();
}

QString MpvVideo::resumeKey() const
{
    if (m_source.isEmpty())
        return {};
    QUrl url = m_source.adjusted(QUrl::NormalizePathSegments);
    if (url.isLocalFile()) {
        const QFileInfo file(url.toLocalFile());
        const QString canonical = file.canonicalFilePath();
        url = QUrl::fromLocalFile(canonical.isEmpty() ? file.absoluteFilePath() : canonical);
    }
    return QStringLiteral("playback/positions/")
        + QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex());
}

void MpvVideo::saveResumePosition()
{
    if (m_stopped || !m_mediaLoaded || !m_seekable || m_source.isEmpty())
        return;
    const QString key = resumeKey();
    m_resumePositions.insert(key, m_position);
    if (m_persistent) {
        QSettings settings;
        settings.setValue(key, m_position);
        settings.sync();
    }
}

double MpvVideo::resumePosition() const
{
    const QString key = resumeKey();
    const double seconds = m_resumePositions.contains(key) ? m_resumePositions.value(key)
        : m_persistent ? QSettings().value(key, 0).toDouble() : 0;
    return std::isfinite(seconds) ? std::max(0.0, seconds) : 0;
}

void MpvVideo::clearResumePosition()
{
    const QString key = resumeKey();
    m_resumePositions.remove(key);
    if (m_persistent)
        QSettings().remove(key);
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
        setError(MpvErrors::description(result));
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
        setError(MpvErrors::description(result));
        return;
    }
    m_volume = volume;
    emit volumeChanged();
}

void MpvVideo::setMaximumSpeed(double speed)
{
    if (!std::isfinite(speed))
        return;
    speed = std::clamp(speed, 1.0, 100.0);
    if (qFuzzyCompare(m_maximumSpeed, speed))
        return;
    m_maximumSpeed = speed;
    m_lastSpeed = std::min(m_lastSpeed, speed);
    if (m_speed > speed)
        setSpeed(speed);
    emit maximumSpeedChanged();
}

void MpvVideo::setSpeed(double speed)
{
    if (!m_mpv || !std::isfinite(speed))
        return;
    speed = std::clamp(speed, 0.2, m_maximumSpeed);
    if (qFuzzyCompare(m_speed, speed))
        return;
    const quint64 revision = m_speedRevision + 1;
    const int result = mpv_set_property_async(m_mpv.get(), speedRequestMask | revision,
                                             "speed", MPV_FORMAT_DOUBLE, &speed);
    if (result < 0) {
        setError(MpvErrors::description(result));
        return;
    }
    m_speedRevision = revision;
    m_speedPending = true;
    m_speed = speed;
    if (!qFuzzyCompare(speed, 1.0))
        m_lastSpeed = speed;
    emit speedChanged();
}

void MpvVideo::toggleSpeed()
{
    setSpeed(qFuzzyCompare(m_speed, 1.0) ? m_lastSpeed : 1.0);
}

void MpvVideo::syncSpeed()
{
    // Older property notifications must not undo a newer, still queued request.
    if (m_speedPending)
        return;
    double speed = 0;
    // Read the current value rather than replaying an already queued observation.
    if (mpv_get_property(m_mpv.get(), "speed", MPV_FORMAT_DOUBLE, &speed) >= 0
        && std::isfinite(speed) && !qFuzzyCompare(m_speed, speed)) {
        m_speed = speed;
        emit speedChanged();
    }
}

void MpvVideo::setAspectRatio(const QString &ratio)
{
    static const QStringList ratios{"no", "4:3", "16:9", "16:10", "1:1", "2.35:1"};
    if (!m_mpv || m_aspectRatio == ratio || !ratios.contains(ratio))
        return;
    const QByteArray encoded = ratio.toUtf8();
    const char *value = encoded.constData();
    const int result = mpv_set_property_async(m_mpv.get(), 0, "video-aspect-override",
                                              MPV_FORMAT_STRING, &value);
    if (result < 0) {
        setError(MpvErrors::description(result));
        return;
    }
    m_aspectRatio = ratio;
    emit aspectRatioChanged();
}

void MpvVideo::adjustFrameSize(int widthSteps, int heightSteps)
{
    setFrameSize(m_frameWidthScale + widthSteps * 0.02,
                 m_frameHeightScale + heightSteps * 0.02);
}

void MpvVideo::resetFrameSize()
{
    setFrameSize(1.0, 1.0);
}

void MpvVideo::setFrameSize(double widthScale, double heightScale)
{
    // Keep repeated key presses reversible and prevent a zero-sized picture.
    widthScale = std::clamp(std::round(widthScale * 100) / 100, 0.1, 4.0);
    heightScale = std::clamp(std::round(heightScale * 100) / 100, 0.1, 4.0);
    if (!m_mpv || (qFuzzyCompare(widthScale, m_frameWidthScale)
                   && qFuzzyCompare(heightScale, m_frameHeightScale)))
        return;
    if (!setControlProperty("video-scale-x", QString::number(widthScale))
        || !setControlProperty("video-scale-y", QString::number(heightScale)))
        return;
    m_frameWidthScale = widthScale;
    m_frameHeightScale = heightScale;
    emit frameSizeChanged();
}

void MpvVideo::setVideoAdjustment(const char *name, int value, int &current)
{
    value = std::clamp(value, -100, 100);
    if (!m_mpv || value == current)
        return;
    int64_t property = value;
    const int result = mpv_set_property_async(m_mpv.get(), 0, name, MPV_FORMAT_INT64, &property);
    if (result < 0) {
        setError(MpvErrors::description(result));
        return;
    }
    current = value;
    emit videoAdjustmentsChanged();
}

void MpvVideo::setBrightness(int value) { setVideoAdjustment("brightness", value, m_brightness); }
void MpvVideo::setContrast(int value) { setVideoAdjustment("contrast", value, m_contrast); }
void MpvVideo::setSaturation(int value) { setVideoAdjustment("saturation", value, m_saturation); }
void MpvVideo::setHue(int value) { setVideoAdjustment("hue", value, m_hue); }

void MpvVideo::resetVideoAdjustments()
{
    setBrightness(0);
    setContrast(0);
    setSaturation(0);
    setHue(0);
}

bool MpvVideo::setControlProperty(const char *name, const QString &value)
{
    if (!m_mpv)
        return false;
    const QByteArray encoded = value.toUtf8();
    const char *text = encoded.constData();
    const int result = mpv_set_property_async(m_mpv.get(), 0, name, MPV_FORMAT_STRING, &text);
    if (result < 0)
        setError(MpvErrors::description(result));
    return result >= 0;
}

void MpvVideo::setEqualizerEnabled(bool enabled)
{
    if (m_equalizerEnabled == enabled)
        return;
    m_equalizerEnabled = enabled;
    updateEqualizer();
    emit audioControlsChanged();
}

void MpvVideo::setEqualizerBands(const QVariantList &bands)
{
    if (bands.size() != 10)
        return;
    QVariantList normalized;
    for (const auto &band : bands) {
        bool valid = false;
        const double value = band.toDouble(&valid);
        if (!valid || !std::isfinite(value))
            return;
        normalized.append(std::clamp(value, -12.0, 12.0));
    }
    if (m_equalizerBands == normalized)
        return;
    m_equalizerBands = normalized;
    updateEqualizer();
    emit audioControlsChanged();
}

void MpvVideo::setPreamp(double value)
{
    if (!std::isfinite(value))
        return;
    value = std::clamp(value, -12.0, 12.0);
    if (m_preamp == value)
        return;
    m_preamp = value;
    updateEqualizer();
    emit audioControlsChanged();
}

void MpvVideo::updateEqualizer()
{
    if (!m_mpv)
        return;
    // Replace only our labeled filter; leave other audio processing untouched.
    if (!m_equalizerEnabled) {
        if (m_equalizerApplied) {
            const char *command[] = {"af", "remove", "@kplayer-eq", nullptr};
            mpv_command_async(m_mpv.get(), 0, command);
            m_equalizerApplied = false;
        }
        return;
    }
    static constexpr int frequencies[] = {60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000};
    QStringList filters;
    for (int i = 0; i < 10; ++i)
        filters.append(QStringLiteral("equalizer=f=%1:t=o:w=1:g=%2")
                           .arg(frequencies[i]).arg(m_equalizerBands[i].toDouble()));
    filters.append(QStringLiteral("volume=%1dB").arg(m_preamp));
    const QByteArray filter = (QStringLiteral("@kplayer-eq:lavfi=[")
                              + filters.join(',') + ']').toUtf8();
    const char *command[] = {"af", "add", filter.constData(), nullptr};
    const int result = mpv_command_async(m_mpv.get(), 0, command);
    if (result < 0)
        setError(MpvErrors::description(result));
    else
        m_equalizerApplied = true;
}

void MpvVideo::setVideoFilter(bool enabled, bool &current)
{
    if (current == enabled)
        return;
    current = enabled;
    updateVideoFilters();
    emit videoFiltersChanged();
}

void MpvVideo::setSoften(bool enabled) { setVideoFilter(enabled, m_soften); }
void MpvVideo::setSharpen(bool enabled) { setVideoFilter(enabled, m_sharpen); }
void MpvVideo::setDeblock(bool enabled) { setVideoFilter(enabled, m_deblock); }

void MpvVideo::updateVideoFilters()
{
    if (!m_mpv)
        return;
    QStringList filters;
    if (m_soften) filters.append(QStringLiteral("gblur=sigma=0.7"));
    if (m_sharpen) filters.append(QStringLiteral("unsharp=5:5:0.5:5:5:0"));
    if (m_deblock) filters.append(QStringLiteral("deblock=filter=weak:block=8"));
    if (filters.isEmpty()) {
        if (m_videoFiltersApplied) {
            const char *command[] = {"vf", "remove", "@kplayer-video", nullptr};
            mpv_command_async(m_mpv.get(), 0, command);
            m_videoFiltersApplied = false;
        }
        return;
    }
    const QByteArray filter = (QStringLiteral("@kplayer-video:lavfi=[")
                              + filters.join(',') + ']').toUtf8();
    const char *command[] = {"vf", "add", filter.constData(), nullptr};
    const int result = mpv_command_async(m_mpv.get(), 0, command);
    if (result < 0)
        setError(MpvErrors::description(result));
    else
        m_videoFiltersApplied = true;
}

void MpvVideo::setSubtitleFont(const QString &font)
{
    const QString normalized = font.trimmed();
    if (normalized.isEmpty() || normalized == m_subtitleFont
        || !setControlProperty("sub-font", normalized))
        return;
    m_subtitleFont = normalized;
    emit subtitleStyleChanged();
}

void MpvVideo::setSubtitleFontSize(int size)
{
    size = std::clamp(size, 8, 72);
    if (size == m_subtitleFontSize || !setControlProperty("sub-font-size", QString::number(size)))
        return;
    m_subtitleFontSize = size;
    emit subtitleStyleChanged();
}

void MpvVideo::setSubtitlePosition(int position)
{
    position = std::clamp(position, 0, 100);
    if (position == m_subtitlePosition)
        return;
    m_subtitlePosition = position;
    updateSubtitlePosition();
    emit subtitleStyleChanged();
}

void MpvVideo::setSubtitleBottomInset(double pixels)
{
    if (!std::isfinite(pixels))
        return;
    pixels = std::max(0.0, pixels);
    if (m_subtitleBottomInset == pixels)
        return;
    m_subtitleBottomInset = pixels;
    updateSubtitlePosition();
    emit subtitleBottomInsetChanged();
}

void MpvVideo::updateSubtitlePosition()
{
    // Keep the user's position persistent; the UI clearance is temporary and
    // expressed as a fraction of the same surface mpv renders subtitles onto.
    const double offset = height() > 0 ? 100.0 * m_subtitleBottomInset / height() : 0;
    setControlProperty("sub-pos", QString::number(std::clamp(m_subtitlePosition - offset, 0.0, 100.0)));
}

void MpvVideo::setSubtitleAlignment(int alignment)
{
    alignment = std::clamp(alignment, -1, 1);
    static const QStringList names{"left", "center", "right"};
    if (alignment == m_subtitleAlignment || !setControlProperty("sub-align-x", names[alignment + 1]))
        return;
    m_subtitleAlignment = alignment;
    emit subtitleStyleChanged();
}

void MpvVideo::setSubtitleDelay(double delay)
{
    if (!std::isfinite(delay))
        return;
    delay = std::clamp(delay, -60.0, 60.0);
    if (delay == m_subtitleDelay || !setControlProperty("sub-delay", QString::number(delay)))
        return;
    m_subtitleDelay = delay;
    emit subtitleStyleChanged();
}

void MpvVideo::setLoopPoint(bool end, double seconds)
{
    if (!m_seekable || !std::isfinite(seconds))
        return;
    seconds = std::clamp(seconds, 0.0, m_duration);
    // A complete loop must have a positive duration.
    if ((end && m_loopA >= 0 && seconds <= m_loopA)
        || (!end && m_loopB >= 0 && seconds >= m_loopB))
        return;
    if (!setControlProperty(end ? "ab-loop-b" : "ab-loop-a", QString::number(seconds)))
        return;
    (end ? m_loopB : m_loopA) = seconds;
    emit loopPointsChanged();
}

void MpvVideo::clearLoop()
{
    if (m_loopA < 0 && m_loopB < 0)
        return;
    setControlProperty("ab-loop-a", QStringLiteral("no"));
    setControlProperty("ab-loop-b", QStringLiteral("no"));
    m_loopA = m_loopB = -1;
    emit loopPointsChanged();
}

void MpvVideo::captureFrame(const QUrl &file)
{
    if (!m_mpv || !m_mediaLoaded || !file.isLocalFile() || !m_captureFile.isEmpty())
        return;
    const QByteArray path = file.toLocalFile().toUtf8();
    const char *command[] = {"screenshot-to-file", path.constData(), "subtitles", nullptr};
    const int result = mpv_command_async(m_mpv.get(), 1001, command);
    if (result < 0)
        setError(MpvErrors::description(result));
    else
        m_captureFile = file;
}

void MpvVideo::setDefaultSubtitleLanguage(const QString &language)
{
    const QString normalized = language.simplified().remove(QLatin1Char(' ')).toLower();
    if (!m_mpv || m_defaultSubtitleLanguage == normalized)
        return;
    const QByteArray value = normalized.toUtf8();
    const char *languages = value.constData();
    const int result = mpv_set_property_async(m_mpv.get(), 0, "slang", MPV_FORMAT_STRING,
                                             &languages);
    if (result < 0) {
        setError(MpvErrors::description(result));
        return;
    }
    m_defaultSubtitleLanguage = normalized;
    emit defaultSubtitleLanguageChanged();
    applySubtitlePreference();
}

void MpvVideo::setDefaultSubtitleTrack(int track)
{
    track = std::max(0, track);
    if (m_defaultSubtitleTrack == track)
        return;
    m_defaultSubtitleTrack = track;
    emit defaultSubtitleTrackChanged();
    applySubtitlePreference();
}

void MpvVideo::setDefaultSubtitleTrackName(const QString &name)
{
    const QString normalized = name.trimmed();
    if (m_defaultSubtitleTrackName == normalized)
        return;
    m_defaultSubtitleTrackName = normalized;
    emit defaultSubtitleTrackNameChanged();
    applySubtitlePreference();
}

void MpvVideo::setSubtitleStyleOverride(bool enabled)
{
    if (m_subtitleStyleOverride == enabled
        || !setControlProperty("sub-ass-override", enabled ? "strip" : "scale"))
        return;
    m_subtitleStyleOverride = enabled;
    // Changing ASS mode rebuilds the decoder. Reselect the stream so its current
    // cue is available again, including while paused and for embedded subtitles.
    if (m_mediaLoaded && m_subtitleTrack >= 0) {
        setControlProperty("sid", QStringLiteral("no"));
        setControlProperty("sid", QString::number(m_subtitleTrack));
    }
    emit subtitleStyleChanged();
}

void MpvVideo::setDefaultSubtitleRules(const QString &rules)
{
    const QString normalized = rules.trimmed();
    if (m_defaultSubtitleRules == normalized)
        return;
    m_defaultSubtitleRules = normalized;
    emit defaultSubtitleRulesChanged();
    applySubtitlePreference();
}

void MpvVideo::applySubtitlePreference()
{
    if (!m_mpv || !m_mediaLoaded || m_subtitleTracks.isEmpty())
        return;
    int selected = -1;
    for (const QString &line : m_defaultSubtitleRules.split('\n', Qt::SkipEmptyParts)) {
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QString kind = line.left(colon).trimmed().toLower();
        const QString value = line.mid(colon + 1).trimmed();
        if (value.isEmpty())
            continue;
        bool validNumber = false;
        const int number = value.toInt(&validNumber);
        int bestLanguageMatch = 0;
        for (const QVariant &entry : m_subtitleTracks) {
            const auto track = entry.toMap();
            if (kind == "language") {
                const int match = subtitleLanguageMatchScore(track.value("lang").toString(), value);
                if (match > bestLanguageMatch) {
                    bestLanguageMatch = match;
                    selected = track.value("id").toInt();
                }
                continue;
            }
            const QString title = track.value("title").toString().trimmed();
            const QString filename = QFileInfo(track.value("external-filename").toString()).fileName().trimmed();
            if ((kind == "tracknum" && validNumber && number > 0 && track.value("id").toInt() == number)
                || (kind == "subtrackname" && (title.compare(value, Qt::CaseInsensitive) == 0
                    || filename.compare(value, Qt::CaseInsensitive) == 0))) {
                selected = track.value("id").toInt();
                break;
            }
        }
        if (selected >= 0)
            break;
    }
    if (selected < 0 && !m_defaultSubtitleTrackName.isEmpty()) {
        for (const QVariant &entry : m_subtitleTracks) {
            const auto track = entry.toMap();
            const QString title = track.value("title").toString().trimmed();
            const QString fileName = QFileInfo(track.value("external-filename").toString()).fileName();
            if (title.compare(m_defaultSubtitleTrackName, Qt::CaseInsensitive) == 0
                || fileName.trimmed().compare(m_defaultSubtitleTrackName, Qt::CaseInsensitive) == 0) {
                selected = track.value("id").toInt();
                break;
            }
        }
    }
    if (selected < 0) {
        for (const QVariant &entry : m_subtitleTracks) {
            const auto track = entry.toMap();
            if (m_defaultSubtitleTrack > 0 && track.value("id").toInt() == m_defaultSubtitleTrack) {
                selected = m_defaultSubtitleTrack;
                break;
            }
        }
    }
    if (selected < 0) {
        const auto languages = m_defaultSubtitleLanguage.split(',', Qt::SkipEmptyParts);
        for (const QString &language : languages) {
            int bestMatch = 0;
            for (const QVariant &entry : m_subtitleTracks) {
                const auto track = entry.toMap();
                const int match = subtitleLanguageMatchScore(track.value("lang").toString(), language);
                if (match > bestMatch) {
                    bestMatch = match;
                    selected = track.value("id").toInt();
                }
            }
            if (selected >= 0)
                break;
        }
    }
    // Let mpv use its normal automatic selection if no preference matches.
    const QByteArray value = selected < 0 ? QByteArray("auto") : QByteArray::number(selected);
    setControlProperty("sid", QString::fromLatin1(value));
}

void MpvVideo::setSubtitlesVisible(bool visible)
{
    if (!m_mpv || m_subtitlesVisible == visible)
        return;
    int value = visible ? 1 : 0;
    const int result = mpv_set_property_async(m_mpv.get(), 0, "sub-visibility", MPV_FORMAT_FLAG, &value);
    if (result < 0) {
        setError(MpvErrors::description(result));
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
        setError(MpvErrors::description(result));
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
        setError(MpvErrors::description(result));
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
        setError(MpvErrors::description(result));
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

QString MpvVideo::subtitleLabel(const QVariantMap &track) const
{
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
    return label.join(QStringLiteral(" · "));
}

void MpvVideo::retranslateSubtitles()
{
    if (m_subtitleTracks.isEmpty())
        return;
    for (QVariant &entry : m_subtitleTracks) {
        QVariantMap track = entry.toMap();
        track.insert("label", subtitleLabel(track));
        entry = track;
    }
    emit subtitleTracksChanged();
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
            track.insert("label", subtitleLabel(track));
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
        setError(MpvErrors::description(result));
}

void MpvVideo::seekRelative(double seconds)
{
    if (m_mpv && !m_stopped && m_seekable && std::isfinite(seconds)) {
        m_seekHintPending = true;
        seek(m_position + seconds);
    }
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
        if (event->event_id == MPV_EVENT_SET_PROPERTY_REPLY
            && event->reply_userdata == (speedRequestMask | m_speedRevision)) {
            m_speedPending = false;
            syncSpeed();
        }
        if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->reply_userdata == 1001) {
            const QUrl file = std::exchange(m_captureFile, QUrl{});
            if (event->error >= 0)
                emit frameCaptured(file);
        }
        if ((event->event_id == MPV_EVENT_COMMAND_REPLY
             || event->event_id == MPV_EVENT_SET_PROPERTY_REPLY) && event->error < 0)
            setError(MpvErrors::description(event->error));
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
                else if (std::strcmp(property->name, "speed") == 0)
                    syncSpeed();
                else if (std::strcmp(property->name, "volume") == 0 && m_volume != value) {
                    m_volume = value;
                    emit volumeChanged();
                }
            }
        }
        if (event->event_id == MPV_EVENT_FILE_LOADED && !m_stopped) {
            m_mediaLoaded = true;
            emit mediaLoadedChanged();
            // FILE_LOADED can arrive before the observed track-list notification.
            mpv_node tracks{};
            if (mpv_get_property(m_mpv.get(), "track-list", MPV_FORMAT_NODE, &tracks) >= 0) {
                updateSubtitleTracks(tracks);
                mpv_free_node_contents(&tracks);
            }
            applySubtitlePreference();
            updateSubtitlePosition();
        }
        if (event->event_id == MPV_EVENT_PLAYBACK_RESTART && m_seekHintPending && !m_stopped) {
            double seconds = 0;
            if (mpv_get_property(m_mpv.get(), "time-pos", MPV_FORMAT_DOUBLE, &seconds) >= 0) {
                m_seekHintPending = false;
                setPosition(seconds);
                emit seekCompleted(seconds);
            }
        }
        if (event->event_id == MPV_EVENT_END_FILE) {
            const auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR) {
                clearSubtitleTracks();
                setPlaybackState(true, false);
                setPosition(0);
                setDuration(0);
                setSeekable(false);
                setError(tr("Cannot play %1: %2")
                             .arg(m_source.toDisplayString(),
                                  MpvErrors::description(end->error)));
            } else if (end->reason == MPV_END_FILE_REASON_EOF) {
                clearResumePosition();
                clearSubtitleTracks();
                setPlaybackState(true, false);
                setSeekable(false);
                emit endOfFile();
            }
        }
    }
}

#include "MpvVideo.moc"
