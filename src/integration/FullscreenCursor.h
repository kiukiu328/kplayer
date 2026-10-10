#pragma once

#include <QCursor>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// Hide only this window's cursor, leaving other applications and dialogs alone.
class FullscreenCursor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool hidden READ hidden NOTIFY hiddenChanged)
    Q_PROPERTY(int idleInterval READ idleInterval WRITE setIdleInterval NOTIFY idleIntervalChanged)

public:
    explicit FullscreenCursor(QObject *parent = nullptr);
    ~FullscreenCursor() override;
    QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool hidden() const { return m_hidden; }
    int idleInterval() const { return m_timer.interval(); }
    void setIdleInterval(int milliseconds);

signals:
    void windowChanged();
    void enabledChanged();
    void hiddenChanged();
    void idleIntervalChanged();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    bool canHide() const;
    void reset();
    void showCursor();
    QPointer<QQuickWindow> m_window;
    QTimer m_timer;
    QCursor m_previousCursor;
    bool m_enabled = false;
    bool m_hidden = false;
};
