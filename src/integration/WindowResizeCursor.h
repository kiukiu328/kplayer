#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <qqmlintegration.h>

// Windows can request a cursor before delivering a move to the QML hover areas.
class WindowResizeCursor : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem *frame READ frame WRITE setFrame NOTIFY frameChanged)

public:
    explicit WindowResizeCursor(QObject *parent = nullptr);
    ~WindowResizeCursor() override;
    QQuickItem *frame() const { return m_frame; }
    void setFrame(QQuickItem *frame);
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void frameChanged();

private:
    QPointer<QQuickItem> m_frame;
};
