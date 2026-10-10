#include "PlaybackInhibitor.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QTest>
#include <QVariantMap>

#include <memory>
#include <vector>

static QDBusConnection serverBus()
{
    return QDBusConnection(QStringLiteral("inhibitor-test-server"));
}

class FakeRequest : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")
public:
    bool closed = false;
public slots:
    void Close() { closed = true; }
};

class FakePortal : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Inhibit")
public:
    uint flags = 0;
    QVariantMap options;
    bool fail = false;
    bool delay = false;
    std::vector<std::unique_ptr<FakeRequest>> requests;
    QList<QDBusMessage> pending;

    void finishPending(int index)
    {
        const auto path = QDBusObjectPath(QStringLiteral("/request/%1").arg(index));
        serverBus().send(pending[index].createReply(
            {QVariant::fromValue(path)}));
    }

public slots:
    QDBusObjectPath Inhibit(const QString &, uint requestedFlags, const QVariantMap &requestedOptions)
    {
        flags = requestedFlags;
        options = requestedOptions;
        if (fail) {
            sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.NotSupported"),
                           QStringLiteral("No inhibit backend"));
            return QDBusObjectPath(QStringLiteral("/"));
        }
        const auto path = QStringLiteral("/request/%1").arg(requests.size());
        auto request = std::make_unique<FakeRequest>();
        if (!serverBus().registerObject(
                path, request.get(), QDBusConnection::ExportAllSlots))
            qFatal("Cannot register fake portal request");
        requests.push_back(std::move(request));
        if (delay) {
            setDelayedReply(true);
            pending.append(message());
        }
        return QDBusObjectPath(path);
    }
};

class FakeScreenSaver : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ScreenSaver")
public:
    uint calls = 0;
    QList<uint> released;
    bool delay = false;
    QDBusMessage pending;

public slots:
    uint Inhibit(const QString &, const QString &)
    {
        ++calls;
        if (delay) {
            setDelayedReply(true);
            pending = message();
        }
        // Zero is a valid cookie too.
        return calls - 1;
    }
    void UnInhibit(uint cookie) { released.append(cookie); }
};

class PlaybackInhibitorTests : public QObject
{
    Q_OBJECT
    std::unique_ptr<FakePortal> m_portal;
    std::unique_ptr<FakeScreenSaver> m_screenSaver;

private slots:
    void init()
    {
        // A separate connection makes replies travel through the daemon, including
        // delayed replies (Qt cannot delay an optimized call to its own connection).
        auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                QStringLiteral("inhibitor-test-server"));
        QVERIFY(bus.isConnected());
        QVERIFY(bus.registerService(QStringLiteral("org.freedesktop.portal.Desktop")));
        QVERIFY(bus.registerService(QStringLiteral("org.freedesktop.ScreenSaver")));
        m_portal = std::make_unique<FakePortal>();
        m_screenSaver = std::make_unique<FakeScreenSaver>();
        QVERIFY(bus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"),
                                   m_portal.get(), QDBusConnection::ExportAllSlots));
        QVERIFY(bus.registerObject(QStringLiteral("/org/freedesktop/ScreenSaver"),
                                   m_screenSaver.get(), QDBusConnection::ExportAllSlots));
    }

    void cleanup()
    {
        auto bus = serverBus();
        bus.unregisterObject(QStringLiteral("/"), QDBusConnection::UnregisterTree);
        bus.unregisterService(QStringLiteral("org.freedesktop.portal.Desktop"));
        bus.unregisterService(QStringLiteral("org.freedesktop.ScreenSaver"));
        m_portal.reset();
        m_screenSaver.reset();
        QDBusConnection::disconnectFromBus(QStringLiteral("inhibitor-test-server"));
    }

    void playingPauseResumeAndDestruction()
    {
        {
            PlaybackInhibitor inhibitor;
            inhibitor.setPlaying(false);
            QCOMPARE(m_portal->requests.size(), size_t(0));
            inhibitor.setPlaying(true);
            inhibitor.setPlaying(true);
            QTRY_COMPARE(m_portal->requests.size(), size_t(1));
            QCOMPARE(m_portal->flags, uint(12));
            QCOMPARE(m_portal->options.value("reason").toString(), QStringLiteral("Playing media"));
            QVERIFY(!m_portal->requests[0]->closed);
            inhibitor.setPlaying(false);
            QTRY_VERIFY(m_portal->requests[0]->closed);
            inhibitor.setPlaying(true);
            QTRY_COMPARE(m_portal->requests.size(), size_t(2));
            QVERIFY(!m_portal->requests[1]->closed);
        }
        QTRY_VERIFY(m_portal->requests[1]->closed);
        QCOMPARE(m_screenSaver->calls, uint(0));
    }

    void lateReplyAfterPauseDoesNotAffectResumedPlayback()
    {
        m_portal->delay = true;
        PlaybackInhibitor inhibitor;
        inhibitor.setPlaying(true);
        QTRY_COMPARE(m_portal->pending.size(), 1);
        inhibitor.setPlaying(false);
        inhibitor.setPlaying(true);
        QTRY_COMPARE(m_portal->pending.size(), 2);
        m_portal->finishPending(1);
        m_portal->finishPending(0);
        QTRY_VERIFY(m_portal->requests[0]->closed);
        QVERIFY(!m_portal->requests[1]->closed);
        inhibitor.setPlaying(false);
        QTRY_VERIFY(m_portal->requests[1]->closed);
    }

    void lateReplyAfterDestructionIsReleased()
    {
        m_portal->delay = true;
        {
            PlaybackInhibitor inhibitor;
            inhibitor.setPlaying(true);
            QTRY_COMPARE(m_portal->pending.size(), 1);
        }
        m_portal->finishPending(0);
        QTRY_VERIFY(m_portal->requests[0]->closed);
    }

    void fallsBackWithoutPortalBackend()
    {
        m_portal->fail = true;
        PlaybackInhibitor inhibitor;
        inhibitor.setPlaying(true);
        QTRY_COMPARE(m_screenSaver->calls, uint(1));
        inhibitor.setPlaying(false);
        QTRY_COMPARE(m_screenSaver->released, QList<uint>{0});
        inhibitor.setPlaying(true);
        QTRY_COMPARE(m_screenSaver->calls, uint(2));
        inhibitor.setPlaying(false);
        QTRY_COMPARE(m_screenSaver->released, (QList<uint>{0, 1}));
    }

    void lateFallbackCookieIsReleased()
    {
        m_portal->fail = true;
        m_screenSaver->delay = true;
        {
            PlaybackInhibitor inhibitor;
            inhibitor.setPlaying(true);
            QTRY_COMPARE(m_screenSaver->calls, uint(1));
        }
        QVERIFY(serverBus().send(
            m_screenSaver->pending.createReply({QVariant::fromValue(uint(0))})));
        QTRY_COMPARE(m_screenSaver->released, QList<uint>{0});
    }
};

QTEST_GUILESS_MAIN(PlaybackInhibitorTests)
#include "tst_playbackinhibitor.moc"
