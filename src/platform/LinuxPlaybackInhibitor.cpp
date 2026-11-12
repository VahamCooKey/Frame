#include "LinuxPlaybackInhibitor.h"

#include <QDBusMessage>
#include <QDBusError>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QTimer>
#include <QUuid>
#include <QVariantMap>

#include <functional>
#include <utility>

namespace {
constexpr quint32 kSuspend = 4;
constexpr quint32 kIdle = 8;
constexpr int kReplyTimeoutMs = 1500;
const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kPortalInterface = QStringLiteral("org.freedesktop.portal.Inhibit");
const QString kGnomeService = QStringLiteral("org.gnome.SessionManager");
const QString kGnomePath = QStringLiteral("/org/gnome/SessionManager");
const QString kSaverService = QStringLiteral("org.freedesktop.ScreenSaver");
const QString kSaverPath = QStringLiteral("/org/freedesktop/ScreenSaver");
const QString kPowerService = QStringLiteral("org.freedesktop.PowerManagement");
const QString kPowerInterface = QStringLiteral("org.freedesktop.PowerManagement.Inhibit");
const QString kPowerPath = QStringLiteral("/org/freedesktop/PowerManagement/Inhibit");
const QString kReason = QStringLiteral("Frame media playback");

struct Lease {
    QString owner;
    QString path;
    QString interface;
    QString releaseMethod;
    QVariant argument;
    quint32 flags{0};
};
}

class LinuxInhibitRequest final : public QObject {
    Q_OBJECT
public:
    using Result = std::function<void(quint32, const QString &)>;

    LinuxInhibitRequest(quint32 flags, Result result, QObject *parent)
        : QObject(parent),
          m_connectionName(QStringLiteral("frame-inhibit-")
                           + QUuid::createUuid().toString(QUuid::Id128)),
          m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName)),
          m_flags(flags), m_result(std::move(result))
    {
        m_portalTimer.setSingleShot(true);
        connect(&m_portalTimer, &QTimer::timeout, this, [this] {
            m_portalFinished = true;
            stopPortalWaiting();
            releasePortalHandle();
            if (!m_cancelled) acquireGnome();
            finishCancellation();
        });
    }

    ~LinuxInhibitRequest() override
    {
        // D-Bus services release inhibitors when the requesting client exits.
        // This also covers an outstanding reply whose cookie is not known yet.
        QDBusConnection::disconnectFromBus(m_connectionName);
    }

    void start()
    {
        if (!m_bus.isConnected()) {
            m_result(0, QStringLiteral("Session D-Bus is unavailable"));
            return;
        }
        resolveOwner(kPortalService, true, [this](const QString &owner) {
            if (m_cancelled) return;
            if (owner.isEmpty()) { acquireGnome(); return; }
            m_portalOwner = owner;
            // Subscribe before Inhibit. A dedicated connection has only this
            // portal request; buffer early responses by path until its method
            // reply tells us the real handle (also works with older portals).
            if (!m_bus.connect(owner, QString(), QStringLiteral("org.freedesktop.portal.Request"),
                               QStringLiteral("Response"), this,
                               SLOT(portalResponse(uint,QVariantMap,QDBusMessage)))) {
                acquireGnome();
                return;
            }
            m_waitingPortal = true;
            ++m_pending;
            auto message = QDBusMessage::createMethodCall(owner, kPortalPath,
                                                          kPortalInterface, QStringLiteral("Inhibit"));
            const QVariantMap options{{QStringLiteral("reason"), kReason},
                {QStringLiteral("handle_token"),
                 QStringLiteral("frame_") + QUuid::createUuid().toString(QUuid::Id128)}};
            // The portal explicitly permits an empty parent. No private Qt
            // Wayland surface-export API or fabricated X11 identifier is used.
            message.setArguments({QString(), m_flags, options});
            call(message, [this](const QDBusMessage &reply) {
                QDBusPendingReply<QDBusObjectPath> parsed(reply);
                if (parsed.isError() || parsed.value().path().isEmpty()
                    || parsed.value().path() == QStringLiteral("/")) {
                    stopPortalWaiting();
                    if (!m_cancelled && !m_portalFinished) acquireGnome();
                    m_portalFinished = true;
                    return;
                }
                m_portalHandle = parsed.value().path();
                if (m_cancelled || m_portalFinished) {
                    stopPortalWaiting();
                    releasePortalHandle();
                } else if (m_earlyResponses.contains(m_portalHandle)) {
                    completePortal(m_earlyResponses.value(m_portalHandle));
                } else m_portalTimer.start(kReplyTimeoutMs);
            });
        });
    }

    void cancel()
    {
        if (m_cancelled) return;
        m_cancelled = true;
        stopPortalWaiting();
        releasePortalHandle();
        const auto leases = std::exchange(m_leases, {});
        for (const auto &lease : leases) release(lease);
        finishCancellation();
    }

private:
    void stopPortalWaiting()
    {
        if (!m_waitingPortal) return;
        m_portalTimer.stop();
        m_bus.disconnect(m_portalOwner, QString(), QStringLiteral("org.freedesktop.portal.Request"),
                         QStringLiteral("Response"), this,
                         SLOT(portalResponse(uint,QVariantMap,QDBusMessage)));
        m_waitingPortal = false;
        --m_pending;
        m_earlyResponses.clear();
    }

    void releasePortalHandle()
    {
        const QString handle = std::exchange(m_portalHandle, {});
        if (!handle.isEmpty()) release({m_portalOwner, handle,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Close"), {}, m_flags});
    }

    void completePortal(quint32 response)
    {
        m_portalFinished = true;
        stopPortalWaiting();
        if (response == 0 && !m_cancelled) {
            addLease({m_portalOwner, std::exchange(m_portalHandle, {}),
                QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Close"), {}, m_flags});
        } else {
            releasePortalHandle();
            if (!m_cancelled) acquireGnome();
        }
        finishCancellation();
    }

private slots:
    void portalResponse(uint response, const QVariantMap &, const QDBusMessage &message)
    {
        if (!m_waitingPortal) return;
        if (m_portalHandle.isEmpty()) m_earlyResponses.insert(message.path(), response);
        else if (message.path() == m_portalHandle) completePortal(response);
    }

private:
    void resolveOwner(const QString &service, bool activate,
                      std::function<void(const QString &)> done)
    {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("GetNameOwner"));
        message.setArguments({service});
        call(message, [this, service, activate, done = std::move(done)](const QDBusMessage &reply) {
            if (m_cancelled) return;
            QDBusPendingReply<QString> owner(reply);
            if (!owner.isError()) { done(owner.value()); return; }
            if (!activate) { done({}); return; }
            auto start = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
                QStringLiteral("StartServiceByName"));
            start.setArguments({service, quint32(0)});
            call(start, [this, service, done](const QDBusMessage &started) {
                if (m_cancelled) return;
                if (started.type() == QDBusMessage::ErrorMessage) done({});
                else resolveOwner(service, false, done);
            });
        });
    }

    void serviceCall(const QDBusMessage &message,
                     std::function<void(const QDBusMessage &, const QString &)> done)
    {
        // QDBusPendingCall's reconstructed reply does not reliably retain the
        // sender. Resolve/activate asynchronously, then pin both acquire and
        // release to the same unique owner across desktop service restarts.
        resolveOwner(message.service(), true, [this, message, done = std::move(done)](const QString &owner) {
            if (m_cancelled) return;
            if (owner.isEmpty()) {
                done(QDBusMessage::createError(QDBusError::ServiceUnknown,
                                               QStringLiteral("Desktop inhibition service unavailable")), {});
                return;
            }
            auto addressed = QDBusMessage::createMethodCall(owner, message.path(), message.interface(), message.member());
            addressed.setArguments(message.arguments());
            call(addressed, [done, owner](const QDBusMessage &reply) { done(reply, owner); });
        });
    }

    void call(const QDBusMessage &message, std::function<void(const QDBusMessage &)> done)
    {
        ++m_pending;
        auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, kReplyTimeoutMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, done = std::move(done)](QDBusPendingCallWatcher *finished) {
            const QDBusMessage reply = finished->reply();
            --m_pending;
            finished->deleteLater();
            done(reply);
            finishCancellation();
        });
    }

    void acquireGnome()
    {
        auto message = QDBusMessage::createMethodCall(kGnomeService, kGnomePath,
                                                      kGnomeService, QStringLiteral("Inhibit"));
        message.setArguments({QStringLiteral("Frame"), quint32(0), kReason, m_flags});
        serviceCall(message, [this](const QDBusMessage &reply, const QString &owner) {
            QDBusPendingReply<quint32> parsed(reply);
            if (parsed.isError()) {
                if (!m_cancelled) acquireLegacy();
                return;
            }
            addLease({owner, kGnomePath, kGnomeService,
                      QStringLiteral("Uninhibit"), parsed.value(), m_flags});
        });
    }

    void acquireLegacy()
    {
        // PowerDevil's suspend inhibitor and the standard screensaver idle
        // inhibitor are separate. Audio asks only for suspend inhibition.
        acquireCookie(kPowerService, kPowerPath, kPowerInterface, kSuspend);
        if (m_flags & kIdle)
            acquireCookie(kSaverService, kSaverPath, kSaverService, kIdle, QStringLiteral("/ScreenSaver"));
    }

    void acquireCookie(const QString &service, const QString &path,
                       const QString &interface, quint32 flags, const QString &fallbackPath = {})
    {
        auto message = QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("Inhibit"));
        message.setArguments({QStringLiteral("Frame"), kReason});
        serviceCall(message, [this, service, path, interface, flags, fallbackPath](const QDBusMessage &reply, const QString &owner) {
            QDBusPendingReply<quint32> parsed(reply);
            if (parsed.isError()) {
                if (!m_cancelled) {
                    if (!fallbackPath.isEmpty()) acquireCookie(service, fallbackPath, interface, flags);
                    else report(parsed.error().message());
                }
                return;
            }
            addLease({owner, path, interface, QStringLiteral("UnInhibit"),
                      parsed.value(), flags});
        });
    }

    void addLease(Lease lease)
    {
        // Release to the acquire's unique owner, never a replacement service
        // that might have reused the same cookie after a desktop restart.
        if (m_cancelled) release(lease);
        else {
            m_leases.append(std::move(lease));
            report({});
        }
    }

    void release(const Lease &lease)
    {
        auto message = QDBusMessage::createMethodCall(lease.owner, lease.path,
                                                      lease.interface, lease.releaseMethod);
        if (lease.argument.isValid()) message.setArguments({lease.argument});
        call(message, [](const QDBusMessage &) {});
    }

    void report(const QString &error)
    {
        quint32 held = 0;
        for (const auto &lease : m_leases) held |= lease.flags;
        m_result(held, held == m_flags ? QString()
                     : error.isEmpty() ? QStringLiteral("Desktop inhibition is incomplete") : error);
    }

    void finishCancellation()
    {
        if (m_cancelled && m_pending == 0) {
            QDBusConnection::disconnectFromBus(m_connectionName);
            deleteLater();
        }
    }

    const QString m_connectionName;
    QDBusConnection m_bus;
    quint32 m_flags;
    Result m_result;
    QList<Lease> m_leases;
    int m_pending{0};
    bool m_cancelled{false};
    QTimer m_portalTimer;
    QString m_portalOwner;
    QString m_portalHandle;
    QHash<QString, quint32> m_earlyResponses;
    bool m_waitingPortal{false};
    bool m_portalFinished{false};
};

LinuxPlaybackInhibitor::LinuxPlaybackInhibitor(QObject *parent) : QObject(parent)
{
    auto *watcher = new QDBusServiceWatcher(this);
    watcher->setConnection(QDBusConnection::sessionBus());
    watcher->setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    watcher->setWatchedServices({kPortalService, kGnomeService, kSaverService, kPowerService});
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &oldOwner, const QString &) {
        // Recovery is driven by desktop ownership changes, never a retry loop.
        // Do not restart on our own successful auto-activation of a service.
        if (m_requestedFlags && (!oldOwner.isEmpty() || !m_lastError.isEmpty())) acquire();
    });
}

LinuxPlaybackInhibitor::~LinuxPlaybackInhibitor()
{
    // Tear down children while our result state still exists. Outstanding
    // service calls are cut off by each request's owned connection.
    const auto objects = children();
    for (auto *object : objects)
        if (auto *request = dynamic_cast<LinuxInhibitRequest *>(object)) delete request;
}

void LinuxPlaybackInhibitor::setPlaybackActive(bool playing, bool hasVideo)
{
    const quint32 flags = playing ? kSuspend | (hasVideo ? kIdle : 0) : 0;
    if (flags == m_requestedFlags) return;
    m_requestedFlags = flags;
    acquire();
}

void LinuxPlaybackInhibitor::acquire()
{
    if (m_request) {
        m_request->cancel();
        m_request = nullptr;
    }
    setResult(0, {});
    if (!m_requestedFlags) return;
    auto *request = new LinuxInhibitRequest(m_requestedFlags,
        [this](quint32 flags, const QString &error) { setResult(flags, error); }, this);
    m_request = request;
    request->start();
}

void LinuxPlaybackInhibitor::setResult(quint32 flags, const QString &error)
{
    if (m_inhibitedFlags == flags && m_lastError == error) return;
    m_inhibitedFlags = flags;
    m_lastError = error;
    emit inhibitionChanged();
}

#include "LinuxPlaybackInhibitor.moc"
