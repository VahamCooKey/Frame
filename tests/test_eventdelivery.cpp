#include <QtTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <thread>
#include <vector>
#include "core/PlayerController.h"

namespace {
bool sendMessage(PlayerController &player, const QByteArray &name)
{
    const char *args[] = {"script-message", name.constData(), nullptr};
    return mpv_command(player.handle(), args) >= 0;
}
}

class TestEventDelivery : public QObject {
    Q_OBJECT
    static auto seedMetadata(PlayerController &player, quint64 firstId)
    {
        player.invalidateFileMetadata();
        player.m_fileMetadataPending = true;
        player.m_presentationFileLoaded = true;
        for (int i = 0; i < PlayerController::MetadataCount; ++i)
            player.m_fileMetadataReplies[i] = firstId + i;
        return std::array<QVariant, PlayerController::MetadataCount>{
            QVariantList{QVariantMap{{"id", 1}, {"type", "audio"}, {"selected", true}, {"lang", "eng"}}},
            true, QFileInfo("assets/test/sample.mkv").absoluteFilePath(), 1, true, 30.0};
    }
private slots:
    void subtitleRequestsOverlapAndIgnoreStaleCompletion()
    {
        PlayerController player;
        QSignalSpy failed(&player, &PlayerController::subtitleLoadingFailed);
        player.beginSubtitleRequest(90001);
        player.beginSubtitleRequest(90002);
        QVERIFY(player.subtitleLoading());
        player.finishSubtitleRequest(90001, true);
        QVERIFY(player.subtitleLoading());
        player.finishSubtitleRequest(90002, false);
        QVERIFY(!player.subtitleLoading());
        QCOMPARE(failed.count(), 1);
        player.beginSubtitleRequest(90003);
        player.clearSubtitleRequests();
        player.beginSubtitleRequest(90004);
        player.finishSubtitleRequest(90003, false);
        QVERIFY(player.subtitleLoading());
        QCOMPARE(failed.count(), 1);
        player.prepareForShutdown();
        QVERIFY(!player.subtitleLoading());
    }

    void subtitleLoadCompletesAsynchronously()
    {
        QTemporaryDir directory;
        QFile subtitle(directory.filePath("sample.srt"));
        QVERIFY(subtitle.open(QIODevice::WriteOnly));
        subtitle.write("1\n00:00:00,000 --> 00:00:09,000\nSubtitle loading test\n");
        subtitle.close();
        PlayerController player;
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        QSignalSpy failed(&player, &PlayerController::subtitleLoadingFailed);
        player.openFile(QFileInfo("assets/test/sample.mkv").absoluteFilePath());
        QTRY_COMPARE(loaded.count(), 1);
        player.loadSubtitleFile(subtitle.fileName());
        QVERIFY(player.subtitleLoading());
        QTRY_COMPARE(player.m_subtitleFontProvider, QStringLiteral("auto"));
        QTRY_VERIFY(!player.subtitleLoading());
        QVERIFY(player.m_activeSubtitleId >= 0);
        QCOMPARE(failed.count(), 0);
        QVERIFY(player.m_subtitleRequests.isEmpty());
        player.loadSubtitleFile(directory.filePath("missing.srt"));
        QVERIFY(player.subtitleLoading());
        QTRY_VERIFY(!player.subtitleLoading());
        QTRY_COMPARE(failed.count(), 1);
        player.beginSubtitleRequest(90005);
        player.stop();
        QVERIFY(!player.subtitleLoading());
    }

    void embeddedSubtitleFontSetupKeepsTrackIdentity()
    {
        PlayerController player;
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        QSignalSpy failed(&player, &PlayerController::subtitleLoadingFailed);
        player.openFile(QFileInfo("assets/test/track-choice-a.mkv").absoluteFilePath());
        QTRY_COMPARE(loaded.count(), 1);
        int id = -1;
        for (const auto &entry : player.trackList()) {
            const auto track = entry.toMap();
            if (track.value("type") == "sub") { id = track.value("id").toInt(); break; }
        }
        QVERIFY(id >= 0);
        player.setSubtitleTrack(id);
        QTRY_COMPARE(player.m_activeSubtitleId, id);
        QTRY_COMPARE(player.m_subtitleFontProvider, QStringLiteral("auto"));
        QTRY_VERIFY(!player.subtitleLoading());
        QCOMPARE(failed.count(), 0);
        QCOMPARE(player.mpvProperty("sid").toInt(), id);
        QVERIFY(player.m_subtitleRequests.isEmpty());
    }

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no;sub-auto=no");
        qputenv("FRAME_PERF", "1");
    }

    void metadataWaitsForAllRepliesAndPresentation()
    {
        PlayerController player;
        const auto values = seedMetadata(player, 100000);
        player.m_waitingForPresentation = true;
        player.m_presentationFrameReady = true;
        player.m_presentationGeneration = 10;
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        player.videoFramePresented(10);
        QVERIFY(player.m_videoPresented);
        QVERIFY(player.m_waitingForPresentation);
        for (int i = PlayerController::MetadataCount - 1; i >= 0; --i) {
            QCOMPARE(loaded.count(), 0);
            QVERIFY(player.acceptFileMetadataReply(100000 + i, values[i]));
        }
        QCOMPARE(loaded.count(), 1);
        QCOMPARE(player.currentFile(), values[PlayerController::Path].toString());
        QCOMPARE(player.containerFps(), 30.0);
        QVERIFY(player.isSeekable());
        QVERIFY(!player.m_waitingForPresentation);
        QVERIFY(player.m_haveInitialTracks);
        QVERIFY(!player.acceptFileMetadataReply(100000, values[0]));
    }

    void metadataRejectsStaleRepliesAndKeepsNewerTracks()
    {
        PlayerController player;
        const auto old = seedMetadata(player, 100000);
        QVERIFY(player.acceptFileMetadataReply(100000, old[0]));
        const auto fresh = seedMetadata(player, 200000);
        const QVariantList latest{QVariantMap{{"id", 2}, {"type", "audio"}, {"selected", true}, {"lang", "fra"}}};
        player.m_metadataTrackObserved = true;
        player.m_trackList = latest;
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        for (int i = 0; i < PlayerController::MetadataCount; ++i) {
            QVERIFY(!player.acceptFileMetadataReply(100000 + i, old[i]));
            QVERIFY(player.acceptFileMetadataReply(200000 + i, fresh[i]));
        }
        QCOMPARE(loaded.count(), 1);
        QCOMPARE(player.trackList(), latest);
        QCOMPARE(player.m_initialTracks, fresh[0].toList());
    }

    void metadataStartupSeekRequiresSettlement_data()
    {
        QTest::addColumn<bool>("video");
        QTest::newRow("video-fresh-swap") << true;
        QTest::newRow("audio-no-swap") << false;
    }

    void metadataStartupSeekRequiresSettlement()
    {
        QFETCH(bool, video);
        PlayerController player;
        auto values = seedMetadata(player, 100000);
        values[PlayerController::VideoTrack] = video ? QVariant(1) : QVariant();
        player.m_waitingForPresentation = true;
        player.m_presentationFrameReady = true;
        player.m_videoPresented = true; // Initial frame arrived before metadata.
        connect(&player, &PlayerController::fileLoaded, &player, [&] { player.seek(0.5); });
        for (int i = 0; i < PlayerController::MetadataCount; ++i)
            QVERIFY(player.acceptFileMetadataReply(100000 + i, values[i]));
        QVERIFY(player.m_seekPending);
        QVERIFY(player.m_waitingForPresentation);
        QVERIFY(!player.m_videoPresented);
        const auto duringSeek = player.presentationGeneration();
        player.videoFramePresented(duringSeek);
        QVERIFY(!player.m_videoPresented);
        player.clearPendingSeek(true); // Model the correlated seek settlement.
        if (video) {
            QVERIFY(player.m_waitingForPresentation);
            player.videoFramePresented(duringSeek);
            QVERIFY(player.m_waitingForPresentation);
            player.videoFramePresented(player.presentationGeneration());
        }
        QVERIFY(!player.m_waitingForPresentation);
    }

    void metadataErrorsSettleAndAudioDoesNotWaitForVideo()
    {
        PlayerController player;
        auto values = seedMetadata(player, 100000);
        values[PlayerController::Tracks] = QVariant();
        values[PlayerController::VideoTrack] = QVariant(); // Unavailable for audio-only.
        values[PlayerController::Fps] = QVariant();
        player.m_waitingForPresentation = true;
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        for (int i = 0; i < PlayerController::MetadataCount; ++i)
            QVERIFY(player.acceptFileMetadataReply(100000 + i, values[i]));
        QCOMPARE(loaded.count(), 1);
        QVERIFY(!player.m_haveInitialTracks);
        QVERIFY(!player.m_waitingForPresentation);
        QCOMPARE(player.containerFps(), 0.0);
    }

    void stopAndShutdownCancelMetadata()
    {
        PlayerController player;
        const auto values = seedMetadata(player, 100000);
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        player.stop();
        for (int i = 0; i < PlayerController::MetadataCount; ++i)
            QVERIFY(player.acceptFileMetadataReply(100000 + i, values[i]));
        QCOMPARE(loaded.count(), 0);
        QTRY_VERIFY(!player.m_fileMetadataPending);
        QCOMPARE(loaded.count(), 0);
        seedMetadata(player, 200000);
        player.prepareForShutdown();
        QVERIFY(!player.m_fileMetadataPending);
        QVERIFY(!player.acceptFileMetadataReply(200000, values[0]));
        QCOMPARE(loaded.count(), 0);
    }

    void navigationWithoutTransitionKeepsMetadata_data()
    {
        QTest::addColumn<QStringList>("command");
        QTest::newRow("successful-no-op") << QStringList{"playlist-play-index", "999"};
        QTest::newRow("rejected-property") << QStringList{"set", "playlist-pos", "not-a-number"};
    }

    void navigationWithoutTransitionKeepsMetadata()
    {
        QFETCH(QStringList, command);
        PlayerController player;
        const auto values = seedMetadata(player, 100000);
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        QVERIFY(player.command(command));
        for (int i = 0; i < PlayerController::MetadataCount; ++i)
            QVERIFY(player.acceptFileMetadataReply(100000 + i, values[i]));
        QCOMPARE(loaded.count(), 0);
        QTRY_COMPARE(loaded.count(), 1);
        QVERIFY(!player.m_fileMetadataPending);
    }

    void metadataSignalCanSupersedeCompletion()
    {
        PlayerController player;
        const auto values = seedMetadata(player, 100000);
        player.m_waitingForPresentation = true;
        player.m_videoPresented = true;
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        connect(&player, &PlayerController::fileLoaded, &player, [&] { player.stop(); });
        for (int i = 0; i < PlayerController::MetadataCount; ++i)
            QVERIFY(player.acceptFileMetadataReply(100000 + i, values[i]));
        QCOMPARE(loaded.count(), 1);
        QVERIFY(player.m_waitingForPresentation); // Old completion cannot unpause after Stop.
        QTRY_VERIFY(!player.m_fileMetadataPending);
        QCOMPARE(loaded.count(), 1);
    }

    void metadataPublishesOnceAfterSignalNavigation_data()
    {
        navigationWithoutTransitionKeepsMetadata_data();
    }

    void metadataPublishesOnceAfterSignalNavigation()
    {
        QFETCH(QStringList, command);
        PlayerController player;
        const auto values = seedMetadata(player, 100000);
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        connect(&player, &PlayerController::fileLoaded, &player, [&] { player.command(command); });
        for (int i = 0; i < PlayerController::MetadataCount; ++i)
            QVERIFY(player.acceptFileMetadataReply(100000 + i, values[i]));
        QCOMPARE(loaded.count(), 1);
        QTRY_VERIFY(!player.m_fileMetadataPending);
        QCOMPARE(loaded.count(), 1);
    }

    void nestedDrainPreservesDeliveryOrder()
    {
        PlayerController player;
        QTest::qWait(100);
        mpv_set_wakeup_callback(player.handle(), nullptr, nullptr);
        QVERIFY(sendMessage(player, "app-first"));
        QVERIFY(sendMessage(player, "app-second"));
        QStringList order;
        connect(&player, &PlayerController::appCommand, &player,
                [&](const QString &name, const QStringList &) {
            order.append(name + "-enter");
            if (name == "app-first") {
                // Model a direct slot entering a nested Qt event loop while
                // the outer consumer still owns mpv's borrowed event.
                QMetaObject::invokeMethod(&player, &PlayerController::processMpvEvents,
                                          Qt::QueuedConnection);
                QCoreApplication::sendPostedEvents(&player, QEvent::MetaCall);
            }
            order.append(name + "-exit");
        });
        player.processMpvEvents();
        QTRY_COMPARE(order.size(), 4);
        QCOMPARE(order, QStringList({"app-first-enter", "app-first-exit",
                                     "app-second-enter", "app-second-exit"}));
        QCOMPARE(player.m_eventReentries, quint64(1));
    }

    void backlogYieldsAndContinuesWithoutAnotherWakeup()
    {
        PlayerController player;
        QTest::qWait(100);
        // No callbacks will rescue a truncated drain. The consumer must
        // arrange every continuation itself until it reaches EVENT_NONE.
        mpv_set_wakeup_callback(player.handle(), nullptr, nullptr);
        constexpr int count = 512;
        for (int i = 0; i < count; ++i)
            QVERIFY(sendMessage(player, "app-burst-" + QByteArray::number(i)));
        int received = 0;
        bool inOrder = true;
        connect(&player, &PlayerController::appCommand, &player,
                [&](const QString &name, const QStringList &) {
            inOrder &= name == "app-burst-" + QString::number(received);
            ++received;
        });
        int receivedAtGuiTurn = -1;
        QObject inputReceiver;
        QMetaObject::invokeMethod(&inputReceiver, [&] { receivedAtGuiTurn = received; },
                                  Qt::QueuedConnection);
        player.processMpvEvents();
        QVERIFY2(received < count, "A single event drain consumed the entire backlog");
        QTRY_COMPARE_WITH_TIMEOUT(received, count, 3000);
        QVERIFY(inOrder);
        QVERIFY(receivedAtGuiTurn >= 0 && receivedAtGuiTurn < count);
        QVERIFY(player.m_maxEventBatch <= 128);
        QVERIFY(player.m_eventBudgetYields >= 3);
        QCOMPARE(player.m_eventOverflows, quint64(0));
        qInfo("backlog: messages=%d max-batch=%d yields=%llu input-at=%d",
              count, player.m_maxEventBatch,
              static_cast<unsigned long long>(player.m_eventBudgetYields), receivedAtGuiTurn);
    }

    void concurrentWakeupsCoalesce()
    {
        PlayerController player;
        QTest::qWait(100);
        mpv_set_wakeup_callback(player.handle(), nullptr, nullptr);
        QCoreApplication::sendPostedEvents(&player, QEvent::MetaCall);
        const auto posts = player.m_eventPosts.load();
        const auto wakeups = player.m_eventWakeups.load();
        {
            std::vector<std::jthread> producers;
            for (int t = 0; t < 4; ++t)
                producers.emplace_back([&] {
                    for (int i = 0; i < 5000; ++i) PlayerController::mpvWakeup(&player);
                });
        }
        QCOMPARE(player.m_eventWakeups.load() - wakeups, quint64(20000));
        QCOMPARE(player.m_eventPosts.load() - posts, quint64(1));
        QCoreApplication::sendPostedEvents(&player, QEvent::MetaCall);
        QVERIFY(!player.m_eventWakeupQueued.load());
        // A wakeup after the previous invocation was consumed must not get
        // stuck behind an old coalescing flag.
        QVERIFY(sendMessage(player, "app-after-burst"));
        QSignalSpy received(&player, &PlayerController::appCommand);
        PlayerController::mpvWakeup(&player);
        QTRY_COMPARE(received.size(), 1);
        QCOMPARE(player.m_eventPosts.load() - posts, quint64(2));
    }

    void wakeupsRaceWithEmptyQueue()
    {
        PlayerController player;
        QTest::qWait(100);
        mpv_set_wakeup_callback(player.handle(), nullptr, nullptr);
        std::atomic_bool failed{false};
        int received = 0;
        bool inOrder = true;
        connect(&player, &PlayerController::appCommand, &player,
                [&](const QString &name, const QStringList &) {
            inOrder &= name == "app-race-" + QString::number(received);
            ++received;
        });
        std::jthread producer([&] {
            for (int i = 0; i < 300; ++i) {
                if (!sendMessage(player, "app-race-" + QByteArray::number(i))) failed = true;
                PlayerController::mpvWakeup(&player);
                std::this_thread::yield();
            }
        });
        QTRY_COMPARE_WITH_TIMEOUT(received, 300, 3000);
        producer.join();
        QVERIFY(!failed.load());
        QVERIFY(inOrder);
        QCOMPARE(player.m_eventOverflows, quint64(0));
    }

    void slowHandlerYieldsAfterCurrentEvent()
    {
        PlayerController player;
        QTest::qWait(100);
        mpv_set_wakeup_callback(player.handle(), nullptr, nullptr);
        QVERIFY(sendMessage(player, "app-slow"));
        QVERIFY(sendMessage(player, "app-after-slow"));
        int received = 0;
        connect(&player, &PlayerController::appCommand, &player,
                [&](const QString &, const QStringList &) {
            if (++received == 1) QThread::msleep(20);
        });
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("perf: mpv-handler event=client-message.*"));
        player.processMpvEvents();
        QCOMPARE(received, 1);
        QTRY_COMPARE(received, 2);
    }

    void shutdownInsideHandlerWithoutAnotherWakeup()
    {
        PlayerController player;
        player.openFile(QFileInfo("assets/test/sample.mkv").absoluteFilePath());
        QTRY_VERIFY(!player.currentFile().isEmpty());
        QTRY_VERIFY(!player.m_idleActive);
        mpv_set_wakeup_callback(player.handle(), nullptr, nullptr);
        bool returned = false;
        connect(&player, &PlayerController::appCommand, &player,
                [&](const QString &, const QStringList &) {
            player.prepareForShutdown();
            QVERIFY(player.m_stopping);
            QVERIFY(player.m_finishShutdownPending);
            // The event is still being handled, so shutdown cannot enter
            // its nested event pump until this direct slot returns.
            QVERIFY(player.m_processingEvents);
            returned = true;
        });
        QVERIFY(sendMessage(player, "app-close"));
        player.processMpvEvents();
        QTRY_VERIFY(returned && !player.m_eventDeliveryEnabled.load());
        QVERIFY(!player.m_finishShutdownPending);
        QVERIFY(player.m_idleActive);
        QVERIFY(!player.m_loadHookPending);
        const auto posts = player.m_eventPosts.load();
        for (int i = 0; i < 1000; ++i) PlayerController::mpvWakeup(&player);
        QCoreApplication::sendPostedEvents(&player, QEvent::MetaCall);
        QCOMPARE(player.m_eventPosts.load(), posts);
        QCOMPARE(player.m_eventReentries, quint64(0));
    }

    void shutdownFromPresentationHook()
    {
        PlayerController player;
        player.requireVideoPresentation();
        bool closed = false;
        connect(&player, &PlayerController::presentationStateChanged, &player, [&] {
            if (player.m_processingEvents && !closed) {
                closed = true;
                player.prepareForShutdown();
            }
        });
        player.openFile(QFileInfo("assets/test/sample.mkv").absoluteFilePath());
        QTRY_VERIFY(closed && !player.m_eventDeliveryEnabled.load());
        QVERIFY(!player.m_loadHookPending);
        QVERIFY(player.m_idleActive);
    }

    void destructionWithQueuedDelivery()
    {
        for (int i = 0; i < 12; ++i) {
            auto player = std::make_unique<PlayerController>();
            PlayerController::mpvWakeup(player.get());
            QVERIFY(sendMessage(*player, "app-destroy"));
            player.reset();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        }
    }
};

QTEST_GUILESS_MAIN(TestEventDelivery)
#include "test_eventdelivery.moc"
