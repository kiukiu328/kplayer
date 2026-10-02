#pragma once

#include <QQuickFramebufferObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

struct mpv_handle;

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

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePause();
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
    void endOfFile();

private:
    void processEvents();
    void setError(const QString &message);
    void setPlaybackState(bool stopped, bool paused);
    bool setPaused(bool paused);
    void setPosition(double seconds);
    void setDuration(double seconds);
    void setSeekable(bool seekable);

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
};
