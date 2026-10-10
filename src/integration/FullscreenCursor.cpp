#include "FullscreenCursor.h"

#include <QEvent>
#include <algorithm>

FullscreenCursor::FullscreenCursor(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(3000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (!canHide())
            return;
        m_previousCursor = m_window->cursor();
        m_window->setCursor(Qt::BlankCursor);
        m_hidden = true;
        emit hiddenChanged();
    });
}

FullscreenCursor::~FullscreenCursor()
{
    showCursor();
}

void FullscreenCursor::setWindow(QQuickWindow *window)
{
    if (m_window == window)
        return;
    showCursor();
    if (m_window) {
        m_window->removeEventFilter(this);
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_window = window;
    if (m_window) {
        m_window->installEventFilter(this);
        connect(m_window, &QWindow::activeChanged, this, &FullscreenCursor::reset);
        connect(m_window, &QWindow::visibilityChanged, this, &FullscreenCursor::reset);
    }
    reset();
    emit windowChanged();
}

void FullscreenCursor::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    reset();
    emit enabledChanged();
}

void FullscreenCursor::setIdleInterval(int milliseconds)
{
    milliseconds = std::max(1, milliseconds);
    if (m_timer.interval() == milliseconds)
        return;
    m_timer.setInterval(milliseconds);
    reset();
    emit idleIntervalChanged();
}

bool FullscreenCursor::canHide() const
{
    return m_enabled && m_window && m_window->isActive()
        && m_window->visibility() == QWindow::FullScreen;
}

void FullscreenCursor::showCursor()
{
    if (!m_hidden)
        return;
    if (m_window)
        m_window->setCursor(m_previousCursor);
    m_hidden = false;
    emit hiddenChanged();
}

void FullscreenCursor::reset()
{
    showCursor();
    if (canHide())
        m_timer.start();
    else
        m_timer.stop();
}

bool FullscreenCursor::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_window) {
        switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::Wheel:
        case QEvent::Enter:
            reset();
            break;
        case QEvent::Leave:
            showCursor();
            m_timer.stop();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(object, event);
}
