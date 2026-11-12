#include <QtTest>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QDataStream>
#include <QRegularExpression>
#include <QScopeGuard>
#include "core/GraphicsBootstrap.h"
#include "core/MpvRenderItem.h"

class TestStartupPresentation : public QObject {
    Q_OBJECT
private slots:
    void graphicsBootstrapImmediateTeardown()
    {
#if defined(Q_OS_WIN) && defined(QT_OPENGL_DYNAMIC)
        if (qgetenv("QT_OPENGL").toLower() == "software")
            QSKIP("Explicit software rendering skips the optional bootstrap");
        const auto perf = qgetenv("FRAME_PERF");
        const auto disabled = qgetenv("FRAME_NO_GRAPHICS_BOOTSTRAP");
        const auto restore = qScopeGuard([&] {
            if (perf.isNull()) qunsetenv("FRAME_PERF"); else qputenv("FRAME_PERF", perf);
            if (disabled.isNull()) qunsetenv("FRAME_NO_GRAPHICS_BOOTSTRAP");
            else qputenv("FRAME_NO_GRAPHICS_BOOTSTRAP", disabled);
        });
        qputenv("FRAME_PERF", "1");
        qunsetenv("FRAME_NO_GRAPHICS_BOOTSTRAP");
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("^perf: graphics-bootstrap module=0 ms=\\d+$"));
        {
            GraphicsBootstrap bootstrap;
            // Immediate teardown must join initialization before Qt's
            // platform integration can be destroyed by an early exit.
        }
        // The remaining tests create real Qt Quick/mpv contexts after this
        // helper is gone, covering ownership and renderer reuse as well.
#else
        QSKIP("Windows dynamic OpenGL optimization only");
#endif
    }

    void startupSeek_data() {
        QTest::addColumn<bool>("video");
        QTest::addColumn<bool>("userPaused");
        QTest::newRow("video-play") << true << false;
        QTest::newRow("video-pause") << true << true;
        QTest::newRow("audio-play") << false << false;
        QTest::newRow("audio-pause") << false << true;
    }
    void startupSeek() {
        QFETCH(bool, video);
        QFETCH(bool, userPaused);
        PlayerController player;
        QQuickWindow window;
        window.resize(640, 360);
        auto *item = new MpvRenderItem(window.contentItem());
        item->setSize(QSizeF(640, 360));
        item->setPlayer(&player);
        window.show();
        QTimer::singleShot(50, &window, [&] { window.hide(); window.show(); });
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(item->renderReady(), 5000);
        bool loaded = false, earlyUnpause = false;
        connect(&player, &PlayerController::fileLoaded, &player, [&] {
            loaded = true;
            player.seek(0.5); // Same path as resume and CLI --start/--start-frame.
        });
        connect(&player, &PlayerController::pauseChanged, &player, [&](bool paused) {
            if (loaded && !paused && (player.seekPending() || player.position() < .45))
                earlyUnpause = true;
        });
        if (userPaused) player.pause();
        player.openFile(QFileInfo(video ? "assets/test/sample.mkv" : "assets/test/tone.wav").absoluteFilePath());
        QTRY_VERIFY_WITH_TIMEOUT(loaded && !player.seekPending() && player.position() >= .45, 5000);
        if (video) QTRY_VERIFY(player.videoPresented());
        if (userPaused) {
            QTest::qWait(200);
            QVERIFY(player.isPaused());
            QVERIFY(player.position() < .65);
        } else {
            QTRY_VERIFY(!player.isPaused() && player.position() > .7);
        }
        QVERIFY(!earlyUnpause);
        player.stop();
        QTRY_COMPARE(player.state(), PlayerController::State::Idle);
        window.hide();
    }
    void delayedPresentation_data() {
        QTest::addColumn<bool>("userPaused");
        QTest::newRow("play-after-presentation") << false;
        QTest::newRow("keep-user-pause") << true;
    }
    void delayedPresentation() {
        QFETCH(bool, userPaused);
        PlayerController player;
        QQuickWindow window;
        window.resize(640, 360);
        auto *item = new MpvRenderItem(window.contentItem());
        item->setSize(QSizeF(640, 360));
        item->setPlayer(&player);
        window.show();
        QTimer::singleShot(50, &window, [&] { window.hide(); window.show(); });
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(item->renderReady(), 5000);
        QTest::qWait(100); // Finish the initial Windows exposure retry first.
        // Simulate a slow/unavailable display after the GL context exists.
        // Previously mpv's audio clock advanced throughout this interval.
        window.hide();
        QTRY_VERIFY(!window.isExposed());
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        const auto media = QFileInfo("assets/test/sample.mkv").absoluteFilePath();
        player.openFile(media);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(player.isPaused(), 2000);
        QTest::qWait(1500);
        QVERIFY2(player.position() < .08, "Playback advanced before any video frame could be presented");
        QVERIFY(!item->hasRenderedVideo());
        QVERIFY(!player.videoPresented());
        if (userPaused) player.pause();
        const auto oldGeneration = player.presentationGeneration();
        // Reopening also exercises per-file reset and stale swap rejection.
        player.openFile(media);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 5000);
        QVERIFY(player.presentationGeneration() > oldGeneration);
        player.videoFramePresented(oldGeneration);
        QTest::qWait(100);
        QVERIFY(player.isPaused());
        QVERIFY(!player.videoPresented());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(item->hasRenderedVideo(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(player.videoPresented(), 2000);
        if (userPaused) {
            QTest::qWait(350);
            QVERIFY(player.isPaused());
            QVERIFY(player.position() < .08);
        } else {
            QTRY_VERIFY_WITH_TIMEOUT(!player.isPaused() && player.position() > .1, 4000);
            player.command({"keypress", "SPACE"});
            QTRY_VERIFY(player.isPaused());
            player.openFile(media);
            QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 3, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(item->hasRenderedVideo(), 5000);
            QTest::qWait(200);
            QVERIFY2(player.isPaused(), "Native keyboard pause was lost when opening another video");
        }
        player.stop();
        QTRY_COMPARE(player.state(), PlayerController::State::Idle);
        window.hide();
    }
    void guidePause_data() {
        QTest::addColumn<QString>("action");
        QTest::newRow("resume-on-close") << QString();
        QTest::newRow("explicit-pause-wins") << QStringLiteral("pause");
        QTest::newRow("explicit-play-wins") << QStringLiteral("play");
        QTest::newRow("preserve-initial-pause") << QStringLiteral("initial-pause");
    }
    void guidePause() {
        QFETCH(QString, action);
        PlayerController player;
        QQuickWindow window;
        window.resize(640, 360);
        auto *item = new MpvRenderItem(window.contentItem());
        item->setSize(QSizeF(640, 360));
        item->setPlayer(&player);
        window.show();
        QTimer::singleShot(50, &window, [&] { window.hide(); window.show(); });
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(item->renderReady(), 5000);
        if (action == QStringLiteral("initial-pause")) player.setPaused(true);
        player.setInteractionPause(true);
        player.openFile(QFileInfo("assets/test/sample.mkv").absoluteFilePath());
        QTRY_VERIFY_WITH_TIMEOUT(item->hasRenderedVideo(), 5000);
        QTest::qWait(250);
        QVERIFY(player.isPaused());
        QVERIFY(player.position() < .08);
        if (action == QStringLiteral("pause")) player.pause();
        if (action == QStringLiteral("play")) player.play();
        player.setInteractionPause(false);
        if (action == QStringLiteral("pause") || action == QStringLiteral("initial-pause")) {
            QTest::qWait(250);
            QVERIFY(player.isPaused());
            QVERIFY(player.position() < .08);
        } else {
            QTRY_VERIFY_WITH_TIMEOUT(!player.isPaused() && player.position() > .1, 4000);
        }
        player.stop();
        QTRY_COMPARE(player.state(), PlayerController::State::Idle);
        window.hide();
    }
    void audioOnlyDoesNotWaitForVideo() {
        QTemporaryFile wave;
        QVERIFY(wave.open());
        const QByteArray samples(48000 * 2 * 3, '\0');
        QDataStream data(&wave);
        data.setByteOrder(QDataStream::LittleEndian);
        data.writeRawData("RIFF", 4); data << quint32(36 + samples.size());
        data.writeRawData("WAVEfmt ", 8); data << quint32(16) << quint16(1) << quint16(1)
            << quint32(48000) << quint32(96000) << quint16(2) << quint16(16);
        data.writeRawData("data", 4); data << quint32(samples.size());
        data.writeRawData(samples.constData(), samples.size()); wave.flush();
        PlayerController player;
        player.requireVideoPresentation();
        player.openFile(wave.fileName());
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > .1, 5000);
        QVERIFY(!player.isPaused());
    }
    void selectionAfterPauseStartsVideo() {
        PlayerController player;
        QQuickWindow window;
        window.resize(640, 360);
        auto *item = new MpvRenderItem(window.contentItem());
        item->setSize(QSizeF(640, 360));
        item->setPlayer(&player);
        window.show();
        QTimer::singleShot(50, &window, [&] { window.hide(); window.show(); });
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(item->renderReady(), 5000);
        const auto video = QFileInfo("assets/test/sample.mkv").absoluteFilePath();
        const auto audio = QFileInfo("assets/test/tone.wav").absoluteFilePath();
        player.playlistAppendAndPlay({video, audio});
        QTRY_VERIFY(player.playlist().size() == 2 && player.position() > .05);
        for (int attempt = 0; attempt < 8; ++attempt) {
            player.playlistPlayIndex(1);
            QTRY_VERIFY(player.currentFile() == audio && !player.isPaused() && player.position() > .05);
            player.pause();
            // Deliberately do not drain the pause notification before selecting.
            player.playlistPrev();
            QTRY_VERIFY_WITH_TIMEOUT(player.currentFile() == video && player.playlistPos() == 0
                                     && !player.isPaused() && player.position() > .05, 5000);
        }
        player.playlistPlayIndex(1);
        QTRY_VERIFY(player.currentFile() == audio && !player.isPaused());
        player.playlistPrev();
        player.pause(); // A newer explicit pause still wins over selection.
        QTRY_VERIFY(player.currentFile() == video && item->hasRenderedVideo());
        QTest::qWait(250);
        QVERIFY(player.isPaused());
        player.stop();
        QTRY_COMPARE(player.state(), PlayerController::State::Idle);
        window.hide();
    }
    void playlistTransitionsAndReplay() {
        PlayerController player;
        QQuickWindow window;
        window.resize(640, 360);
        auto *item = new MpvRenderItem(window.contentItem());
        item->setSize(QSizeF(640, 360));
        item->setPlayer(&player);
        window.show();
        QTimer::singleShot(50, &window, [&] { window.hide(); window.show(); });
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(item->renderReady(), 5000);
        QTest::qWait(100);
        const auto video = QFileInfo("assets/test/sample.mkv").absoluteFilePath();
        const auto audio = QFileInfo("assets/test/tone.wav").absoluteFilePath();
        player.playlistAppendAndPlay({video, audio, video});
        QTRY_VERIFY_WITH_TIMEOUT(item->hasRenderedVideo() && player.position() > .05, 5000);
        const auto generation = player.presentationGeneration();
        player.seek(player.duration() - .15);
        QTRY_VERIFY_WITH_TIMEOUT(player.playlistPos() == 1 && player.currentFile() == audio
                                 && !player.isPaused() && player.position() > .05, 5000);
        player.seek(player.duration() - .15);
        QTRY_VERIFY_WITH_TIMEOUT(player.playlistPos() == 2 && player.currentFile() == video
                                 && item->hasRenderedVideo() && !player.isPaused()
                                 && player.position() > .05, 5000);
        QVERIFY(player.presentationGeneration() > generation);
        player.seek(player.duration() - .15);
        QTRY_VERIFY_WITH_TIMEOUT(player.eofReached() && player.isPaused(), 5000);
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(!player.eofReached() && !player.isPaused()
                                 && player.position() > .05 && player.position() < 2, 5000);
        QCOMPARE(player.playlistPos(), 2);
        player.stop();
        QTRY_COMPARE(player.state(), PlayerController::State::Idle);
        window.hide();
    }
};

int main(int argc, char **argv) {
    QStandardPaths::setTestModeEnabled(true);
    qputenv("FRAME_MPV_OPTS", "ao=null");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    TestStartupPresentation test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_startup_presentation.moc"
