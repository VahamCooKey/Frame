#include "MediaSession.h"

#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
qint64 microseconds(double seconds)
{
    // Leave headroom for Windows' 100 ns timeline representation.
    constexpr qint64 maximum = std::numeric_limits<qint64>::max() / 10;
    if (!std::isfinite(seconds) || seconds <= 0) return 0;
    // MSVC's long double is double and rounds `maximum` upwards. Clamp the
    // integer too, keeping the subsequent multiplication by ten overflow-safe.
    return std::min(maximum, static_cast<qint64>(std::min(static_cast<long double>(seconds) * 1000000.0L,
                                                       static_cast<long double>(maximum))));
}
}

MediaSessionState mediaSessionState(const MediaSessionInput &input)
{
    MediaSessionState result;
    const bool loading = input.playback == MediaSessionInput::Playback::Loading;
    const bool error = input.playback == MediaSessionInput::Playback::Error;
    result.hasMedia = input.loaded && !input.file.isEmpty() && !loading && !error
                      && (input.playback != MediaSessionInput::Playback::Idle || input.eof);
    if (loading) result.status = MediaSessionState::Status::Loading;
    if (!result.hasMedia) return result;

    result.trackId = input.trackId;
    result.title = input.title;
    QUrl url(input.file);
    if (QFileInfo(input.file).isAbsolute() || url.scheme().isEmpty())
        url = QUrl::fromLocalFile(input.file);
    result.url = url.toString(QUrl::FullyEncoded);
    if (result.title.isEmpty())
        result.title = url.isLocalFile() ? QFileInfo(url.toLocalFile()).fileName() : url.fileName();
    if (result.title.isEmpty()) result.title = QStringLiteral("Frame");
    result.hasVideo = input.hasVideo;
    result.durationUs = microseconds(input.duration);
    result.positionUs = microseconds(input.position);
    if (result.durationUs > 0) result.positionUs = std::min(result.positionUs, result.durationUs);
    result.rate = std::isfinite(input.rate) && input.rate > 0 ? input.rate : 1.0;
    result.volume = input.muted || !std::isfinite(input.volume) ? 0 : std::clamp(input.volume / 100.0, 0.0, 1.0);
    result.status = input.eof ? MediaSessionState::Status::Stopped
                             : input.paused ? MediaSessionState::Status::Paused : MediaSessionState::Status::Playing;
    result.canPlay = true; // Includes replay of the final keep-open frame.
    result.canPause = !input.eof;
    result.canSeek = input.seekable && result.durationUs > 0;
    result.canNext = input.playlistIndex >= 0 && input.playlistIndex + 1 < input.playlistCount;
    result.canPrevious = input.playlistIndex > 0 && input.playlistIndex < input.playlistCount;
    return result;
}

MediaSession::MediaSession(QObject *parent) : QObject(parent)
{
    m_positionClock.start();
}

void MediaSession::publish(const MediaSessionState &state)
{
    const auto previous = m_state;
    const double elapsedUs = static_cast<double>(m_positionClock.nsecsElapsed()) / 1000.0;
    const double expected = static_cast<double>(previous.positionUs)
        + (previous.status == MediaSessionState::Status::Playing ? elapsedUs * previous.rate : 0);
    const bool discontinuity = state.hasMedia && previous.hasMedia
        && state.trackId == previous.trackId
        && std::abs(static_cast<double>(state.positionUs) - expected) > 750000.0;
    m_state = state;
    m_positionClock.restart();
    update(previous, discontinuity);
}

void MediaSession::request(Command command)
{
    bool allowed = false;
    switch (command) {
    case Command::Raise: allowed = true; break;
    case Command::Play: allowed = m_state.canPlay && m_state.status != MediaSessionState::Status::Playing; break;
    case Command::Pause: allowed = m_state.canPause && m_state.status == MediaSessionState::Status::Playing; break;
    case Command::TogglePause: allowed = m_state.canPlay; break;
    case Command::Stop: allowed = m_state.hasMedia; break;
    case Command::Next: allowed = m_state.canNext; break;
    case Command::Previous: allowed = m_state.canPrevious; break;
    }
    if (allowed) emit commandRequested(command);
}

void MediaSession::requestSeek(qint64 positionUs, const QString &trackId)
{
    if (!m_state.canSeek || positionUs < 0 || positionUs > m_state.durationUs) return;
    if (!trackId.isEmpty() && trackId != m_state.trackId) return;
    emit seekRequested(static_cast<double>(positionUs) / 1000000.0);
}

void MediaSession::requestSeekRelative(qint64 offsetUs)
{
    if (!m_state.canSeek) return;
    // Do not add an untrusted D-Bus int64 directly (signed overflow).
    const long double destination = static_cast<long double>(m_state.positionUs) + offsetUs;
    if (destination > m_state.durationUs) {
        request(Command::Next);
        return;
    }
    requestSeek(static_cast<qint64>(std::max(0.0L, destination)));
}

void MediaSession::requestVolume(double volume)
{
    if (m_state.hasMedia && std::isfinite(volume)) emit volumeRequested(std::clamp(volume, 0.0, 1.0) * 100.0);
}

void MediaSession::requestRate(double rate)
{
    if (!m_state.hasMedia || !std::isfinite(rate)) return;
    if (rate == 0) request(Command::Pause);
    else if (rate >= 0.1 && rate <= 4.0) emit rateRequested(rate);
}
