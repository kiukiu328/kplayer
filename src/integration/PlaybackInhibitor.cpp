#include "PlaybackInhibitor.h"

#include <QCoreApplication>
#include <QDebug>
#include <QtGlobal>

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>
#elif defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

struct PlaybackInhibitor::State
{
    bool playing = false;
#ifdef Q_OS_LINUX
    quint64 generation = 0;
    QString requestPath;
    uint screenSaverCookie = 0;
    bool hasScreenSaverCookie = false;

    static void closeRequest(const QString &path)
    {
        if (path.isEmpty())
            return;
        auto message = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"), path,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Close"));
        QDBusConnection::sessionBus().asyncCall(message);
    }

    static void releaseScreenSaver(uint cookie)
    {
        auto message = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver"),
            QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("UnInhibit"));
        message << cookie;
        QDBusConnection::sessionBus().asyncCall(message);
    }

    static void inhibitScreenSaver(const std::shared_ptr<State> &state, quint64 generation)
    {
        auto message = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver"),
            QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("Inhibit"));
        message << QStringLiteral("KPlayer") << QStringLiteral("Playing media");
        auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(message), QCoreApplication::instance());
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
                         [state, generation](QDBusPendingCallWatcher *call) {
            const QDBusPendingReply<uint> reply = *call;
            call->deleteLater();
            if (reply.isError()) {
                if (state->playing && state->generation == generation)
                    qWarning() << "Cannot inhibit desktop idle:" << reply.error().message();
                return;
            }
            if (!state->playing || state->generation != generation) {
                releaseScreenSaver(reply.value());
                return;
            }
            state->screenSaverCookie = reply.value();
            state->hasScreenSaverCookie = true;
        });
    }
#elif defined(Q_OS_WIN)
    HANDLE request = nullptr;
#endif
};

PlaybackInhibitor::PlaybackInhibitor() : m_state(std::make_shared<State>()) {}

PlaybackInhibitor::~PlaybackInhibitor()
{
    setPlaying(false);
}

void PlaybackInhibitor::setPlaying(bool playing)
{
    auto state = m_state;
    if (state->playing == playing)
        return;
    state->playing = playing;
#ifdef Q_OS_LINUX
    const quint64 generation = ++state->generation;
    if (!playing) {
        State::closeRequest(state->requestPath);
        state->requestPath.clear();
        if (state->hasScreenSaverCookie) {
            State::releaseScreenSaver(state->screenSaverCookie);
            state->hasScreenSaverCookie = false;
        }
        return;
    }

    auto message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Inhibit"), QStringLiteral("Inhibit"));
    // Suspend | Idle covers automatic sleep, dimming, blanking and idle locking.
    // No logout/user-switch inhibition is requested.
    message << QString{} << uint(4 | 8)
            << QVariantMap{{QStringLiteral("reason"), QStringLiteral("Playing media")}};
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(message), QCoreApplication::instance());
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
                     [state, generation](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<QDBusObjectPath> reply = *call;
        call->deleteLater();
        if (reply.isError()) {
            if (state->playing && state->generation == generation)
                State::inhibitScreenSaver(state, generation);
            return;
        }
        const QString path = reply.value().path();
        // A pause, stop or destruction can happen before the asynchronous reply.
        // Each old request must be released without disturbing a newer one.
        if (!state->playing || state->generation != generation) {
            State::closeRequest(path);
            return;
        }
        state->requestPath = path;
    });
#elif defined(Q_OS_WIN)
    if (!playing) {
        if (state->request) {
            PowerClearRequest(state->request, PowerRequestDisplayRequired);
            PowerClearRequest(state->request, PowerRequestSystemRequired);
            CloseHandle(state->request);
            state->request = nullptr;
        }
        return;
    }
    wchar_t reason[] = L"KPlayer is playing media";
    REASON_CONTEXT context{};
    context.Version = POWER_REQUEST_CONTEXT_VERSION;
    context.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    context.Reason.SimpleReasonString = reason;
    state->request = PowerCreateRequest(&context);
    if (state->request == INVALID_HANDLE_VALUE) {
        state->request = nullptr;
        qWarning() << "Cannot create playback power request:" << GetLastError();
        return;
    }
    if (!PowerSetRequest(state->request, PowerRequestDisplayRequired)
        || !PowerSetRequest(state->request, PowerRequestSystemRequired)) {
        qWarning() << "Cannot inhibit sleep during playback:" << GetLastError();
        PowerClearRequest(state->request, PowerRequestDisplayRequired);
        PowerClearRequest(state->request, PowerRequestSystemRequired);
        CloseHandle(state->request);
        state->request = nullptr;
    }
#endif
}
