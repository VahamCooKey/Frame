#include <QtTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include "core/PlayerController.h"
#include "input/ShortcutManager.h"

class TestPlaybackRequests : public QObject {
    Q_OBJECT
    const QString video = QFileInfo("assets/test/sample.mkv").absoluteFilePath();
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no;sub-auto=no");
    }
    void rapidPauseToggles_data() {
        QTest::addColumn<bool>("initial");
        QTest::addColumn<int>("count");
        QTest::newRow("playing-double") << false << 2;
        QTest::newRow("paused-double") << true << 2;
        QTest::newRow("playing-odd") << false << 31;
        QTest::newRow("paused-even") << true << 30;
    }
    void rapidPauseToggles() {
        QFETCH(bool, initial);
        QFETCH(int, count);
        PlayerController player;
        player.openFile(video);
        QTRY_COMPARE(player.currentFile(), video);
        player.setPaused(initial);
        QTest::qWait(100);
        QTRY_COMPARE(player.isPaused(), initial);
        QSignalSpy actions(&player, &PlayerController::playbackActionRequested);
        for (int i = 0; i < count; ++i) player.togglePause();
        QCOMPARE(actions.size(), count);
        for (int i = 0; i < count; ++i)
            QCOMPARE(actions.at(i).at(0).toBool(), initial != ((i + 1) % 2 != 0));
        const bool expected = initial != (count % 2 != 0);
        // An even burst ends at the initial value. A live backend getter in
        // QTRY_COMPARE can match before the first write, then see that write
        // on the macro's next evaluation. Require both observable states to
        // agree continuously while the serialized writes settle instead.
        QElapsedTimer settled;
        QVERIFY(QTest::qWaitFor([&] {
            const QVariant actual = player.mpvProperty("pause");
            if (!actual.isValid() || actual.toBool() != expected || player.isPaused() != expected) {
                settled.invalidate();
                return false;
            }
            if (!settled.isValid()) settled.start();
            return settled.elapsed() >= 150;
        }, 3000));
        QCOMPARE(player.isPaused(), expected);
        player.openFile(video);
        QTest::qWait(150);
        QTRY_COMPARE(player.isPaused(), expected);
    }
    void holdAndNativePause() {
        PlayerController player;
        player.openFile(video);
        QTRY_VERIFY(player.position() > .1);
        player.setInteractionPause(true);
        QTRY_VERIFY(player.isPaused());
        // Toggle changes the user's underlying play intent, not the hold.
        player.togglePause();
        player.setInteractionPause(false);
        QTest::qWait(150);
        QVERIFY(player.isPaused());
        player.command({"keypress", "SPACE"});
        QTRY_VERIFY(!player.isPaused());
        player.togglePause();
        player.togglePause();
        QTest::qWait(150);
        QVERIFY(!player.isPaused());
        player.setInteractionPause(true);
        player.setInteractionPause(false); // release before the hold write replies
        QTest::qWait(150);
        QVERIFY(!player.isPaused());
        player.play();
        player.command({"frame-step"}); // newer frame-step beats pending Play
        QTRY_VERIFY(player.isPaused());
        QTest::qWait(150);
        QVERIFY(player.isPaused());
        for (int i = 0; i < 20; ++i) player.command({"frame-step"});
        player.play(); // Play must run after every already requested step.
        QTRY_VERIFY(!player.isPaused());
        QTest::qWait(150);
        QVERIFY(!player.isPaused());
    }
    void pauseWriteBeforePresentationHook() {
        PlayerController player;
        player.requireVideoPresentation();
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        for (int i = 0; i < 6; ++i) {
            player.pause();
            player.play();
            player.openFile(video);
            QTRY_COMPARE(loaded.size(), i + 1);
            QTRY_VERIFY(player.presentationFrameReady());
            QTest::qWait(100);
            QVERIFY(player.isPaused());
            QVERIFY(player.position() < .08);
            player.videoFramePresented(player.presentationGeneration() - 1);
            QVERIFY(!player.videoPresented());
            player.videoFramePresented(player.presentationGeneration());
            QTRY_VERIFY(!player.isPaused());
        }
    }
    void seekTargetSurvivesReleaseAndLatestWins() {
        PlayerController player;
        player.openFile(video);
        QTRY_VERIFY(player.duration() > 2);
        player.pause();
        QTRY_VERIFY(player.isPaused());
        player.setScrubbing(true);
        player.seek(player.duration() * .6);
        const double target = player.seekTarget();
        player.setScrubbing(false);
        QVERIFY(player.seekPending());
        QCOMPARE(player.seekTarget(), target);
        QTRY_VERIFY_WITH_TIMEOUT(!player.seekPending(), 2500);
        QVERIFY(qAbs(player.position() - target) < .25);
        player.seek(player.duration() * .2);
        player.seek(player.duration() * .7);
        player.seek(player.duration() * .4);
        const double latest = player.seekTarget();
        QTRY_VERIFY_WITH_TIMEOUT(!player.seekPending(), 2500);
        QVERIFY(qAbs(player.position() - latest) < .25);
        player.seekRelative(.25);
        player.seekRelative(.25);
        QTRY_VERIFY_WITH_TIMEOUT(!player.seekPending(), 2500);
        QVERIFY(qAbs(player.position() - latest - .5) < .25);
    }
    void scrubCancelTracksBackendAndStopClearsSeek() {
        PlayerController player;
        player.openFile(video);
        QTRY_VERIFY(player.position() > .1);
        player.setScrubbing(true);
        const double before = player.position();
        QTRY_VERIFY(player.position() > before + .15);
        player.setScrubbing(false);
        QVERIFY(!player.seekPending());
        player.seek(player.duration() * .5);
        QVERIFY(player.seekPending());
        player.stop();
        QVERIFY(!player.seekPending());
        QTRY_COMPARE(player.state(), PlayerController::State::Idle);
        player.openFile(video);
        QTRY_COMPARE(player.currentFile(), video);
        player.seek(player.duration() * .5);
        player.openFile(video);
        QVERIFY(!player.seekPending());
        player.prepareForShutdown();
        player.seek(1);
        QVERIFY(!player.seekPending());
        QVERIFY(!player.screenshotToClipboard());
    }
    void smallSeekAndFastPlaybackSettle() {
        PlayerController player;
        player.openFile(video);
        QTRY_VERIFY(player.duration() > 8);
        player.pause();
        QTRY_VERIFY(player.isPaused());
        player.seek(1);
        QTRY_VERIFY_WITH_TIMEOUT(!player.seekPending(), 2000);
        const double before = player.position();
        player.seek(before + .1);
        QVERIFY(player.seekPending());
        QTRY_VERIFY_WITH_TIMEOUT(!player.seekPending(), 2000);
        QVERIFY(player.position() > before + .05);
        player.setSpeed(8);
        QTRY_COMPARE(player.speed(), 8.0);
        player.play();
        QTRY_VERIFY(!player.isPaused());
        player.seek(4);
        QTRY_VERIFY_WITH_TIMEOUT(!player.seekPending(), 2000);
        QVERIFY(player.position() >= 3.95);
        player.pause();
    }
};

QTEST_MAIN(TestPlaybackRequests)
#include "test_playback_requests.moc"
