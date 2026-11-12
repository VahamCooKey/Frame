// Requires a real interactive Windows session; gate with FRAME_ENABLE_RENDER_TESTS.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

#include "platform/NativeWindowWin32.h"
#include "platform/WindowsMediaSession.h"

#include <QQuickWindow>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>
#include <atomic>
#include <memory>

namespace {
template<typename T>
T awaitOperation(const winrt::Windows::Foundation::IAsyncOperation<T> &operation)
{
    auto done = std::make_shared<std::atomic_bool>(false);
    operation.Completed([done](const auto &, auto) { done->store(true); });
    QElapsedTimer timer; timer.start();
    while (!done->load() && timer.elapsed() < 5000) QTest::qWait(10);
    if (!done->load()) throw winrt::hresult_error(HRESULT_FROM_WIN32(WAIT_TIMEOUT));
    return operation.GetResults();
}
}

class TestWindowsDesktop final : public QObject {
    Q_OBJECT
private slots:
    void windowedSurfaceAvoidsMonitorSize_data()
    {
        QTest::addColumn<QRect>("display");
        QList<QRect> displays;
        EnumDisplayMonitors(nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
                MONITORINFO info{};
                info.cbSize = sizeof(info);
                if (GetMonitorInfoW(monitor, &info)) {
                    const RECT r = info.rcMonitor;
                    reinterpret_cast<QList<QRect> *>(data)->append(
                        QRect(r.left, r.top, r.right-r.left, r.bottom-r.top));
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&displays));
        for (qsizetype i = 0; i < displays.size(); ++i)
            QTest::newRow(qPrintable(QStringLiteral("monitor-%1").arg(i))) << displays.at(i);
    }
    void windowedSurfaceAvoidsMonitorSize()
    {
        QFETCH(QRect, display);
        QQuickWindow window;
        NativeWindowWin32 native;
        window.setFlags(Qt::Window | Qt::FramelessWindowHint);
        window.setGeometry(100, 100, 640, 360);
        native.attach(&window);
        window.show();
        const HWND hwnd = reinterpret_cast<HWND>(window.winId());
        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        // Use physical pixels, including under QT_SCALE_FACTOR. A video-sized
        // normal window must not become a display-sized OpenGL surface.
        QVERIFY(SetWindowPos(hwnd, nullptr, display.x()+70, display.y()+90,
                             display.width(), display.height(), SWP_NOZORDER|SWP_NOACTIVATE));
        RECT actual{};
        QVERIFY(GetWindowRect(hwnd, &actual));
        QCOMPARE(actual.left, display.x()+70);
        QCOMPARE(actual.top, display.y()+90);
        QCOMPARE(actual.right-actual.left, display.width());
        QCOMPARE(actual.bottom-actual.top, display.height()+1);
        QVERIFY(!IsZoomed(hwnd));
        QCOMPARE(window.windowStates(), Qt::WindowStates(Qt::WindowNoState));
        QCOMPARE(GetWindowLongPtrW(hwnd, GWL_EXSTYLE), style);
        // Moves must not accumulate extra rows or change/recreate the window.
        for (int step = 0; step < 4; ++step) {
            QVERIFY(SetWindowPos(hwnd, nullptr, display.x()-300+step*150, display.y()+40,
                                 0, 0, SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE));
            QVERIFY(GetWindowRect(hwnd, &actual));
            QCOMPARE(actual.bottom-actual.top, display.height()+1);
            QCOMPARE(reinterpret_cast<HWND>(window.winId()), hwnd);
            QVERIFY(!IsZoomed(hwnd));
        }
        // Ordinary nonmatching sizes are left exact.
        QVERIFY(SetWindowPos(hwnd, nullptr, 100, 100, 731, 419, SWP_NOZORDER|SWP_NOACTIVATE));
        QVERIFY(GetWindowRect(hwnd, &actual));
        QCOMPARE(actual.right-actual.left, 731L);
        QCOMPARE(actual.bottom-actual.top, 419L);
    }
    void topmostSurvivesFullscreenAndRestore()
    {
        QQuickWindow window;
        NativeWindowWin32 native;
        window.setFlags(Qt::Window | Qt::FramelessWindowHint);
        window.setGeometry(100, 100, 640, 360);
        native.attach(&window);
        window.show();
        const HWND hwnd = reinterpret_cast<HWND>(window.winId());
        const auto topmost = [&] { return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0; };
        QSignalSpy changed(&native, &INativeWindow::alwaysOnTopChanged);
        QVERIFY(!topmost());
        native.setAlwaysOnTop(true);
        QVERIFY(topmost());
        native.setAlwaysOnTop(true);
        QCOMPARE(changed.count(), 1);
        window.showMaximized();
        QTRY_VERIFY(window.windowStates().testFlag(Qt::WindowMaximized));
        QVERIFY(topmost());
        window.showNormal();
        QTRY_VERIFY(!window.windowStates().testFlag(Qt::WindowMaximized));
        QVERIFY(topmost());
        native.setFullscreenWindow(true, 100, 100, 640, 360);
        native.setAlwaysOnTop(false);
        QVERIFY(!native.alwaysOnTop());
        QVERIFY(topmost()); // Temporary fullscreen placement remains.
        native.setFullscreenWindow(false, 100, 100, 640, 360);
        QVERIFY(!topmost());
        native.setAlwaysOnTop(true);
        native.setFullscreenWindow(true, 100, 100, 640, 360);
        native.setFullscreenWindow(false, 100, 100, 640, 360);
        QVERIFY(topmost());
        QCOMPARE(reinterpret_cast<HWND>(window.winId()), hwnd);
        QVERIFY(window.flags().testFlag(Qt::FramelessWindowHint));
        const QPoint clientPoint = window.mapToGlobal(QPoint(1, window.height() / 2));
        const LPARAM point = MAKELPARAM(clientPoint.x(), clientPoint.y());
        QCOMPARE(SendMessageW(hwnd, WM_NCHITTEST, 0, point), LRESULT(HTLEFT));
        native.toggleAlwaysOnTop();
        QVERIFY(!topmost());
    }
    void smtcInitializesAndPublishes()
    {
        QQuickWindow window;
        window.show();
        WindowsMediaSession session(&window);
        QVERIFY2(session.available(), qPrintable(session.unavailableReason()));
        MediaSessionState state;
        state.hasMedia = true;
        state.canPlay = true;
        state.canPause = true;
        state.canSeek = true;
        state.title = QStringLiteral("Frame desktop lane SMTC test");
        state.trackId = QStringLiteral("/org/mpris/MediaPlayer2/track/t1");
        state.durationUs = 120000000;
        state.positionUs = 30000000;
        state.status = MediaSessionState::Status::Playing;
        session.publish(state);
        QVERIFY2(session.available(), qPrintable(session.unavailableReason()));
        state.status = MediaSessionState::Status::Paused;
        session.publish(state);
        QVERIFY2(session.available(), qPrintable(session.unavailableReason()));
        state.status = MediaSessionState::Status::Stopped;
        state.canPause = false;
        session.publish(state);
        QVERIFY2(session.available(), qPrintable(session.unavailableReason()));
        session.publish({});
        QVERIFY2(session.available(), qPrintable(session.unavailableReason()));
    }
    void destroyedWindowIsSafe()
    {
        NativeWindowWin32 native;
        auto *window = new QQuickWindow;
        native.attach(window);
        delete window;
        native.toggleAlwaysOnTop();
        QVERIFY(native.alwaysOnTop());
        native.raiseToFront();
    }
    void desktopSessionReadsMetadataAndControlsTransport()
    {
        using namespace winrt::Windows::Media::Control;
        QQuickWindow window;
        window.setTitle(QStringLiteral("Frame SMTC protocol test"));
        window.show();
        WindowsMediaSession session(&window);
        QVERIFY2(session.available(), qPrintable(session.unavailableReason()));
        MediaSessionState state;
        state.hasMedia = true; state.hasVideo = true;
        state.canPlay = true; state.canPause = true; state.canSeek = true;
        state.canNext = true; state.canPrevious = true;
        state.title = QStringLiteral("Frame desktop lane external session check");
        state.trackId = QStringLiteral("/org/mpris/MediaPlayer2/track/t99");
        state.durationUs = 120000000; state.positionUs = 30000000;
        state.status = MediaSessionState::Status::Playing;
        session.publish(state);
        QVERIFY2(session.available(), qPrintable(session.unavailableReason()));
        try {
            const auto manager = awaitOperation(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
            GlobalSystemMediaTransportControlsSession remote{nullptr};
            QElapsedTimer timer; timer.start();
            while (!remote && timer.elapsed() < 5000) {
                for (const auto &candidate : manager.GetSessions()) {
                    const auto props = awaitOperation(candidate.TryGetMediaPropertiesAsync());
                    if (QString::fromWCharArray(props.Title().c_str()) == state.title) { remote = candidate; break; }
                }
                if (!remote) QTest::qWait(50);
            }
            QVERIFY2(remote != nullptr, "Windows session manager did not expose the test SMTC metadata");
            QCOMPARE(remote.GetPlaybackInfo().PlaybackStatus(), GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
            QCOMPARE(remote.GetTimelineProperties().EndTime().count(), 1200000000LL);
            QSignalSpy commands(&session, &MediaSession::commandRequested);
            QSignalSpy seeks(&session, &MediaSession::seekRequested);
            QVERIFY(awaitOperation(remote.TryPauseAsync()));
            QTRY_COMPARE(commands.count(), 1);
            QCOMPARE(qvariant_cast<MediaSession::Command>(commands.last().first()), MediaSession::Command::Pause);
            state.status = MediaSessionState::Status::Paused;
            session.publish(state);
            QVERIFY(awaitOperation(remote.TryPlayAsync()));
            QTRY_COMPARE(commands.count(), 2);
            QCOMPARE(qvariant_cast<MediaSession::Command>(commands.last().first()), MediaSession::Command::Play);
            QVERIFY(awaitOperation(remote.TrySkipNextAsync()));
            QTRY_COMPARE(commands.count(), 3);
            QCOMPARE(qvariant_cast<MediaSession::Command>(commands.last().first()), MediaSession::Command::Next);
            QVERIFY(awaitOperation(remote.TrySkipPreviousAsync()));
            QTRY_COMPARE(commands.count(), 4);
            QCOMPARE(qvariant_cast<MediaSession::Command>(commands.last().first()), MediaSession::Command::Previous);
            QVERIFY(awaitOperation(remote.TryChangePlaybackPositionAsync(450000000LL)));
            QTRY_COMPARE(seeks.count(), 1);
            QCOMPARE(seeks.first().first().toDouble(), 45.0);
            state.status = MediaSessionState::Status::Stopped;
            state.canPause = false;
            session.publish(state);
            QVERIFY(awaitOperation(remote.TryPlayAsync()));
            QTRY_COMPARE(commands.count(), 5); // EOF replay requires an explicit Play.
            session.publish({});
        } catch (const winrt::hresult_error &error) {
            QFAIL(qPrintable(QString::fromWCharArray(error.message().c_str())));
        }
    }
};
QTEST_MAIN(TestWindowsDesktop)
#include "test_nativedesktop_win.moc"
