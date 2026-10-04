#pragma once

#include <QQuickFramebufferObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

struct mpv_handle;
struct mpv_node;

class MpvVideo : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackStateChanged)
    Q_PROPERTY(bool stopped READ stopped NOTIFY playbackStateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY seekableChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(QString aspectRatio READ aspectRatio WRITE setAspectRatio NOTIFY aspectRatioChanged)
    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY videoAdjustmentsChanged)
    Q_PROPERTY(int contrast READ contrast WRITE setContrast NOTIFY videoAdjustmentsChanged)
    Q_PROPERTY(int saturation READ saturation WRITE setSaturation NOTIFY videoAdjustmentsChanged)
    Q_PROPERTY(QString defaultSubtitleLanguage READ defaultSubtitleLanguage WRITE setDefaultSubtitleLanguage NOTIFY defaultSubtitleLanguageChanged)
    Q_PROPERTY(bool subtitlesVisible READ subtitlesVisible WRITE setSubtitlesVisible NOTIFY subtitlesVisibleChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY subtitleTracksChanged)
    Q_PROPERTY(int subtitleTrack READ subtitleTrack WRITE setSubtitleTrack NOTIFY subtitleTrackChanged)
    Q_PROPERTY(bool mediaLoaded READ mediaLoaded NOTIFY mediaLoadedChanged)

public:
    explicit MpvVideo(QQuickItem *parent = nullptr);
    ~MpvVideo() override;

    Renderer *createRenderer() const override;
    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    QString errorString() const { return m_errorString; }
    bool playing() const { return !m_stopped && !m_paused; }
    bool stopped() const { return m_stopped; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool seekable() const { return m_seekable; }
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    double volume() const { return m_volume; }
    void setVolume(double volume);
    quint64 loadRevision() const { return m_loadRevision; }
    double speed() const { return m_speed; }
    void setSpeed(double speed);
    QString aspectRatio() const { return m_aspectRatio; }
    void setAspectRatio(const QString &ratio);
    int brightness() const { return m_brightness; }
    void setBrightness(int value);
    int contrast() const { return m_contrast; }
    void setContrast(int value);
    int saturation() const { return m_saturation; }
    void setSaturation(int value);
    Q_INVOKABLE void resetVideoAdjustments();
    Q_INVOKABLE void toggleSpeed();
    QString defaultSubtitleLanguage() const { return m_defaultSubtitleLanguage; }
    void setDefaultSubtitleLanguage(const QString &language);
    bool subtitlesVisible() const { return m_subtitlesVisible; }
    void setSubtitlesVisible(bool visible);
    QVariantList subtitleTracks() const { return m_subtitleTracks; }
    int subtitleTrack() const { return m_subtitleTrack; }
    void setSubtitleTrack(int id);
    bool mediaLoaded() const { return m_mediaLoaded; }
    Q_INVOKABLE bool addSubtitle(const QUrl &url);
    Q_INVOKABLE void cycleSubtitle();
    Q_INVOKABLE void stepFrame(bool forward);

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void retranslateSubtitles();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekRelative(double seconds);

signals:
    void sourceChanged();
    void errorStringChanged();
    void playbackStateChanged();
    void positionChanged();
    void durationChanged();
    void seekableChanged();
    void mutedChanged();
    void volumeChanged();
    void speedChanged();
    void aspectRatioChanged();
    void videoAdjustmentsChanged();
    void defaultSubtitleLanguageChanged();
    void subtitlesVisibleChanged();
    void subtitleTracksChanged();
    void subtitleTrackChanged();
    void mediaLoadedChanged();
    void endOfFile();

private:
    void processEvents();
    void setError(const QString &message);
    void setPlaybackState(bool stopped, bool paused);
    bool setPaused(bool paused);
    void setPosition(double seconds);
    void setDuration(double seconds);
    void setSeekable(bool seekable);
    QString subtitleLabel(const QVariantMap &track) const;
    void updateSubtitleTracks(const mpv_node &node);
    void clearSubtitleTracks();
    void setVideoAdjustment(const char *name, int value, int &current);

    // The renderer retains the handle until its OpenGL context is freed.
    std::shared_ptr<mpv_handle> m_mpv;
    QUrl m_source;
    QString m_errorString;
    bool m_stopped = true;
    bool m_paused = false;
    double m_position = 0;
    double m_duration = 0;
    bool m_seekable = false;
    bool m_muted = false;
    double m_volume = 100;
    quint64 m_loadRevision = 0;
    double m_speed = 1.0;
    double m_lastSpeed = 1.0;
    QString m_aspectRatio = QStringLiteral("no");
    int m_brightness = 0;
    int m_contrast = 0;
    int m_saturation = 0;
    QString m_defaultSubtitleLanguage;
    bool m_subtitlesVisible = true;
    QVariantList m_subtitleTracks;
    int m_subtitleTrack = -1;
    bool m_mediaLoaded = false;
};
