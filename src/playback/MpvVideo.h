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

public:
    explicit MpvVideo(QQuickItem *parent = nullptr);
    ~MpvVideo() override;

    Renderer *createRenderer() const override;
    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    QString errorString() const { return m_errorString; }
    bool playing() const { return !m_stopped && !m_paused; }
    bool stopped() const { return m_stopped; }
    quint64 loadRevision() const { return m_loadRevision; }

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePause();

signals:
    void sourceChanged();
    void errorStringChanged();
    void playbackStateChanged();

private:
    void processEvents();
    void setError(const QString &message);
    void setPlaybackState(bool stopped, bool paused);
    bool setPaused(bool paused);

    // The renderer retains the handle until its OpenGL context is freed.
    std::shared_ptr<mpv_handle> m_mpv;
    QUrl m_source;
    QString m_errorString;
    bool m_stopped = true;
    bool m_paused = false;
    quint64 m_loadRevision = 0;
};
