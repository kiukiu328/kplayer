#include "WindowResizeCursor.h"

#include <QCoreApplication>
#include <QCursor>
#include <QQuickWindow>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

WindowResizeCursor::WindowResizeCursor(QObject *parent) : QObject(parent)
{
#ifdef Q_OS_WIN
    QCoreApplication::instance()->installNativeEventFilter(this);
#endif
}

WindowResizeCursor::~WindowResizeCursor()
{
#ifdef Q_OS_WIN
    if (auto *app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
#endif
}

void WindowResizeCursor::setFrame(QQuickItem *frame)
{
    if (m_frame == frame)
        return;
    m_frame = frame;
    emit frameChanged();
}

bool WindowResizeCursor::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType);
#ifdef Q_OS_WIN
    const auto *event = static_cast<MSG *>(message);
    if (event->message != WM_SETCURSOR || !m_frame || !m_frame->isEnabled()
        || !m_frame->isVisible() || !m_frame->window()
        || event->hwnd != reinterpret_cast<HWND>(m_frame->window()->winId()))
        return false;

    const QPointF point = m_frame->mapFromGlobal(QCursor::pos());
    if (!m_frame->contains(point))
        return false;
    const qreal edge = m_frame->property("edgeSize").toReal();
    const qreal corner = m_frame->property("cornerSize").toReal();
    const bool left = point.x() < corner;
    const bool right = point.x() >= m_frame->width() - corner;
    const bool top = point.y() < corner;
    const bool bottom = point.y() >= m_frame->height() - corner;
    LPCWSTR cursor = nullptr;
    if ((left && top) || (right && bottom))
        cursor = IDC_SIZENWSE;
    else if ((right && top) || (left && bottom))
        cursor = IDC_SIZENESW;
    else if (point.x() < edge || point.x() >= m_frame->width() - edge)
        cursor = IDC_SIZEWE;
    else if (point.y() < edge || point.y() >= m_frame->height() - edge)
        cursor = IDC_SIZENS;
    if (!cursor)
        return false;

    const auto handle = static_cast<HCURSOR>(LoadImageW(nullptr, cursor, IMAGE_CURSOR,
                                                      0, 0, LR_DEFAULTSIZE | LR_SHARED));
    if (!handle)
        return false;
    SetCursor(handle);
    *result = TRUE;
    return true;
#else
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
#endif
}
