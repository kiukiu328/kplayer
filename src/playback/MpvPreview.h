#pragma once

#include <QQuickFramebufferObject>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

struct mpv_handle;
class MpvPreviewRenderer;

// A separate, silent decoder: requesting a thumbnail never seeks the player.
class MpvPreview : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit MpvPreview(QQuickItem *parent = nullptr);
    ~MpvPreview() override;

    Renderer *createRenderer() const override;
    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    bool active() const { return m_active; }
    void setActive(bool active);
    double position() const { return m_position; }
    void setPosition(double seconds);
    bool ready() const { return m_ready; }
    QString errorString() const { return m_errorString; }

signals:
    void sourceChanged();
    void activeChanged();
    void positionChanged();
    void readyChanged();
    void errorStringChanged();

private:
    friend class MpvPreviewRenderer;
    enum class Command { None, Load, Seek, Stop };
    void processEvents();
    void serviceRequest();
    void requestFrame();
    void resetLoadedState();
    void setReady(bool ready);
    void setError(const QString &message);
    bool submitCommand(Command command, const char **arguments);

    // The render thread keeps this alive until its render context is freed.
    std::shared_ptr<mpv_handle> m_mpv;
    QTimer m_debounce;
    QUrl m_source;
    QUrl m_loadedSource;
    QUrl m_failedSource;
    QString m_errorString;
    bool m_active = false;
    bool m_ready = false;
    bool m_rendererReady = false;
    bool m_loading = false;
    bool m_loaded = false;
    bool m_stopping = false;
    bool m_stopAcknowledged = false;
    bool m_seeking = false;
    bool m_seekStarted = false;
    bool m_seekPending = false;
    bool m_hasVideo = false;
    double m_position = 0;
    double m_requestedPosition = -1;
    double m_duration = 0;
    Command m_command = Command::None;
    quint64 m_commandId = 0;
    quint64 m_idleQueryId = 0;
    quint64 m_requestRevision = 0;
    quint64 m_seekRevision = 0;
    quint64 m_renderRevision = 0;
};
