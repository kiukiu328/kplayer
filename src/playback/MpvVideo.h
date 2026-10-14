#pragma once

#include <QQuickFramebufferObject>
#include <QUrl>
#include <QVariantList>
#include <QHash>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include "PlaybackInhibitor.h"

struct mpv_handle;
struct mpv_node;

class MpvVideo : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool persistent READ persistent WRITE setPersistent NOTIFY persistentChanged)
    Q_PROPERTY(QString defaultSubtitleRules READ defaultSubtitleRules WRITE setDefaultSubtitleRules NOTIFY defaultSubtitleRulesChanged)
    Q_PROPERTY(int defaultSubtitleTrack READ defaultSubtitleTrack WRITE setDefaultSubtitleTrack NOTIFY defaultSubtitleTrackChanged)
    Q_PROPERTY(QString defaultSubtitleTrackName READ defaultSubtitleTrackName WRITE setDefaultSubtitleTrackName NOTIFY defaultSubtitleTrackNameChanged)
    Q_PROPERTY(bool subtitleStyleOverride READ subtitleStyleOverride WRITE setSubtitleStyleOverride NOTIFY subtitleStyleChanged)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackStateChanged)
    Q_PROPERTY(bool stopped READ stopped NOTIFY playbackStateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY seekableChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(double maximumSpeed READ maximumSpeed WRITE setMaximumSpeed NOTIFY maximumSpeedChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(QString aspectRatio READ aspectRatio WRITE setAspectRatio NOTIFY aspectRatioChanged)
    Q_PROPERTY(double frameWidthScale READ frameWidthScale NOTIFY frameSizeChanged)
    Q_PROPERTY(double frameHeightScale READ frameHeightScale NOTIFY frameSizeChanged)
    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY videoAdjustmentsChanged)
    Q_PROPERTY(int contrast READ contrast WRITE setContrast NOTIFY videoAdjustmentsChanged)
    Q_PROPERTY(int saturation READ saturation WRITE setSaturation NOTIFY videoAdjustmentsChanged)
    Q_PROPERTY(int hue READ hue WRITE setHue NOTIFY videoAdjustmentsChanged)
    Q_PROPERTY(bool equalizerEnabled READ equalizerEnabled WRITE setEqualizerEnabled NOTIFY audioControlsChanged)
    Q_PROPERTY(QVariantList equalizerBands READ equalizerBands WRITE setEqualizerBands NOTIFY audioControlsChanged)
    Q_PROPERTY(double preamp READ preamp WRITE setPreamp NOTIFY audioControlsChanged)
    Q_PROPERTY(bool soften READ soften WRITE setSoften NOTIFY videoFiltersChanged)
    Q_PROPERTY(bool sharpen READ sharpen WRITE setSharpen NOTIFY videoFiltersChanged)
    Q_PROPERTY(bool deblock READ deblock WRITE setDeblock NOTIFY videoFiltersChanged)
    Q_PROPERTY(QString subtitleFont READ subtitleFont WRITE setSubtitleFont NOTIFY subtitleStyleChanged)
    Q_PROPERTY(int subtitleFontSize READ subtitleFontSize WRITE setSubtitleFontSize NOTIFY subtitleStyleChanged)
    Q_PROPERTY(int subtitlePosition READ subtitlePosition WRITE setSubtitlePosition NOTIFY subtitleStyleChanged)
    Q_PROPERTY(double subtitleBottomInset READ subtitleBottomInset WRITE setSubtitleBottomInset NOTIFY subtitleBottomInsetChanged)
    Q_PROPERTY(int subtitleAlignment READ subtitleAlignment WRITE setSubtitleAlignment NOTIFY subtitleStyleChanged)
    Q_PROPERTY(double subtitleDelay READ subtitleDelay WRITE setSubtitleDelay NOTIFY subtitleStyleChanged)
    Q_PROPERTY(double loopA READ loopA NOTIFY loopPointsChanged)
    Q_PROPERTY(double loopB READ loopB NOTIFY loopPointsChanged)
    Q_PROPERTY(QString defaultSubtitleLanguage READ defaultSubtitleLanguage WRITE setDefaultSubtitleLanguage NOTIFY defaultSubtitleLanguageChanged)
    Q_PROPERTY(bool subtitlesVisible READ subtitlesVisible WRITE setSubtitlesVisible NOTIFY subtitlesVisibleChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY subtitleTracksChanged)
    Q_PROPERTY(int subtitleTrack READ subtitleTrack WRITE setSubtitleTrack NOTIFY subtitleTrackChanged)
    Q_PROPERTY(bool mediaLoaded READ mediaLoaded NOTIFY mediaLoadedChanged)

public:
    explicit MpvVideo(QQuickItem *parent = nullptr);
    ~MpvVideo() override;

    bool persistent() const { return m_persistent; }
    void setPersistent(bool persistent);
    QString defaultSubtitleRules() const { return m_defaultSubtitleRules; }
    void setDefaultSubtitleRules(const QString &rules);
    int defaultSubtitleTrack() const { return m_defaultSubtitleTrack; }
    void setDefaultSubtitleTrack(int track);
    QString defaultSubtitleTrackName() const { return m_defaultSubtitleTrackName; }
    void setDefaultSubtitleTrackName(const QString &name);
    bool subtitleStyleOverride() const { return m_subtitleStyleOverride; }
    void setSubtitleStyleOverride(bool enabled);
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
    double startPosition() const { return m_startPosition; }
    double maximumSpeed() const { return m_maximumSpeed; }
    void setMaximumSpeed(double speed);
    double speed() const { return m_speed; }
    void setSpeed(double speed);
    QString aspectRatio() const { return m_aspectRatio; }
    void setAspectRatio(const QString &ratio);
    double frameWidthScale() const { return m_frameWidthScale; }
    double frameHeightScale() const { return m_frameHeightScale; }
    Q_INVOKABLE void adjustFrameSize(int widthSteps, int heightSteps);
    Q_INVOKABLE void resetFrameSize();
    int brightness() const { return m_brightness; }
    void setBrightness(int value);
    int contrast() const { return m_contrast; }
    void setContrast(int value);
    int saturation() const { return m_saturation; }
    void setSaturation(int value);
    int hue() const { return m_hue; }
    void setHue(int value);
    bool equalizerEnabled() const { return m_equalizerEnabled; }
    void setEqualizerEnabled(bool enabled);
    QVariantList equalizerBands() const { return m_equalizerBands; }
    void setEqualizerBands(const QVariantList &bands);
    double preamp() const { return m_preamp; }
    void setPreamp(double value);
    bool soften() const { return m_soften; }
    void setSoften(bool enabled);
    bool sharpen() const { return m_sharpen; }
    void setSharpen(bool enabled);
    bool deblock() const { return m_deblock; }
    void setDeblock(bool enabled);
    QString subtitleFont() const { return m_subtitleFont; }
    void setSubtitleFont(const QString &font);
    int subtitleFontSize() const { return m_subtitleFontSize; }
    void setSubtitleFontSize(int size);
    int subtitlePosition() const { return m_subtitlePosition; }
    void setSubtitlePosition(int position);
    double subtitleBottomInset() const { return m_subtitleBottomInset; }
    void setSubtitleBottomInset(double pixels);
    int subtitleAlignment() const { return m_subtitleAlignment; }
    void setSubtitleAlignment(int alignment);
    double subtitleDelay() const { return m_subtitleDelay; }
    void setSubtitleDelay(double delay);
    double loopA() const { return m_loopA; }
    double loopB() const { return m_loopB; }
    Q_INVOKABLE void setLoopPoint(bool end, double seconds);
    Q_INVOKABLE void clearLoop();
    Q_INVOKABLE void captureFrame(const QUrl &file);
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
    Q_INVOKABLE void saveResumePosition();

signals:
    void persistentChanged();
    void defaultSubtitleRulesChanged();
    void defaultSubtitleTrackChanged();
    void defaultSubtitleTrackNameChanged();
    void sourceChanged();
    void errorStringChanged();
    void playbackStateChanged();
    void positionChanged();
    void durationChanged();
    void seekableChanged();
    void mutedChanged();
    void volumeChanged();
    void speedChanged();
    void maximumSpeedChanged();
    void aspectRatioChanged();
    void frameSizeChanged();
    void videoAdjustmentsChanged();
    void audioControlsChanged();
    void videoFiltersChanged();
    void subtitleStyleChanged();
    void subtitleBottomInsetChanged();
    void seekCompleted(double seconds);
    void loopPointsChanged();
    void frameCaptured(const QUrl &file);
    void defaultSubtitleLanguageChanged();
    void subtitlesVisibleChanged();
    void subtitleTracksChanged();
    void subtitleTrackChanged();
    void mediaLoadedChanged();
    void endOfFile();

private:
    void savePreferences();
    void applySubtitlePreference();
    void processEvents();
    void setError(const QString &message);
    void setPlaybackState(bool stopped, bool paused);
    bool setPaused(bool paused);
    void setPosition(double seconds);
    void setDuration(double seconds);
    void setSeekable(bool seekable);
    void syncSpeed();
    QString subtitleLabel(const QVariantMap &track) const;
    void updateSubtitleTracks(const mpv_node &node);
    void clearSubtitleTracks();
    void setVideoAdjustment(const char *name, int value, int &current);
    void setFrameSize(double widthScale, double heightScale);
    bool setControlProperty(const char *name, const QString &value);
    void updateEqualizer();
    void updateVideoFilters();
    void setVideoFilter(bool enabled, bool &current);
    void updateSubtitlePosition();
    QString resumeKey() const;
    double resumePosition() const;
    void clearResumePosition();

    // The renderer retains the handle until its OpenGL context is freed.
    std::shared_ptr<mpv_handle> m_mpv;
    PlaybackInhibitor m_playbackInhibitor;
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
    double m_startPosition = 0;
    QHash<QString, double> m_resumePositions;
    bool m_seekHintPending = false;
    double m_maximumSpeed = 16.0;
    double m_speed = 1.0;
    double m_lastSpeed = 1.0;
    quint64 m_speedRevision = 0;
    bool m_speedPending = false;
    QString m_aspectRatio = QStringLiteral("no");
    double m_frameWidthScale = 1.0;
    double m_frameHeightScale = 1.0;
    int m_brightness = 0;
    int m_contrast = 0;
    int m_saturation = 0;
    int m_hue = 0;
    bool m_equalizerEnabled = false;
    QVariantList m_equalizerBands{0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    double m_preamp = 0;
    bool m_equalizerApplied = false;
    bool m_soften = false;
    bool m_sharpen = false;
    bool m_deblock = false;
    bool m_videoFiltersApplied = false;
    QString m_subtitleFont = QStringLiteral("Segoe UI");
    int m_subtitleFontSize = 18;
    int m_subtitlePosition = 100;
    double m_subtitleBottomInset = 0;
    int m_subtitleAlignment = 0;
    double m_subtitleDelay = 0;
    double m_loopA = -1;
    double m_loopB = -1;
    QUrl m_captureFile;
    bool m_persistent = false;
    bool m_restoringPreferences = false;
    QString m_defaultSubtitleRules;
    int m_defaultSubtitleTrack = 0;
    QString m_defaultSubtitleTrackName;
    bool m_subtitleStyleOverride = false;
    QString m_defaultSubtitleLanguage;
    bool m_subtitlesVisible = true;
    QVariantList m_subtitleTracks;
    int m_subtitleTrack = -1;
    bool m_mediaLoaded = false;
};
