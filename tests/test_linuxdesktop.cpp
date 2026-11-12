// Run inside dbus-run-session; the desktop services below are isolated mocks.
#include "platform/LinuxPlaybackInhibitor.h"
#include "platform/LinuxMediaSession.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVirtualObject>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

#include <memory>

namespace {
const QString portal = QStringLiteral("org.freedesktop.portal.Desktop");
const QString gnome = QStringLiteral("org.gnome.SessionManager");
const QString saver = QStringLiteral("org.freedesktop.ScreenSaver");
const QString power = QStringLiteral("org.freedesktop.PowerManagement");
const QString playerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString mediaPath = QStringLiteral("/org/mpris/MediaPlayer2");

QDBusMessage awaitReply(const QDBusConnection &bus, const QDBusMessage &message)
{
    QDBusPendingCallWatcher watcher(bus.asyncCall(message, 2000));
    QSignalSpy finished(&watcher, &QDBusPendingCallWatcher::finished);
    if (!watcher.isFinished()) finished.wait(2500);
    return watcher.reply();
}

QDBusMessage mediaCall(const QString &service, const QString &method, const QVariantList &args = {})
{
    auto message = QDBusMessage::createMethodCall(service, mediaPath, playerInterface, method);
    message.setArguments(args);
    return awaitReply(QDBusConnection::sessionBus(), message);
}

QVariantMap readProperties(const QString &service)
{
    auto message = QDBusMessage::createMethodCall(service, mediaPath,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    message.setArguments({playerInterface});
    return QDBusPendingReply<QVariantMap>(awaitReply(QDBusConnection::sessionBus(), message)).value();
}
}

class MockDesktop final : public QDBusVirtualObject {
    Q_OBJECT
public:
    explicit MockDesktop(const QDBusConnection &bus) : m_bus(bus), m_owners(this)
    {
        m_owners.setConnection(bus);
        m_owners.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
        connect(&m_owners, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &owner) {
            for (auto it = holds.begin(); it != holds.end(); )
                if (it->owner == owner) it = holds.erase(it);
                else ++it;
        });
    }
    struct Hold { QString owner; quint32 flags; };
    QHash<QString, Hold> holds;
    bool portalSupported{true};
    bool gnomeSupported{false};
    bool legacySupported{false};
    bool rejectCanonicalSaver{false};
    quint32 portalResponseCode{0};
    bool emitPortalResponse{true};
    bool responseBeforeMethodReply{false};
    int replyDelayMs{0};
    int acquireCount{0};
    int releaseCount{0};
    int attempts{0};
    QString introspect(const QString &) const override { return {}; }
    quint32 heldFlags() const
    {
        quint32 result = 0;
        for (const auto &hold : holds) result |= hold.flags;
        return result;
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.member() == QStringLiteral("Inhibit")) {
            ++attempts;
            const bool isPortal = message.interface() == QStringLiteral("org.freedesktop.portal.Inhibit");
            const bool isGnome = message.interface() == gnome;
            if (rejectCanonicalSaver && message.interface() == saver
                && message.path() == QStringLiteral("/org/freedesktop/ScreenSaver")) {
                connection.send(message.createErrorReply(QDBusError::UnknownObject,
                                                         QStringLiteral("Mock old KDE object path")));
                return true;
            }
            if ((isPortal && !portalSupported) || (isGnome && !gnomeSupported)
                || (!isPortal && !isGnome && !legacySupported)) {
                connection.send(message.createErrorReply(QDBusError::UnknownInterface,
                                                         QStringLiteral("Mock desktop backend unavailable")));
                return true;
            }
            ++acquireCount;
            const quint32 flags = isPortal ? message.arguments().at(1).toUInt()
                : isGnome ? message.arguments().at(3).toUInt()
                : message.interface() == saver ? 8u : 4u;
            const QString key = isPortal ? QStringLiteral("/org/freedesktop/portal/desktop/request/mock/r%1").arg(acquireCount)
                                          : QString::number(acquireCount);
            holds.insert(key, {message.service(), flags});
            m_owners.addWatchedService(message.service());
            const QVariant result = isPortal ? QVariant::fromValue(QDBusObjectPath(key)) : QVariant(quint32(acquireCount));
            const auto respond = [connection, message, result, key, isPortal,
                                  code = portalResponseCode, emitResponse = emitPortalResponse,
                                  before = responseBeforeMethodReply] {
                const auto signal = [&] {
                    if (!isPortal || !emitResponse) return;
                    auto response = QDBusMessage::createTargetedSignal(message.service(), key,
                        QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"));
                    response.setArguments({code, QVariantMap()});
                    connection.send(response);
                };
                if (before) signal();
                connection.send(message.createReply({result}));
                if (!before) signal();
            };
            if (replyDelayMs > 0) QTimer::singleShot(replyDelayMs, this, respond);
            else respond();
            return true;
        }
        if (message.member() == QStringLiteral("Close") || message.member() == QStringLiteral("Uninhibit")
            || message.member() == QStringLiteral("UnInhibit")) {
            const QString key = message.member() == QStringLiteral("Close") ? message.path()
                : QString::number(message.arguments().first().toUInt());
            if (holds.contains(key) && holds.value(key).owner == message.service()) {
                holds.remove(key);
                ++releaseCount;
            }
            connection.send(message.createReply());
            return true;
        }
        return false;
    }
private:
    QDBusConnection m_bus;
    QDBusServiceWatcher m_owners;
};

class TestLinuxDesktop final : public QObject {
    Q_OBJECT
public slots:
    void propertiesChanged(const QString &, const QVariantMap &changed, const QStringList &)
    {
        m_properties.append(changed);
    }
    void seeked(qlonglong position) { m_seeked.append(position); }
private:
    const QString m_serverName = QStringLiteral("frame-desktop-test-server");
    std::unique_ptr<QDBusConnection> m_server;
    std::unique_ptr<MockDesktop> m_mock;
    QList<QVariantMap> m_properties;
    QList<qlonglong> m_seeked;
private slots:
    void init()
    {
        m_properties.clear(); m_seeked.clear();
        m_server = std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_serverName));
        QVERIFY(m_server->isConnected());
        m_mock = std::make_unique<MockDesktop>(*m_server);
        QVERIFY(m_server->registerVirtualObject(QStringLiteral("/"), m_mock.get(), QDBusConnection::SubPath));
        for (const auto &service : {portal, gnome, saver, power}) QVERIFY(m_server->registerService(service));
    }
    void cleanup()
    {
        for (const auto &service : {portal, gnome, saver, power}) m_server->unregisterService(service);
        m_server->unregisterObject(QStringLiteral("/"), QDBusConnection::UnregisterTree);
        m_mock.reset();
        QDBusConnection::disconnectFromBus(m_serverName);
        m_server.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void portalVideoAudioAndRelease()
    {
        LinuxPlaybackInhibitor inhibitor;
        inhibitor.setPlaybackActive(true, true);
        QTRY_COMPARE(inhibitor.inhibitedFlags(), 12u);
        QCOMPARE(m_mock->heldFlags(), 12u);
        inhibitor.setPlaybackActive(true, true);
        QTest::qWait(20);
        QCOMPARE(m_mock->acquireCount, 1);
        inhibitor.setPlaybackActive(true, false);
        QTRY_COMPARE(inhibitor.inhibitedFlags(), 4u);
        QTRY_COMPARE(m_mock->holds.size(), 1);
        QCOMPARE(m_mock->heldFlags(), 4u);
        inhibitor.setPlaybackActive(false, false);
        QTRY_VERIFY(m_mock->holds.isEmpty());
        QCOMPARE(inhibitor.inhibitedFlags(), 0u);
        QCOMPARE(m_mock->releaseCount, 2);
    }
    void stalePendingReplyDoesNotHoldAfterPause()
    {
        m_mock->replyDelayMs = 150;
        LinuxPlaybackInhibitor inhibitor;
        inhibitor.setPlaybackActive(true, true);
        QTRY_COMPARE(m_mock->acquireCount, 1);
        inhibitor.setPlaybackActive(false, false);
        inhibitor.setPlaybackActive(true, false);
        QTRY_COMPARE(m_mock->acquireCount, 2);
        QTRY_COMPARE(inhibitor.inhibitedFlags(), 4u);
        QTRY_COMPARE(m_mock->holds.size(), 1);
        QCOMPARE(m_mock->heldFlags(), 4u);
        inhibitor.setPlaybackActive(false, false);
        QTRY_VERIFY(m_mock->holds.isEmpty());
        QCOMPARE(inhibitor.inhibitedFlags(), 0u);
    }
    void exitDuringPendingAcquireReleasesOwnership()
    {
        m_mock->replyDelayMs = 200;
        auto inhibitor = std::make_unique<LinuxPlaybackInhibitor>();
        inhibitor->setPlaybackActive(true, true);
        QTRY_COMPARE(m_mock->acquireCount, 1);
        inhibitor.reset();
        QTRY_VERIFY(m_mock->holds.isEmpty());
        QTest::qWait(250);
        QVERIFY(m_mock->holds.isEmpty());
    }
    void gnomeAndLegacyFallback()
    {
        m_mock->portalSupported = false;
        m_mock->gnomeSupported = true;
        LinuxPlaybackInhibitor inhibitor;
        inhibitor.setPlaybackActive(true, true);
        QTRY_COMPARE(inhibitor.inhibitedFlags(), 12u);
        QCOMPARE(m_mock->attempts, 2);
        inhibitor.setPlaybackActive(false, false);
        QTRY_VERIFY(m_mock->holds.isEmpty());
        m_mock->gnomeSupported = false;
        m_mock->legacySupported = true;
        m_mock->rejectCanonicalSaver = true;
        inhibitor.setPlaybackActive(true, true);
        QTRY_COMPARE(inhibitor.inhibitedFlags(), 12u);
        QCOMPARE(m_mock->holds.size(), 2);
        inhibitor.setPlaybackActive(false, false);
        QTRY_VERIFY(m_mock->holds.isEmpty());
        inhibitor.setPlaybackActive(true, false);
        QTRY_COMPARE(inhibitor.inhibitedFlags(), 4u);
        QCOMPARE(m_mock->holds.size(), 1);
    }
    void absentServicesAndRecovery()
    {
        m_mock->portalSupported = false;
        LinuxPlaybackInhibitor inhibitor;
        inhibitor.setPlaybackActive(true, true);
        QTRY_VERIFY(!inhibitor.lastError().isEmpty());
        QTRY_COMPARE(m_mock->attempts, 5); // Portal, GNOME, power, canonical + old screensaver paths.
        QCOMPARE(inhibitor.inhibitedFlags(), 0u);
        QVERIFY(m_mock->holds.isEmpty());
        const int attempted = m_mock->attempts;
        QTest::qWait(50);
        QCOMPARE(m_mock->attempts, attempted); // No idle polling/retry loop.
        m_mock->portalSupported = true;
        QVERIFY(m_server->unregisterService(portal));
        QVERIFY(m_server->registerService(portal));
        QTRY_COMPARE(inhibitor.inhibitedFlags(), 12u);
    }
    void repeatedTransitionsReleaseEveryLease()
    {
        m_mock->replyDelayMs = 30;
        LinuxPlaybackInhibitor inhibitor;
        for (int i = 0; i < 12; ++i) {
            inhibitor.setPlaybackActive(true, i % 2 == 0);
            QTest::qWait(2);
            inhibitor.setPlaybackActive(false, false);
        }
        QTRY_VERIFY(m_mock->holds.isEmpty());
        QCOMPARE(inhibitor.inhibitedFlags(), 0u);
    }
    void absentBusServicesRecoverOnRegistration()
    {
        for (const auto &service : {portal, gnome, saver, power}) QVERIFY(m_server->unregisterService(service));
        LinuxPlaybackInhibitor inhibitor;
        inhibitor.setPlaybackActive(true, true);
        QTRY_VERIFY(!inhibitor.lastError().isEmpty());
        QCOMPARE(inhibitor.inhibitedFlags(), 0u);
        QCOMPARE(m_mock->acquireCount, 0);
        QVERIFY(m_server->registerService(portal));
        QTRY_COMPARE(inhibitor.inhibitedFlags(), 12u);
        inhibitor.setPlaybackActive(false, false);
        QTRY_VERIFY(m_mock->holds.isEmpty());
    }
    void portalResponseFailureAndEarlySuccess()
    {
        m_mock->responseBeforeMethodReply = true;
        LinuxPlaybackInhibitor inhibitor;
        inhibitor.setPlaybackActive(true, true);
        QTRY_COMPARE(inhibitor.inhibitedFlags(), 12u);
        inhibitor.setPlaybackActive(false, false);
        QTRY_VERIFY(m_mock->holds.isEmpty());
        m_mock->portalResponseCode = 2;
        m_mock->gnomeSupported = true;
        inhibitor.setPlaybackActive(true, true);
        QTRY_COMPARE(inhibitor.inhibitedFlags(), 12u);
        QTRY_COMPARE(m_mock->holds.size(), 1);
        QCOMPARE(m_mock->releaseCount, 2); // Prior success + rejected portal handle.
        QCOMPARE(m_mock->attempts, 3); // Successful portal, rejected portal, GNOME.
    }
    void portalNeverRespondsFallsBackAndReleases()
    {
        m_mock->emitPortalResponse = false;
        m_mock->gnomeSupported = true;
        LinuxPlaybackInhibitor inhibitor;
        inhibitor.setPlaybackActive(true, true);
        QTRY_COMPARE(m_mock->acquireCount, 1);
        QCOMPARE(inhibitor.inhibitedFlags(), 0u); // Handle alone is not success.
        QTRY_COMPARE_WITH_TIMEOUT(inhibitor.inhibitedFlags(), 12u, 4000);
        QTRY_COMPARE(m_mock->holds.size(), 1);
        inhibitor.setPlaybackActive(false, false);
        QTRY_VERIFY(m_mock->holds.isEmpty());
    }
    void mprisMetadataCommandsAndIdle()
    {
        LinuxMediaSession session;
        QVERIFY2(session.available(), qPrintable(session.unavailableReason()));
        auto values = readProperties(session.serviceName());
        QCOMPARE(values.value(QStringLiteral("PlaybackStatus")).toString(), QStringLiteral("Stopped"));
        QVERIFY(!values.value(QStringLiteral("CanPlay")).toBool());
        QSignalSpy commands(&session, &MediaSession::commandRequested);
        QVERIFY(mediaCall(session.serviceName(), QStringLiteral("Play")).type() != QDBusMessage::ErrorMessage);
        QCOMPARE(commands.count(), 0);
        MediaSessionState state;
        state.status = MediaSessionState::Status::Playing;
        state.hasMedia = true;
        state.canPlay = true;
        state.canPause = true;
        state.canSeek = true;
        state.canNext = true;
        state.title = QStringLiteral("MPRIS mock-bus test");
        state.trackId = QStringLiteral("/org/mpris/MediaPlayer2/track/t10");
        state.url = QStringLiteral("file:///tmp/video.mkv");
        state.positionUs = 30000000;
        state.durationUs = 120000000;
        session.publish(state);
        values = readProperties(session.serviceName());
        QCOMPARE(values.value(QStringLiteral("PlaybackStatus")).toString(), QStringLiteral("Playing"));
        const auto metadata = qdbus_cast<QVariantMap>(values.value(QStringLiteral("Metadata")));
        QCOMPARE(metadata.value(QStringLiteral("xesam:title")).toString(), state.title);
        QCOMPARE(qvariant_cast<QDBusObjectPath>(metadata.value(QStringLiteral("mpris:trackid"))).path(), state.trackId);
        QCOMPARE(metadata.value(QStringLiteral("mpris:length")).toLongLong(), state.durationUs);
        QCOMPARE(values.value(QStringLiteral("Position")).toLongLong(), state.positionUs);
        mediaCall(session.serviceName(), QStringLiteral("Pause"));
        QCOMPARE(commands.count(), 1);
        QCOMPARE(qvariant_cast<MediaSession::Command>(commands.last().first()), MediaSession::Command::Pause);
        mediaCall(session.serviceName(), QStringLiteral("Previous"));
        QCOMPARE(commands.count(), 1);
        mediaCall(session.serviceName(), QStringLiteral("Next"));
        QCOMPARE(commands.count(), 2);
        state.status = MediaSessionState::Status::Paused;
        session.publish(state);
        mediaCall(session.serviceName(), QStringLiteral("Play"));
        QCOMPARE(commands.count(), 3);
        state.status = MediaSessionState::Status::Stopped;
        state.canPause = false;
        session.publish(state);
        values = readProperties(session.serviceName());
        QCOMPARE(values.value(QStringLiteral("PlaybackStatus")).toString(), QStringLiteral("Stopped"));
        QVERIFY(values.value(QStringLiteral("CanPlay")).toBool());
        mediaCall(session.serviceName(), QStringLiteral("Pause"));
        QCOMPARE(commands.count(), 3);
    }
    void mprisSeekValidationAndQuietPosition()
    {
        LinuxMediaSession session;
        QVERIFY(session.available());
        QVERIFY(QDBusConnection::sessionBus().connect(session.serviceName(), mediaPath,
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
            SLOT(propertiesChanged(QString,QVariantMap,QStringList))));
        QVERIFY(QDBusConnection::sessionBus().connect(session.serviceName(), mediaPath, playerInterface,
            QStringLiteral("Seeked"), this, SLOT(seeked(qlonglong))));
        MediaSessionState state;
        state.hasMedia = true;
        state.canPlay = true; state.canPause = true; state.canSeek = true;
        state.trackId = QStringLiteral("/org/mpris/MediaPlayer2/track/t11");
        state.positionUs = 10000000; state.durationUs = 120000000;
        state.status = MediaSessionState::Status::Paused;
        session.publish(state);
        QTRY_VERIFY(!m_properties.isEmpty());
        m_properties.clear();
        QSignalSpy seeks(&session, &MediaSession::seekRequested);
        mediaCall(session.serviceName(), QStringLiteral("SetPosition"),
                  {QVariant::fromValue(QDBusObjectPath(QStringLiteral("/old_track"))), qlonglong(5000000)});
        mediaCall(session.serviceName(), QStringLiteral("SetPosition"),
                  {QVariant::fromValue(QDBusObjectPath(state.trackId)), qlonglong(-1)});
        QCOMPARE(seeks.count(), 0);
        mediaCall(session.serviceName(), QStringLiteral("SetPosition"),
                  {QVariant::fromValue(QDBusObjectPath(state.trackId)), qlonglong(5000000)});
        QCOMPARE(seeks.takeFirst().first().toDouble(), 5.0);
        state.positionUs = 20000000;
        session.publish(state);
        QTRY_COMPARE(m_seeked.size(), 1);
        QCOMPARE(m_seeked.first(), 20000000);
        QVERIFY(m_properties.isEmpty()); // Position alone is never PropertiesChanged.
    }
    void mprisTeardownUnregistersService()
    {
        auto session = std::make_unique<LinuxMediaSession>();
        const QString name = session->serviceName();
        QVERIFY(QDBusConnection::sessionBus().interface()->isServiceRegistered(name).value());
        session.reset();
        QTRY_VERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered(name).value());
    }
};
QTEST_GUILESS_MAIN(TestLinuxDesktop)
#include "test_linuxdesktop.moc"
