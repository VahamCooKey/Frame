#include "SystemMediaControls.h"

#include "MediaSession.h"
#include "core/PlayerController.h"

#include <QCoreApplication>
#include <QQuickWindow>

#if defined(Q_OS_WIN)
#include "WindowsMediaSession.h"
#elif defined(Q_OS_LINUX)
#include "LinuxMediaSession.h"
#endif

SystemMediaControls *SystemMediaControls::create(PlayerController *player, QQuickWindow *window, QObject *parent)
{
    return new SystemMediaControls(player, window, parent ? parent : window);
}

SystemMediaControls::SystemMediaControls(PlayerController *player, QQuickWindow *window, QObject *parent)
    : QObject(parent), m_player(player), m_window(window)
{
#if defined(Q_OS_WIN)
    m_session = new WindowsMediaSession(window, this);
#elif defined(Q_OS_LINUX)
    m_session = new LinuxMediaSession(this);
#endif
    // macOS deliberately reports unsupported; no success-shaped no-op backend.
    if (!player || !window || !m_session) return;
    m_loaded = !player->currentFile().isEmpty();
    if (m_loaded) m_seekable = player->isSeekable();
    m_trackGeneration = m_loaded ? 1 : 0;
    const auto refresh = [this] { sync(); };
    connect(player, &PlayerController::fileLoaded, this, [this] {
        m_loaded = true;
        m_seekable = m_player && m_player->isSeekable();
        ++m_trackGeneration;
        sync();
    });
    connect(player, &PlayerController::stateChanged, this, refresh);
    connect(player, &PlayerController::positionChanged, this, refresh);
    connect(player, &PlayerController::durationChanged, this, refresh);
    connect(player, &PlayerController::mediaTitleChanged, this, refresh);
    connect(player, &PlayerController::pauseChanged, this, refresh);
    connect(player, &PlayerController::eofReachedChanged, this, refresh);
    connect(player, &PlayerController::playlistChanged, this, refresh);
    connect(player, &PlayerController::playlistPosChanged, this, refresh);
    connect(player, &PlayerController::videoSizeChanged, this, refresh);
    connect(player, &PlayerController::volumeChanged, this, refresh);
    connect(player, &PlayerController::muteChanged, this, refresh);
    connect(player, &PlayerController::speedChanged, this, refresh);
    connect(player, &QObject::destroyed, this, &SystemMediaControls::shutdown);
    connect(window, &QObject::destroyed, this, &SystemMediaControls::shutdown);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &SystemMediaControls::shutdown);
    connect(m_session, &MediaSession::commandRequested, this, [this](MediaSession::Command command) {
        if (!m_player) return;
        switch (command) {
        case MediaSession::Command::Play: m_player->play(); break;
        case MediaSession::Command::Pause: m_player->pause(); break;
        case MediaSession::Command::TogglePause: m_player->togglePause(); break;
        case MediaSession::Command::Stop: m_player->stop(); break;
        // MPRIS navigation preserves a paused/stopped session. The controller's
        // normal card selection means Play; an immediate explicit Pause cancels
        // that selection's pending unpause before its async reply can run.
        case MediaSession::Command::Next:
        case MediaSession::Command::Previous: {
            const bool stayPaused = m_player->isPaused() || m_player->eofReached();
            if (command == MediaSession::Command::Next) m_player->playlistNext();
            else m_player->playlistPrev();
            if (stayPaused) m_player->pause();
            break;
        }
        case MediaSession::Command::Raise:
            if (m_window) {
                if (m_window->windowStates().testFlag(Qt::WindowMinimized))
                    m_window->setWindowStates(m_window->windowStates() & ~Qt::WindowMinimized);
                m_window->raise();
                m_window->requestActivate();
            }
            break;
        }
    });
    connect(m_session, &MediaSession::seekRequested, player, &PlayerController::seek);
    connect(m_session, &MediaSession::volumeRequested, player, [player](double volume) {
        if (volume > 0 && player->isMuted()) player->setMuted(false);
        player->setVolume(volume);
    });
    connect(m_session, &MediaSession::rateRequested, player, &PlayerController::setSpeed);
    sync();
}

SystemMediaControls::~SystemMediaControls() { shutdown(); }

bool SystemMediaControls::available() const { return m_session && m_session->available(); }

QString SystemMediaControls::unavailableReason() const
{
    return m_session ? m_session->unavailableReason() : QStringLiteral("System media controls are unsupported on this platform");
}

void SystemMediaControls::shutdown()
{
    delete m_session;
    m_session = nullptr;
}

void SystemMediaControls::sync()
{
    if (!m_session || !m_player) return;
    MediaSessionInput input;
    switch (m_player->state()) {
    case PlayerController::State::Idle: input.playback = MediaSessionInput::Playback::Idle; break;
    case PlayerController::State::Loading: input.playback = MediaSessionInput::Playback::Loading; break;
    case PlayerController::State::Buffering: input.playback = MediaSessionInput::Playback::Buffering; break;
    case PlayerController::State::Playing: input.playback = MediaSessionInput::Playback::Playing; break;
    case PlayerController::State::Paused: input.playback = MediaSessionInput::Playback::Paused; break;
    case PlayerController::State::Seeking: input.playback = MediaSessionInput::Playback::Seeking; break;
    case PlayerController::State::Error: input.playback = MediaSessionInput::Playback::Error; break;
    }
    // EOF replay clears eof-reached just before leaving Idle. A transient
    // Idle notification must not forget a successfully loaded keep-open file;
    // mediaSessionState already gates ordinary stop/idle without this latch.
    input.loaded = m_loaded;
    input.seekable = m_seekable;
    input.paused = m_player->isPaused();
    input.eof = m_player->eofReached();
    input.hasVideo = m_player->videoWidth() > 0;
    input.trackId = QStringLiteral("/org/mpris/MediaPlayer2/track/t%1").arg(m_trackGeneration);
    input.title = m_player->mediaTitle();
    input.file = m_player->currentFile();
    input.position = m_player->position();
    input.duration = m_player->duration();
    input.rate = m_player->speed();
    input.volume = m_player->volume();
    input.muted = m_player->isMuted();
    input.playlistIndex = m_player->playlistPos();
    input.playlistCount = static_cast<int>(m_player->playlist().size());
    m_session->publish(mediaSessionState(input));
}
