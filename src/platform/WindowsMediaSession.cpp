#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <roapi.h>
#include <systemmediatransportcontrolsinterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>

#include "WindowsMediaSession.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QQuickWindow>

#include <functional>
#include <mutex>

namespace {
using namespace winrt::Windows::Media;

winrt::hstring nativeString(const QString &text)
{
    return winrt::hstring(reinterpret_cast<const wchar_t *>(text.utf16()), static_cast<quint32>(text.size()));
}

QString errorString(const winrt::hresult_error &error)
{
    const auto message = error.message();
    return QString::fromWCharArray(message.c_str(), static_cast<qsizetype>(message.size()));
}

MediaPlaybackStatus playbackStatus(MediaSessionState::Status status)
{
    switch (status) {
    case MediaSessionState::Status::Loading: return MediaPlaybackStatus::Changing;
    case MediaSessionState::Status::Paused: return MediaPlaybackStatus::Paused;
    case MediaSessionState::Status::Playing: return MediaPlaybackStatus::Playing;
    case MediaSessionState::Status::Stopped: return MediaPlaybackStatus::Stopped;
    }
    return MediaPlaybackStatus::Closed;
}

struct CallbackTarget {
    std::mutex mutex;
    WindowsMediaSession *owner{nullptr};
    QString trackId;

    // SMTC events can arrive on a Windows worker thread. Holding the lock
    // through invokeMethod prevents QObject destruction racing event posting.
    void post(std::function<void(WindowsMediaSession *, const QString &)> action)
    {
        const std::lock_guard lock(mutex);
        if (!owner) return;
        QMetaObject::invokeMethod(owner,
            [target = owner, track = trackId, action = std::move(action)] { action(target, track); },
            Qt::QueuedConnection);
    }
};
}

struct WindowsMediaSession::Impl {
    SystemMediaTransportControls controls{nullptr};
    SystemMediaTransportControls::ButtonPressed_revoker buttonRevoker;
    SystemMediaTransportControls::PlaybackPositionChangeRequested_revoker positionRevoker;
    SystemMediaTransportControls::PlaybackRateChangeRequested_revoker rateRevoker;
    std::shared_ptr<CallbackTarget> target = std::make_shared<CallbackTarget>();
    QElapsedTimer timelineClock;
    QString error;
    bool initializedApartment{false};
    bool available{false};

    ~Impl()
    {
        {
            const std::lock_guard lock(target->mutex);
            target->owner = nullptr;
        }
        buttonRevoker.revoke();
        positionRevoker.revoke();
        rateRevoker.revoke();
        if (controls) {
            try {
                controls.IsEnabled(false);
                controls.PlaybackStatus(MediaPlaybackStatus::Closed);
                controls.DisplayUpdater().ClearAll();
                controls.DisplayUpdater().Update();
            } catch (const winrt::hresult_error &) {
                // Window/shell teardown can remove the session first.
            }
            controls = nullptr;
        }
        if (initializedApartment) RoUninitialize();
    }
};

WindowsMediaSession::WindowsMediaSession(QQuickWindow *window, QObject *parent)
    : MediaSession(parent), m_impl(std::make_unique<Impl>())
{
    if (!window) {
        m_impl->error = QStringLiteral("SMTC needs a top-level window");
        return;
    }
    // Qt can already own a COM apartment on this thread. Balance only our
    // successful initialization; never uninitialize an apartment we did not own.
    const HRESULT result = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        m_impl->error = QStringLiteral("Windows Runtime initialization failed: 0x%1")
                            .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
        return;
    }
    m_impl->initializedApartment = SUCCEEDED(result);
    try {
        const auto interop = winrt::get_activation_factory<SystemMediaTransportControls,
                                                          ISystemMediaTransportControlsInterop>();
        winrt::check_hresult(interop->GetForWindow(reinterpret_cast<HWND>(window->winId()),
                              winrt::guid_of<SystemMediaTransportControls>(),
                              winrt::put_abi(m_impl->controls)));
        const auto callbacks = m_impl->target;
        callbacks->owner = this;
        m_impl->buttonRevoker = m_impl->controls.ButtonPressed(winrt::auto_revoke,
            [callbacks](const auto &, const SystemMediaTransportControlsButtonPressedEventArgs &args) {
                Command command;
                switch (args.Button()) {
                case SystemMediaTransportControlsButton::Play: command = Command::Play; break;
                case SystemMediaTransportControlsButton::Pause: command = Command::Pause; break;
                case SystemMediaTransportControlsButton::Stop: command = Command::Stop; break;
                case SystemMediaTransportControlsButton::Next: command = Command::Next; break;
                case SystemMediaTransportControlsButton::Previous: command = Command::Previous; break;
                default: return;
                }
                callbacks->post([command](WindowsMediaSession *session, const QString &) { session->request(command); });
            });
        m_impl->positionRevoker = m_impl->controls.PlaybackPositionChangeRequested(winrt::auto_revoke,
            [callbacks](const auto &, const PlaybackPositionChangeRequestedEventArgs &args) {
                const qint64 positionUs = args.RequestedPlaybackPosition().count() / 10;
                callbacks->post([positionUs](WindowsMediaSession *session, const QString &track) {
                    session->requestSeek(positionUs, track);
                });
            });
        m_impl->rateRevoker = m_impl->controls.PlaybackRateChangeRequested(winrt::auto_revoke,
            [callbacks](const auto &, const PlaybackRateChangeRequestedEventArgs &args) {
                const double rate = args.RequestedPlaybackRate();
                callbacks->post([rate](WindowsMediaSession *session, const QString &) { session->requestRate(rate); });
            });
        m_impl->controls.IsEnabled(false); // Idle app never registers as playing.
        m_impl->controls.PlaybackStatus(MediaPlaybackStatus::Stopped);
        m_impl->available = true;
    } catch (const winrt::hresult_error &error) {
        m_impl->error = errorString(error);
    }
}

WindowsMediaSession::~WindowsMediaSession() = default;

bool WindowsMediaSession::available() const { return m_impl->available; }
QString WindowsMediaSession::unavailableReason() const { return m_impl->error; }

void WindowsMediaSession::update(const MediaSessionState &previous, bool discontinuity)
{
    if (!m_impl->available) return;
    const auto &current = state();
    {
        const std::lock_guard lock(m_impl->target->mutex);
        m_impl->target->trackId = current.trackId;
    }
    try {
        auto &controls = m_impl->controls;
        controls.IsPlayEnabled(current.canPlay);
        controls.IsPauseEnabled(current.canPause);
        controls.IsStopEnabled(current.hasMedia);
        controls.IsNextEnabled(current.canNext);
        controls.IsPreviousEnabled(current.canPrevious);
        controls.PlaybackStatus(playbackStatus(current.status));
        if (current.rate != previous.rate) controls.PlaybackRate(current.rate);
        const bool metadataChanged = current.trackId != previous.trackId || current.title != previous.title
                                     || current.hasMedia != previous.hasMedia || current.hasVideo != previous.hasVideo;
        if (metadataChanged) {
            const auto updater = controls.DisplayUpdater();
            updater.ClearAll();
            if (current.hasMedia) {
                updater.Type(current.hasVideo ? MediaPlaybackType::Video : MediaPlaybackType::Music);
                updater.AppMediaId(nativeString(current.trackId));
                if (current.hasVideo) updater.VideoProperties().Title(nativeString(current.title));
                else updater.MusicProperties().Title(nativeString(current.title));
            }
            updater.Update();
        }
        const bool forceTimeline = metadataChanged || discontinuity || previous.status != current.status
                                   || previous.durationUs != current.durationUs || previous.rate != current.rate;
        // SMTC extrapolates its timeline. Update at most once a second during
        // ordinary playback, driven solely by existing position notifications.
        if (current.hasMedia && (forceTimeline || !m_impl->timelineClock.isValid()
                                || m_impl->timelineClock.elapsed() >= 1000)) {
            using winrt::Windows::Foundation::TimeSpan;
            SystemMediaTransportControlsTimelineProperties timeline;
            timeline.StartTime(TimeSpan{0});
            timeline.EndTime(TimeSpan{current.durationUs * 10});
            timeline.MinSeekTime(TimeSpan{0});
            timeline.MaxSeekTime(TimeSpan{current.canSeek ? current.durationUs * 10 : 0});
            timeline.Position(TimeSpan{current.positionUs * 10});
            controls.UpdateTimelineProperties(timeline);
            m_impl->timelineClock.restart();
        }
        controls.IsEnabled(current.hasMedia);
    } catch (const winrt::hresult_error &error) {
        m_impl->available = false;
        m_impl->error = errorString(error);
        try { m_impl->controls.IsEnabled(false); } catch (const winrt::hresult_error &) {}
    }
}
