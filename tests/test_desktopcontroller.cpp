// Real mpv/controller transition coverage. Linux uses a private session bus;
// Windows requires an interactive SMTC session.
#include "core/PlayerController.h"
#include "platform/SystemMediaControls.h"
#include "platform/MediaSession.h"
#include "SeekWhenReady.h"

#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>
#include <algorithm>
#include <cmath>

class TestDesktopController final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no;pause=yes");
    }
    void transportFollowsRealControllerTransitions()
    {
        PlayerController player;
        QQuickWindow window;
        window.show();
        auto *controls = SystemMediaControls::create(&player, &window);
        QVERIFY2(controls->available(), qPrintable(controls->unavailableReason()));
        auto *session = controls->findChild<MediaSession *>();
        QVERIFY(session);
        QVERIFY(!session->state().hasMedia);
        session->request(MediaSession::Command::Play);
        QTest::qWait(20);
        QCOMPARE(player.state(), PlayerController::State::Idle);
        const QString file = QStringLiteral(FRAME_TEST_SOURCE_DIR "/assets/test/sample.mkv");
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        player.openFile(file);
        player.pause();
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 6000);
        QTRY_COMPARE_WITH_TIMEOUT(player.state(), PlayerController::State::Paused, 3000);
        QTRY_VERIFY(session->state().hasMedia);
        QCOMPARE(session->state().status, MediaSessionState::Status::Paused);
        QVERIFY(session->state().canPlay);
        // Duration is observed separately from FILE_LOADED and may arrive in
        // a later event batch. Seek capability becomes available with it.
        QTRY_VERIFY(session->state().canSeek);
        QVERIFY(!session->state().title.isEmpty());
        const QString firstId = session->state().trackId;
        player.playlistAppendFiles({file}, false);
        QTRY_COMPARE(player.playlist().size(), 2);
        QTRY_VERIFY(session->state().canNext);
        session->request(MediaSession::Command::Next);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 6000);
        QTRY_COMPARE(player.playlistPos(), 1);
        QTRY_COMPARE(player.state(), PlayerController::State::Paused);
        QCOMPARE(session->state().status, MediaSessionState::Status::Paused);
        QVERIFY(session->state().trackId != firstId);
        QTRY_VERIFY(session->state().canSeek);
        const double previousPosition = player.position();
        session->requestSeek(1000000, firstId);
        QTest::qWait(30);
        QVERIFY(std::abs(player.position() - previousPosition) < 0.1);
        session->requestSeek(500000, session->state().trackId);
        QTRY_VERIFY(std::abs(player.position() - 0.5) < 0.15);
        QCOMPARE(player.isPaused(), true);
        session->request(MediaSession::Command::Play);
        QTRY_COMPARE(player.state(), PlayerController::State::Playing);
        QTRY_COMPARE(session->state().status, MediaSessionState::Status::Playing);
        session->request(MediaSession::Command::Pause);
        QTRY_COMPARE(player.state(), PlayerController::State::Paused);
        session->request(MediaSession::Command::Stop);
        QTRY_COMPARE(player.state(), PlayerController::State::Idle);
        QTRY_VERIFY(!session->state().hasMedia);
        session->request(MediaSession::Command::Play);
        QTest::qWait(30);
        QCOMPARE(player.state(), PlayerController::State::Idle);
        controls->shutdown();
        controls->shutdown();
        QVERIFY(!controls->available());
    }
    void eofReportsStoppedAndExplicitPlayReplays()
    {
        PlayerController player;
        QQuickWindow window;
        window.show();
        auto *controls = SystemMediaControls::create(&player, &window);
        auto *session = controls->findChild<MediaSession *>();
        QVERIFY(session);
        player.openFile(QStringLiteral(FRAME_TEST_SOURCE_DIR "/assets/test/sample.mkv"));
        player.pause();
        QTRY_VERIFY_WITH_TIMEOUT(player.duration() > 0, 6000);
        QTRY_VERIFY(session->state().hasMedia);
        QVERIFY(seekWhenReady(player, std::max(0.0, player.duration() - 0.15)));
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.eofReached(), 6000);
        QTRY_COMPARE(session->state().status, MediaSessionState::Status::Stopped);
        QVERIFY(session->state().canPlay);
        const QString track = session->state().trackId;
        session->request(MediaSession::Command::Play);
        QTRY_VERIFY_WITH_TIMEOUT(!player.eofReached(), 3000);
        QTRY_COMPARE(session->state().status, MediaSessionState::Status::Playing);
        QCOMPARE(session->state().trackId, track);
        player.pause();
        controls->shutdown();
    }
    void destructionOrderIsSafe()
    {
        QQuickWindow window;
        auto *player = new PlayerController;
        auto *controls = SystemMediaControls::create(player, &window);
        delete player;
        QVERIFY(!controls->available());
        controls->shutdown();
    }
};
QTEST_MAIN(TestDesktopController)
#include "test_desktopcontroller.moc"
