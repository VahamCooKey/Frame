#include "LinuxMediaSession.h"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QUuid>
#include <QVariantMap>

namespace {
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");

QString playbackStatus(const MediaSessionState &state)
{
    switch (state.status) {
    case MediaSessionState::Status::Playing: return QStringLiteral("Playing");
    case MediaSessionState::Status::Paused: return QStringLiteral("Paused");
    case MediaSessionState::Status::Loading:
    case MediaSessionState::Status::Stopped: return QStringLiteral("Stopped");
    }
    return QStringLiteral("Stopped");
}

QVariantMap metadata(const MediaSessionState &state)
{
    if (!state.hasMedia) return {};
    QVariantMap result{{QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(state.trackId))},
                       {QStringLiteral("xesam:title"), state.title},
                       {QStringLiteral("xesam:url"), state.url}};
    if (state.durationUs > 0) result.insert(QStringLiteral("mpris:length"), state.durationUs);
    return result;
}

QVariantMap properties(const MediaSessionState &state)
{
    return {{QStringLiteral("PlaybackStatus"), playbackStatus(state)},
            {QStringLiteral("Metadata"), metadata(state)},
            {QStringLiteral("Rate"), state.rate},
            {QStringLiteral("Volume"), state.volume},
            {QStringLiteral("CanGoNext"), state.canNext},
            {QStringLiteral("CanGoPrevious"), state.canPrevious},
            {QStringLiteral("CanPlay"), state.canPlay},
            {QStringLiteral("CanPause"), state.canPause},
            {QStringLiteral("CanSeek"), state.canSeek}};
}
}

class MprisRootAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ schemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ mimeTypes CONSTANT)
public:
    explicit MprisRootAdaptor(LinuxMediaSession *session)
        : QDBusAbstractAdaptor(session), m_session(session) {}
    bool canQuit() const { return false; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Frame"); }
    QString desktopEntry() const { return QStringLiteral("frame"); }
    QStringList schemes() const { return {}; } // No remote OpenUri/autoplay surface.
    QStringList mimeTypes() const { return {}; }
public slots:
    void Raise() { m_session->request(MediaSession::Command::Raise); }
    void Quit() {} // CanQuit is false.
private:
    LinuxMediaSession *m_session;
};

class MprisPlayerAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ status)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(QVariantMap Metadata READ currentMetadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canNext)
    Q_PROPERTY(bool CanGoPrevious READ canPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)
public:
    explicit MprisPlayerAdaptor(LinuxMediaSession *session)
        : QDBusAbstractAdaptor(session), m_session(session) {}
    QString status() const { return playbackStatus(m_session->state()); }
    double rate() const { return m_session->state().rate; }
    void setRate(double value) { m_session->requestRate(value); }
    QVariantMap currentMetadata() const { return metadata(m_session->state()); }
    double volume() const { return m_session->state().volume; }
    void setVolume(double value) { m_session->requestVolume(value); }
    qlonglong position() const { return m_session->state().positionUs; }
    double minimumRate() const { return 0.1; }
    double maximumRate() const { return 4.0; }
    bool canNext() const { return m_session->state().canNext; }
    bool canPrevious() const { return m_session->state().canPrevious; }
    bool canPlay() const { return m_session->state().canPlay; }
    bool canPause() const { return m_session->state().canPause; }
    bool canSeek() const { return m_session->state().canSeek; }
    bool canControl() const { return true; }
public slots:
    void Next() { m_session->request(MediaSession::Command::Next); }
    void Previous() { m_session->request(MediaSession::Command::Previous); }
    void Pause() { m_session->request(MediaSession::Command::Pause); }
    void PlayPause() { m_session->request(MediaSession::Command::TogglePause); }
    void Stop() { m_session->request(MediaSession::Command::Stop); }
    void Play() { m_session->request(MediaSession::Command::Play); }
    void Seek(qlonglong offset) { m_session->requestSeekRelative(offset); }
    void SetPosition(const QDBusObjectPath &trackId, qlonglong position)
    {
        if (trackId.path() == QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack")) return;
        m_session->requestSeek(position, trackId.path());
    }
    void OpenUri(const QString &) {} // SupportedUriSchemes is empty.
signals:
    void Seeked(qlonglong position);
private:
    LinuxMediaSession *m_session;
};

LinuxMediaSession::LinuxMediaSession(QObject *parent)
    : MediaSession(parent),
      m_connectionName(QStringLiteral("frame-mpris-") + QUuid::createUuid().toString(QUuid::Id128)),
      m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName))
{
    if (!m_bus.isConnected()) {
        m_error = QStringLiteral("Session D-Bus is unavailable");
        return;
    }
    new MprisRootAdaptor(this);
    new MprisPlayerAdaptor(this);
    // A dedicated connection owns this fixed object path. The instance suffix
    // also permits development/test players beside the single-instance app.
    m_service = QStringLiteral("org.mpris.MediaPlayer2.Frame.instance%1_%2")
        .arg(QCoreApplication::applicationPid()).arg(QUuid::createUuid().toString(QUuid::Id128));
    if (!m_bus.registerObject(kPath, this, QDBusConnection::ExportAdaptors)
        || !m_bus.registerService(m_service)) {
        m_error = m_bus.lastError().message();
        m_bus.unregisterObject(kPath);
        return;
    }
    m_available = true;
}

LinuxMediaSession::~LinuxMediaSession()
{
    if (m_available) m_bus.unregisterService(m_service);
    m_bus.unregisterObject(kPath);
    QDBusConnection::disconnectFromBus(m_connectionName);
}

void LinuxMediaSession::update(const MediaSessionState &previous, bool discontinuity)
{
    if (!m_available) return;
    const auto before = properties(previous);
    const auto after = properties(state());
    QVariantMap changed;
    for (auto it = after.cbegin(); it != after.cend(); ++it)
        if (before.value(it.key()) != it.value()) changed.insert(it.key(), it.value());
    if (!changed.isEmpty()) {
        auto message = QDBusMessage::createSignal(kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
        message.setArguments({kPlayer, changed, QStringList()});
        m_bus.send(message);
    }
    // Position never appears in PropertiesChanged. Existing controller events
    // provide clock updates; only seeks/discontinuities need an extra signal.
    if (discontinuity) {
        auto message = QDBusMessage::createSignal(kPath, kPlayer, QStringLiteral("Seeked"));
        message.setArguments({state().positionUs});
        m_bus.send(message);
    }
}

#include "LinuxMediaSession.moc"
