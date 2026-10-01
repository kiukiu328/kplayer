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

public:
    explicit MpvVideo(QQuickItem *parent = nullptr);
    ~MpvVideo() override;

    Renderer *createRenderer() const override;
    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    QString errorString() const { return m_errorString; }

signals:
    void sourceChanged();
    void errorStringChanged();

private:
    void processEvents();
    void setError(const QString &message);

    // The renderer retains the handle until its OpenGL context is freed.
    std::shared_ptr<mpv_handle> m_mpv;
    QUrl m_source;
    QString m_errorString;
};
