#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/PlayerController.h"
#include "input/ShortcutManager.h"
#include "SeekWhenReady.h"

namespace {

// ctest working dir and the exe dir differ; try the usual relative spots.
QString findAsset(const QString &relative)
{
    const QStringList candidates{
        relative,
        QStringLiteral("../") + relative,
        QStringLiteral("../../") + relative,
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }
    return {};
}

int selectedTrackId(PlayerController &player, const QString &type)
{
    const auto tracks = player.mpvProperty("track-list").toList();
    for (const auto &entry : tracks) {
        const auto track = entry.toMap();
        if (track.value("type") == type && track.value("selected").toBool())
            return track.value("id").toInt();
    }
    return -1;
}

} // namespace

class TestPlaylist : public QObject {
    Q_OBJECT

private slots:
    void appendRemoveClear();
    void siblingMediaFiles();
    void expandMediaPaths();
    void relativeSeekIsAccurate();
    void replayAtEnd_data();
    void replayAtEnd();
    void automaticPlaylistAdvance();
    void playlistSkipsMissingAndRepeats();
    void trackChangeFeedback();
    void dropAppendAndPlay();
    void recoversAfterInvalidMedia();
    void openFromIdleReportsLoading();
    void rememberedTracks_data();
    void rememberedTracks();
    void rememberedSubtitleOffAndVisibility();
    void rememberedKeyboardAndDefaultChoice();
    void immediateTrackChoiceBeforeNext();
    void cycledSubtitlesBackToOff();
};

// Drives the real mpv playlist engine headless. Keep a paused video active
// while mutating the queue so a short first item cannot advance the
// playlist before the assertions run.
void TestPlaylist::appendRemoveClear()
{
    const QByteArray previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;audio=no;pause=yes;hwdec=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS");
    else qputenv("FRAME_MPV_OPTS", previous);

    const QString mkv = findAsset(QStringLiteral("assets/test/sample.mkv"));
    QVERIFY2(!mkv.isEmpty(), "sample.mkv not found relative to test working dir");
    player.openFile(mkv);
    QTRY_VERIFY_WITH_TIMEOUT(player.duration() > 0, 6000);
    player.setPaused(true);
    QTRY_VERIFY_WITH_TIMEOUT(player.isPaused(), 3000);

    QSignalSpy changed(&player, &PlayerController::playlistChanged);
    QVERIFY(changed.isValid());

    const QString wav = findAsset(QStringLiteral("assets/test/tone.wav"));
    QVERIFY2(!wav.isEmpty(), "tone.wav not found relative to test working dir");

    player.playlistAppendFiles({wav, mkv, wav}, false);
    QTRY_COMPARE(player.playlist().size(), 4);
    QTRY_COMPARE(player.playlistPos(), 0);
    // playlistChanged is coalesced onto a 16 ms timer (one emit per event-
    // loop turn, so multi-file drops do not churn QML delegates once per
    // file). QTRY_COMPARE above can pass without ever spinning the loop,
    // so wait for the coalesced emission explicitly here.
    QTRY_VERIFY(changed.count() >= 1);

    // Entry shape mirrors mpv's playlist node: filename string present.
    const QVariantMap first = player.playlist().first().toMap();
    QVERIFY(first.contains(QStringLiteral("filename")));
    QVERIFY(!first.value(QStringLiteral("filename")).toString().isEmpty());

    player.playlistRemove(1);
    QTRY_COMPARE(player.playlist().size(), 3);

    // playlist-clear keeps the currently playing entry.
    player.playlistClear();
    QTRY_COMPARE(player.playlist().size(), 1);

    player.stop();
}

// F16 (A11): folder auto-enqueue helper. Pure filesystem logic — no mpv
// playback is driven here.
void TestPlaylist::siblingMediaFiles()
{
    PlayerController player;

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "could not create temp dir");

    // Plain lexicographic order would put b10 before b2; the helper must
    // keep the natural (episode-style) sequence. The queried file (z.mkv)
    // itself must be part of the result; the txt file must not.
    const QStringList names{QStringLiteral("a.mkv"),   QStringLiteral("b10.mkv"),
                            QStringLiteral("b2.mkv"),  QStringLiteral("cover.txt"),
                            QStringLiteral("z.mkv")};
    for (const QString &name : names) {
        QFile file(dir.filePath(name));
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.fileName()));
    }

    const QStringList siblings = player.siblingMediaFiles(dir.filePath(QStringLiteral("z.mkv")));
    QCOMPARE(siblings.size(), 4); // cover.txt excluded

    // Natural order, absolute paths, queried file itself included.
    QStringList expected;
    for (const char *name : {"a.mkv", "b2.mkv", "b10.mkv", "z.mkv"})
        expected.append(dir.filePath(QString::fromLatin1(name)));
    QCOMPARE(siblings, expected);
    QVERIFY(!siblings.isEmpty() && QFileInfo(siblings.constFirst()).isAbsolute());

    // A path that does not exist yields an empty list.
    QVERIFY(player.siblingMediaFiles(dir.filePath(QStringLiteral("nope.mkv"))).isEmpty());
    QVERIFY(player.siblingMediaFiles(QString()).isEmpty());
}

void TestPlaylist::expandMediaPaths()
{
    PlayerController player;

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "could not create temp dir");
    for (const char *name : {"b10.mkv", "b2.mkv", "notes.txt", "a.mp4"}) {
        QFile file(dir.filePath(QString::fromLatin1(name)));
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.fileName()));
    }
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("sub")));

    // A folder becomes its media files in natural order; files pass through
    // untouched (even non-media ones: the caller decided to open them), and
    // nested folders are not descended into.
    const QString keep = QStringLiteral("C:/elsewhere/clip.mov");
    const QStringList out = player.expandMediaPaths({keep, dir.path()});
    QStringList expected{keep};
    for (const char *name : {"a.mp4", "b2.mkv", "b10.mkv"})
        expected.append(dir.filePath(QString::fromLatin1(name)));
    QCOMPARE(out, expected);

    // A folder on the command line is an open target.
    QCOMPARE(player.resolveOpenTarget(dir.path()), QFileInfo(dir.path()).absoluteFilePath());

    // Pasted or dropped text may name a network stream, never one of mpv's
    // internal protocol handlers (descriptors, memory, image globs, capture
    // devices), and prose is not a URL.
    QCOMPARE(player.resolveOpenTarget(QStringLiteral("https://example.com/clip.m3u8")),
             QStringLiteral("https://example.com/clip.m3u8"));
    QCOMPARE(player.resolveOpenTarget(QStringLiteral("  \"rtsp://cam.local:554/live\"  ")),
             QStringLiteral("rtsp://cam.local:554/live"));
    for (const char *rejected : {"fd://0", "edl://host/x", "memory://abc", "mf://*.png",
                                 "av://dshow:video=Camera", "lavf://concat:a|b", "note: something"})
        QVERIFY2(player.resolveOpenTarget(QString::fromLatin1(rejected)).isEmpty(), rejected);
    QVERIFY(player.expandMediaPaths({dir.filePath(QStringLiteral("sub"))}).isEmpty());
}

void TestPlaylist::relativeSeekIsAccurate()
{
    const QByteArray previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;audio=no;pause=yes;hwdec=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS");
    else qputenv("FRAME_MPV_OPTS", previous);
    const QString file = findAsset(QStringLiteral("assets/test/sample.mkv"));
    QVERIFY(!file.isEmpty());
    player.openFile(file);
    QTRY_VERIFY_WITH_TIMEOUT(player.duration() > 8, 6000);
    QVERIFY(seekWhenReady(player, 1.2));
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.position() - 1.2) < 0.06, 6000);
    player.seekRelative(3.0);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.position() - 4.2) < 0.06, 6000);
    player.seekRelative(-3.0);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.position() - 1.2) < 0.06, 6000);
    player.prepareForShutdown();
}

void TestPlaylist::replayAtEnd_data()
{
    QTest::addColumn<QString>("action");
    QTest::addColumn<QString>("media");
    for (const QString &file : {QStringLiteral("sample.mkv"), QStringLiteral("tone.wav")}) {
        for (const QString &action : {QStringLiteral("play"), QStringLiteral("toggle"), QStringLiteral("space"), QStringLiteral("remapped"), QStringLiteral("media-play")})
            QTest::newRow(qPrintable(file + "-" + action)) << action << file;
    }
}

void TestPlaylist::replayAtEnd()
{
    QFETCH(QString, action);
    QFETCH(QString, media);
    const auto previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS");
    else qputenv("FRAME_MPV_OPTS", previous);
    ShortcutManager shortcuts;
    shortcuts.setPlayer(&player);
    shortcuts.setDefaultBindings({QVariantMap{{"key", "SPACE"}, {"command", "cycle pause"}},
                                  QVariantMap{{"key", "PLAY"}, {"command", "set pause no"}}});
    if (action == "remapped") shortcuts.applyOverrides({{"cycle pause", "Ctrl+r"}});
    player.playlistAppendAndPlay({findAsset("assets/test/sample.mkv"), findAsset("assets/test/" + media)}, 1);
    QTRY_VERIFY_WITH_TIMEOUT(player.duration() > 0, 5000);
    // Replay at the end must keep the final entry selected and retain the queue.
    QTRY_COMPARE(player.playlist().size(), 2);
    for (int repeat = 0; repeat < 2; ++repeat) {
        QVERIFY(seekWhenReady(player, player.duration() - .15));
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.eofReached() && player.isPaused(), 5000);
        if (action == "play") player.play();
        else if (action == "toggle") player.togglePause();
        else if (action == "remapped") shortcuts.handleKeyPress(Qt::Key_R, Qt::ControlModifier);
        else if (action == "media-play") shortcuts.handleKeyPress(Qt::Key_MediaPlay, Qt::NoModifier);
        else shortcuts.handleKeyPress(Qt::Key_Space, Qt::NoModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!player.eofReached() && !player.isPaused()
                                 && player.position() > .05 && player.position() < 2, 3000);
        QCOMPARE(player.playlistPos(), 1);
        QCOMPARE(player.playlist().size(), 2);
    }
    // Seeking away from EOF is different from requesting replay: preserve
    // pause and the chosen timestamp until the user asks to play.
    QVERIFY(seekWhenReady(player, player.duration() - .15));
    QTRY_VERIFY_WITH_TIMEOUT(player.eofReached() && player.isPaused(), 5000);
    player.seek(.5);
    QTRY_VERIFY(!player.eofReached() && qAbs(player.position() - .5) < .08);
    QVERIFY(player.isPaused());
    player.play();
    QTRY_VERIFY(!player.isPaused() && player.position() > .65);
    player.prepareForShutdown();
}

void TestPlaylist::trackChangeFeedback()
{
    const auto previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS");
    else qputenv("FRAME_MPV_OPTS", previous);
    QSignalSpy feedback(&player, &PlayerController::trackSelectionFeedback);
    player.openFile(findAsset("assets/test/sample.mkv"));
    QTRY_VERIFY(player.position() > .05);
    player.pause();
    QTRY_VERIFY(player.isPaused());
    feedback.clear();
    player.command({"audio-add", findAsset("assets/test/tone.wav"), "select", "Commentary", "eng"});
    QTRY_VERIFY_WITH_TIMEOUT(!feedback.isEmpty(), 10000);
    QCOMPARE(feedback.last().at(0).toString(), "audio");
    QVERIFY(feedback.last().at(1).toString().contains("Commentary"));
    QTemporaryDir dir;
    QFile subtitles(dir.filePath("captions.srt"));
    QVERIFY(subtitles.open(QIODevice::WriteOnly));
    subtitles.write("1\n00:00:00,000 --> 00:00:09,000\nFrame track selection test\n");
    subtitles.close();
    feedback.clear();
    player.command({"sub-add", subtitles.fileName(), "select", "English captions", "eng"});
    QTRY_VERIFY_WITH_TIMEOUT(!feedback.isEmpty(), 20000);
    QCOMPARE(feedback.last().at(0).toString(), "sub");
    QVERIFY(feedback.last().at(1).toString().contains("English captions"));
    feedback.clear();
    player.command({"set", "sub-visibility", "no"});
    QTRY_VERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(1).toString(), "Off");
    int subId = -1;
    for (const auto &entry : player.trackList()) {
        const auto track = entry.toMap();
        if (track.value("type") == "sub") subId = track.value("id").toInt();
    }
    QVERIFY(subId > 0);
    feedback.clear();
    player.setSubtitleTrack(subId); // Choosing a track restores hidden captions.
    QTRY_VERIFY(!feedback.isEmpty());
    // libass's first font-provider reload can replace an external track's
    // supplied title with its filename (and may clear its language).
    QVERIFY(feedback.last().at(1).toString().contains("captions"));
    feedback.clear();
    player.command({"set", "sid", "no"});
    QTRY_VERIFY(!feedback.isEmpty());
    QCOMPARE(feedback.last().at(1).toString(), "Off");
    player.prepareForShutdown();
}

void TestPlaylist::automaticPlaylistAdvance()
{
    const auto previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS");
    else qputenv("FRAME_MPV_OPTS", previous);
    const auto video = findAsset("assets/test/sample.mkv");
    const auto audio = findAsset("assets/test/tone.wav");
    player.playlistAppendAndPlay({video, audio});
    QTRY_VERIFY(player.duration() > 0 && player.playlistPos() == 0);
    QVERIFY(seekWhenReady(player, player.duration() - .15));
    QTRY_COMPARE_WITH_TIMEOUT(player.playlistPos(), 1, 5000);
    QTRY_VERIFY(!player.isPaused() && player.position() > .05 && player.currentFile() == audio);
    QVERIFY(seekWhenReady(player, player.duration() - .15));
    QTRY_VERIFY_WITH_TIMEOUT(player.eofReached() && player.isPaused(), 5000);
    QCOMPARE(player.playlistPos(), 1);
    player.play();
    player.pause(); // A quick change of mind must cancel deferred replay.
    QTest::qWait(300);
    QVERIFY(player.isPaused());
    player.playlistPlayIndex(0);
    QTRY_VERIFY(player.playlistPos() == 0 && !player.isPaused() && player.position() > .05);
    player.prepareForShutdown();
}

void TestPlaylist::playlistSkipsMissingAndRepeats()
{
    const auto previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS");
    else qputenv("FRAME_MPV_OPTS", previous);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto video = findAsset("assets/test/sample.mkv");
    const auto audio = findAsset("assets/test/tone.wav");
    QSignalSpy errors(&player, &PlayerController::errorOccurred);
    player.playlistAppendAndPlay({video, dir.filePath("missing.mp4"), audio});
    QTRY_VERIFY(player.duration() > 0 && player.playlistPos() == 0);
    QVERIFY(seekWhenReady(player, player.duration() + 10)); // Seeking past the end also advances.
    QTRY_VERIFY_WITH_TIMEOUT(player.playlistPos() == 2 && player.currentFile() == audio
                             && !player.isPaused() && player.position() > .05, 5000);
    QVERIFY(!errors.isEmpty());
    player.command({"set", "loop-playlist", "inf"});
    player.seek(player.duration() - .15);
    QTRY_VERIFY_WITH_TIMEOUT(player.playlistPos() == 0 && player.currentFile() == video
                             && !player.isPaused() && player.position() > .05, 5000);
    player.prepareForShutdown();
}

void TestPlaylist::dropAppendAndPlay()
{
    const QByteArray previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;audio=no;pause=yes;hwdec=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS");
    else qputenv("FRAME_MPV_OPTS", previous);
    const QString mkv = findAsset(QStringLiteral("assets/test/sample.mkv"));
    const QString wav = findAsset(QStringLiteral("assets/test/tone.wav"));
    QVERIFY(!mkv.isEmpty() && !wav.isEmpty());

    // Add to playlist must stay passive even when the player is empty.
    player.playlistAppendFiles({mkv, wav}, false);
    QTRY_COMPARE(player.playlist().size(), 2);
    QCOMPARE(player.state(), PlayerController::State::Idle);
    QVERIFY(player.currentFile().isEmpty());

    // Play now keeps the existing queue, selects the new occurrence of a
    // duplicate path, and actually plays even when previously paused.
    player.playlistAppendAndPlay({QString(), mkv, wav});
    QTRY_COMPARE(player.playlist().size(), 4);
    QTRY_COMPARE(player.playlistPos(), 2);
    QTRY_VERIFY(!player.isPaused());
    QTRY_VERIFY(player.duration() > 0);
    QCOMPARE(QDir::fromNativeSeparators(player.playlist().at(0).toMap().value("filename").toString()), mkv);
    QCOMPARE(QDir::fromNativeSeparators(player.playlist().at(3).toMap().value("filename").toString()), wav);

    // These calls deliberately share an event-loop turn: cached playlist
    // length is stale. The last Play now request must win by entry ID.
    player.playlistAppendFiles({wav}, false);
    player.playlistAppendAndPlay({mkv, wav});
    player.playlistAppendAndPlay({mkv, wav});
    QTRY_COMPARE(player.playlist().size(), 9);
    QTRY_COMPARE(player.playlistPos(), 7);

    player.setPaused(true);
    QTRY_VERIFY(player.isPaused());
    player.playlistAppendFiles({wav, mkv}, false);
    QTRY_COMPARE(player.playlist().size(), 11);
    QCOMPARE(player.playlistPos(), 7);
    QVERIFY(player.isPaused());

    // A subsequent explicit selection cancels the pending drop selection.
    player.playlistAppendAndPlay({mkv});
    player.playlistPlayIndex(0);
    QTRY_COMPARE(player.playlist().size(), 12);
    QTRY_COMPARE(player.playlistPos(), 0);
    // Opening a folder selection must target its appended entry, not an
    // index relative to the old queue. Select the second sibling here.
    player.playlistAppendAndPlay({wav, mkv}, 1);
    QTRY_COMPARE(player.playlist().size(), 14);
    QTRY_COMPARE(player.playlistPos(), 13);
    player.prepareForShutdown();
}

void TestPlaylist::recoversAfterInvalidMedia()
{
    const QByteArray previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;audio=no;pause=yes;hwdec=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS");
    else qputenv("FRAME_MPV_OPTS", previous);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString broken = directory.filePath(QStringLiteral("broken.mkv"));
    QFile file(broken);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a valid video container\n");
    file.close();
    QSignalSpy errors(&player, &PlayerController::errorOccurred);
    for (const QString &path : {broken, directory.filePath(QStringLiteral("missing.mp4"))}) {
        errors.clear();
        player.openFile(path);
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
        player.openFile(findAsset(QStringLiteral("assets/test/sample.mkv")));
        QTRY_VERIFY_WITH_TIMEOUT(player.duration() > 0, 5000);
        player.stop();
        QTRY_COMPARE(player.state(), PlayerController::State::Idle);
    }
    player.prepareForShutdown();
}

// The empty-state UI gates on Idle. An open must leave Idle on the call
// itself (mpv says nothing until START_FILE, which can be seconds away on a
// slow demuxer open), then retain a useful failure state if the file cannot open.
void TestPlaylist::openFromIdleReportsLoading()
{
    const QByteArray previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;audio=no;pause=yes;hwdec=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS");
    else qputenv("FRAME_MPV_OPTS", previous);

    const QString mkv = findAsset(QStringLiteral("assets/test/sample.mkv"));
    const QString wav = findAsset(QStringLiteral("assets/test/tone.wav"));
    QVERIFY(!mkv.isEmpty() && !wav.isEmpty());
    QCOMPARE(player.state(), PlayerController::State::Idle);

    player.openFile(mkv);
    QCOMPARE(player.state(), PlayerController::State::Loading);
    QTRY_VERIFY_WITH_TIMEOUT(player.duration() > 0, 6000);
    QTRY_VERIFY(player.state() != PlayerController::State::Loading);
    player.stop();
    QTRY_COMPARE(player.state(), PlayerController::State::Idle);

    // A passive append is not an open: the button must stay.
    player.playlistAppendFiles({wav}, false);
    QCOMPARE(player.state(), PlayerController::State::Idle);
    QTRY_COMPARE(player.playlist().size(), 1);
    QCOMPARE(player.state(), PlayerController::State::Idle);

    // Play-now from idle covers the gap between the append and the
    // selection it triggers from the reply.
    player.playlistAppendAndPlay({mkv}, 0);
    QCOMPARE(player.state(), PlayerController::State::Loading);
    QTRY_VERIFY_WITH_TIMEOUT(player.currentFile() == mkv && player.duration() > 0, 6000);
    player.stop();
    QTRY_COMPARE(player.state(), PlayerController::State::Idle);

    // A missing file retains a useful failure state until retry or Stop.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSignalSpy errors(&player, &PlayerController::errorOccurred);
    const QString missing = directory.filePath(QStringLiteral("missing.mp4"));
    player.openFile(missing);
    QCOMPARE(player.state(), PlayerController::State::Loading);
    QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
    QTRY_COMPARE(player.state(), PlayerController::State::Error);
    QCOMPARE(player.failedFile(), missing);
    QVERIFY(player.playbackError().contains(QStringLiteral("no longer available")));
    QVERIFY(player.unavailableFiles().contains(missing));
    QTRY_VERIFY(!player.playlist().isEmpty());
    QCOMPARE(player.playlist().first().toMap().value(QStringLiteral("filename")).toString(), missing);
    player.playlistAppendFiles({wav}, false);
    QTRY_COMPARE(player.playlist().size(), 2);
    QVERIFY(QFile::copy(mkv, missing));
    player.retryFailedFile();
    QTRY_VERIFY_WITH_TIMEOUT(player.currentFile() == missing && player.duration() > 0, 6000);
    QCOMPARE(player.playlist().size(), 2);
    QVERIFY(player.failedFile().isEmpty());
    QVERIFY(player.playbackError().isEmpty());
    QVERIFY(!player.unavailableFiles().contains(missing));
    player.stop();
    QTRY_COMPARE(player.state(), PlayerController::State::Idle);
    player.prepareForShutdown();
}

void TestPlaylist::rememberedTracks_data()
{
    QTest::addColumn<bool>("automatic");
    QTest::newRow("next-and-previous") << false;
    QTest::newRow("automatic-advance") << true;
}

void TestPlaylist::rememberedTracks()
{
    QFETCH(bool, automatic);
    const auto previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no;pause=yes;sub-auto=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS"); else qputenv("FRAME_MPV_OPTS", previous);
    const auto first = findAsset("assets/test/track-choice-a.mkv");
    const auto second = findAsset("assets/test/track-choice-b.mkv");
    const auto fallback = findAsset("assets/test/track-choice-fallback.mkv");
    QVERIFY(!first.isEmpty() && !second.isEmpty() && !fallback.isEmpty());
    player.openFile(first);
    QTRY_VERIFY_WITH_TIMEOUT(player.currentFile() == first && player.duration() > 0, 10000);
    QTRY_COMPARE(player.trackList().size(), 21); // video + ten audio + ten subtitle tracks
    player.setAudioTrack(7); // Japanese, not the adjacent commentary track
    player.setSubtitleTrack(9); // English SDH, not English full/forced
    QTRY_COMPARE(selectedTrackId(player, "audio"), 7);
    QTRY_COMPARE(selectedTrackId(player, "sub"), 9);
    player.playlistAppendFiles({second, fallback, second}, false);
    QTRY_COMPARE(player.playlist().size(), 4);

    int audioAtLoad = -1, subtitlesAtLoad = -1;
    connect(&player, &PlayerController::fileLoaded, &player, [&](const QString &file) {
        if (file == second) {
            audioAtLoad = selectedTrackId(player, "audio");
            subtitlesAtLoad = selectedTrackId(player, "sub");
        }
    });
    if (automatic) {
        player.command({"seek", "11.7", "absolute+exact"});
        QTRY_VERIFY(player.position() >= 11.5);
        player.play();
    } else {
        player.playlistNext();
    }
    QTRY_VERIFY_WITH_TIMEOUT(player.playlistPos() == 1 && player.currentFile() == second, 10000);
    // Selection is already correct before FILE_LOADED, rather than changing
    // language after the opening audio has started.
    QCOMPARE(audioAtLoad, 3);
    QCOMPARE(subtitlesAtLoad, 2);
    QTRY_COMPARE(selectedTrackId(player, "audio"), 3);
    QTRY_COMPARE(selectedTrackId(player, "sub"), 2);

    player.playlistNext();
    QTRY_VERIFY(player.playlistPos() == 2 && player.currentFile() == fallback);
    QCOMPARE(selectedTrackId(player, "audio"), 1); // no Japanese: use file default
    QCOMPARE(selectedTrackId(player, "sub"), 1); // no SDH: use file default
    player.playlistNext();
    QTRY_VERIFY(player.playlistPos() == 3 && player.currentFile() == second);
    QCOMPARE(selectedTrackId(player, "audio"), 3); // fallback did not erase choices
    QCOMPARE(selectedTrackId(player, "sub"), 2);
    player.playlistPlayIndex(0);
    QTRY_VERIFY(player.playlistPos() == 0 && player.currentFile() == first);
    QCOMPARE(selectedTrackId(player, "audio"), 7);
    QCOMPARE(selectedTrackId(player, "sub"), 9);
    player.prepareForShutdown();
}

void TestPlaylist::rememberedSubtitleOffAndVisibility()
{
    const auto previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no;pause=yes;sub-auto=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS"); else qputenv("FRAME_MPV_OPTS", previous);
    const auto first = findAsset("assets/test/track-choice-a.mkv");
    const auto second = findAsset("assets/test/track-choice-b.mkv");
    player.openFile(first);
    QTRY_VERIFY(player.currentFile() == first && player.duration() > 0);
    player.playlistAppendFiles({second}, false);
    QTRY_COMPARE(player.playlist().size(), 2);
    player.setSubtitleTrack(9);
    QTRY_COMPARE(selectedTrackId(player, "sub"), 9);
    player.command({"set", "sub-visibility", "no"});
    QTRY_VERIFY(!player.mpvProperty("sub-visibility").toBool());
    player.playlistNext();
    QTRY_COMPARE(player.currentFile(), second);
    QCOMPARE(selectedTrackId(player, "sub"), 2);
    QVERIFY(!player.mpvProperty("sub-visibility").toBool());

    player.setSubtitleTrack(2); // reselecting the same track makes it visible
    QTRY_VERIFY(player.mpvProperty("sub-visibility").toBool());
    player.command({"set", "sid", "no"});
    QTRY_COMPARE(selectedTrackId(player, "sub"), -1);
    player.playlistPrev();
    QTRY_COMPARE(player.currentFile(), first);
    QCOMPARE(selectedTrackId(player, "sub"), -1);
    QVERIFY(player.mpvProperty("sub-visibility").toBool());
    player.setSubtitleTrack(1);
    QTRY_COMPARE(selectedTrackId(player, "sub"), 1);
    player.playlistNext();
    QTRY_COMPARE(player.currentFile(), second);
    QCOMPARE(selectedTrackId(player, "sub"), 7);
    QVERIFY(player.mpvProperty("sub-visibility").toBool());
    player.prepareForShutdown();
}

void TestPlaylist::rememberedKeyboardAndDefaultChoice()
{
    const auto previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no;pause=yes;sub-auto=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS"); else qputenv("FRAME_MPV_OPTS", previous);
    const auto first = findAsset("assets/test/track-choice-a.mkv");
    const auto second = findAsset("assets/test/track-choice-b.mkv");
    const auto fallback = findAsset("assets/test/track-choice-fallback.mkv");
    player.openFile(first);
    QTRY_VERIFY(player.currentFile() == first && player.duration() > 0);
    player.playlistAppendFiles({second, fallback, second}, false);
    QTRY_COMPARE(player.playlist().size(), 4);
    player.command({"keybind", "a", "cycle audio"});
    player.command({"keypress", "a"});
    QTRY_COMPARE(selectedTrackId(player, "audio"), 2); // Spanish via native key binding
    player.playlistNext();
    QTRY_COMPARE(player.currentFile(), second);
    QCOMPARE(selectedTrackId(player, "audio"), 4);
    player.playlistNext();
    QTRY_COMPARE(player.currentFile(), fallback);
    QCOMPARE(selectedTrackId(player, "audio"), 1);
    player.setAudioTrack(1); // an explicit click on the fallback default replaces Spanish
    player.playlistNext();
    QTRY_VERIFY(player.playlistPos() == 3 && player.currentFile() == second);
    QCOMPARE(selectedTrackId(player, "audio"), 7);
    player.prepareForShutdown();
}

void TestPlaylist::immediateTrackChoiceBeforeNext()
{
    const auto previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no;pause=yes;sub-auto=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS"); else qputenv("FRAME_MPV_OPTS", previous);
    const auto first = findAsset("assets/test/track-choice-a.mkv");
    const auto second = findAsset("assets/test/track-choice-b.mkv");
    player.openFile(first);
    QTRY_VERIFY(player.currentFile() == first && player.trackList().size() == 21);
    player.playlistAppendFiles({second}, false);
    QTRY_COMPARE(player.playlist().size(), 2);
    player.setAudioTrack(7);
    player.setSubtitleTrack(9);
    player.playlistNext(); // intentionally do not wait for selection replies
    QTRY_COMPARE(player.currentFile(), second);
    QCOMPARE(selectedTrackId(player, "audio"), 3);
    QCOMPARE(selectedTrackId(player, "sub"), 2);
    player.prepareForShutdown();
}

void TestPlaylist::cycledSubtitlesBackToOff()
{
    const auto previous = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no;pause=yes;sub-auto=no;sid=no");
    PlayerController player;
    if (previous.isNull()) qunsetenv("FRAME_MPV_OPTS"); else qputenv("FRAME_MPV_OPTS", previous);
    const auto first = findAsset("assets/test/track-choice-a.mkv");
    const auto second = findAsset("assets/test/track-choice-b.mkv");
    player.openFile(first);
    QTRY_VERIFY(player.currentFile() == first && player.trackList().size() == 21);
    QCOMPARE(selectedTrackId(player, "sub"), -1);
    player.playlistAppendFiles({second}, false);
    QTRY_COMPARE(player.playlist().size(), 2);
    player.setSubtitleTrack(10);
    QTRY_COMPARE(selectedTrackId(player, "sub"), 10);
    player.command({"cycle", "sub"}); // back to the initial Off, with a previous menu choice
    QTRY_COMPARE(selectedTrackId(player, "sub"), -1);
    player.playlistNext();
    QTRY_COMPARE(player.currentFile(), second);
    QCOMPARE(selectedTrackId(player, "sub"), -1);
    player.prepareForShutdown();
}

QTEST_MAIN(TestPlaylist)
#include "test_playlist.moc"
