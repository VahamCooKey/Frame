#include <QtTest>
#include <QAbstractItemModelTester>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPersistentModelIndex>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalBlocker>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <memory>

#include "core/PlayerController.h"
#include "core/PlaylistSession.h"
#include "core/SettingsStore.h"
#include "SeekWhenReady.h"

namespace {
QString sample()
{
    return QStringLiteral(FRAME_TEST_SOURCE_DIR "/assets/test/sample.mkv");
}
QStringList queueIds(const QVariantList &queue)
{
    QStringList result;
    for (const auto &entry : queue)
        result.append(entry.toMap().value(QStringLiteral("id")).toString());
    return result;
}
QStringList ids(PlayerController &player)
{
    return queueIds(player.mpvProperty(QStringLiteral("playlist")).toList());
}
QStringList files(PlayerController &player)
{
    QStringList result;
    for (const auto &entry : player.mpvProperty(QStringLiteral("playlist")).toList())
        result.append(QDir::fromNativeSeparators(entry.toMap().value(QStringLiteral("filename")).toString()));
    return result;
}
QByteArray contents(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
void writeSession(const QString &path, const QStringList &paths, int selected)
{
    QJsonArray entries;
    for (const auto &file : paths) entries.append(QJsonObject{{QStringLiteral("filename"), file}});
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) qFatal("Could not create test session");
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
        {QStringLiteral("entries"), entries}, {QStringLiteral("selectedIndex"), selected}}).toJson());
}

bool copyQml(const QString &directory)
{
    const QDir source(QStringLiteral(FRAME_TEST_SOURCE_DIR "/qml"));
    for (const auto &name : source.entryList({"*.qml"}, QDir::Files))
        if (!QFile::copy(source.filePath(name), QDir(directory).filePath(name))) return false;
    QFile qmldir(QDir(directory).filePath("qmldir"));
    if (!qmldir.open(QIODevice::WriteOnly)) return false;
    qmldir.write("singleton Colors 1.0 Colors.qml\nsingleton Fonts 1.0 Fonts.qml\nsingleton KeyNames 1.0 KeyNames.qml\n");
    return true;
}

std::unique_ptr<QObject> stub(QQmlEngine &engine, const QByteArray &body)
{
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nQtObject { " + body + " }", {});
    std::unique_ptr<QObject> object(component.create());
    if (!object) qFatal("Invalid UI-test stub: %s", qPrintable(component.errorString()));
    return object;
}

QQuickItem *visualItem(QQuickItem *parent, const QString &name)
{
    if (parent->objectName() == name) return parent;
    for (auto *child : parent->childItems())
        if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}
} // namespace

class TestPlaylistSession : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void restoreDefaultsOff();
    void duplicateIdsPreservePlayingAndFocus();
    void filterReorderMapsToFullQueue();
    void rapidMovesAndRemovalUseStableIds();
    void rapidMixedActionsUseStableIds();
    void replayAtEofWithQueuedActions();
    void queuedActionsResolveExternalChanges();
    void queuedActionsResolveExternalChanges_data();
    void conflictingChangesTimeOutAndRecover();
    void matchingSnapshotStillRequiresCommandReply();
    void longQueueMovePreservesDelegates();
    void restoreRoundTripWithoutAutoplay();
    void explicitLaunchAndExistingQueueWin();
    void restoreRejectsStreamsFoldersAndPlaylistFiles();
    void preferenceResetDisablesRestore();
    void finishFlushesAndStopsPersistence();
    void finishCancelsQueuedActions();
    void chapterSeekPreservesPause();
    void qmlPlaylistKeyboardFilterAndDrag();
    void qmlPlaylistKeyboardFilterAndDrag_data();
    void qmlChaptersKeyboardAndCurrent();
private:
    QTemporaryDir m_preferences;
};

void TestPlaylistSession::initTestCase()
{
    QVERIFY(m_preferences.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_preferences.path());
    qputenv("FRAME_MPV_OPTS", "vo=null;audio=no;pause=yes;hwdec=no");
    QVERIFY2(QFileInfo::exists(sample()), "sample.mkv fixture missing");
}

void TestPlaylistSession::init()
{
    QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "Frame", "Frame");
    preferences.clear();
    preferences.sync();
}

void TestPlaylistSession::restoreDefaultsOff()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("session.json");
    writeSession(path, {sample()}, 0);
    const auto before = contents(path);
    SettingsStore settings;
    PlayerController player;
    PlaylistSession session(&player, &settings, path);
    QVERIFY(!session.restoreEnabled());
    QVERIFY(!session.restoreIfEmpty(false));
    QTest::qWait(50);
    QVERIFY(ids(player).isEmpty());
    QVERIFY(!session.saveNow());
    QCOMPARE(contents(path), before);
}

void TestPlaylistSession::duplicateIdsPreservePlayingAndFocus()
{
    QTemporaryDir dir;
    SettingsStore settings;
    PlayerController player;
    PlaylistSession session(&player, &settings, dir.filePath("session.json"));
    QAbstractItemModelTester tester(&session, QAbstractItemModelTester::FailureReportingMode::QtTest);
    session.restoreIfEmpty(true);
    player.playlistAppendFiles({sample(), sample(), sample()}, false);
    QTRY_COMPARE(session.rowCount(), 3);
    const auto original = ids(player);
    QCOMPARE(QSet<QString>(original.cbegin(), original.cend()).size(), 3);
    QVERIFY(session.play(original.at(1)));
    QTRY_COMPARE(player.playlistPos(), 1);
    QTRY_VERIFY(player.duration() > 0);
    player.pause();
    QTRY_VERIFY(player.isPaused());
    QTRY_COMPARE(session.currentId(), original.at(1));
    const QPersistentModelIndex selected(session.index(1));
    QSignalSpy loaded(&player, &PlayerController::fileLoaded);
    QVERIFY(session.moveRelative(original.at(1), original.at(2), true));
    QTRY_COMPARE(ids(player), (QStringList{original.at(0), original.at(2), original.at(1)}));
    QTRY_COMPARE(session.selectedRow(), 2);
    QTRY_COMPARE(player.playlistPos(), 2);
    QCOMPARE(session.currentId(), original.at(1));
    QCOMPARE(session.selectedId(), original.at(1));
    QVERIFY(selected.isValid());
    QCOMPARE(selected.row(), 2);
    QCOMPARE(selected.data(PlaylistSession::EntryRole).toMap().value("entryId").toString(), original.at(1));
    QCOMPARE(loaded.count(), 0);
    QVERIFY(player.isPaused());
    QVERIFY(session.remove(original.at(0)));
    QTRY_COMPARE(session.rowCount(), 2);
    QCOMPARE(session.selectedId(), original.at(1));
    QVERIFY(!session.remove(original.at(0))); // Stale ID cannot remove another occurrence.
    QVERIFY(!session.moveRelative("invalid", original.at(1), false));
}

void TestPlaylistSession::filterReorderMapsToFullQueue()
{
    QTemporaryDir dir;
    SettingsStore settings;
    PlayerController player;
    PlaylistSession session(&player, &settings, dir.filePath("session.json"));
    QAbstractItemModelTester tester(&session, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QString hidden = dir.filePath("other.mp4");
    player.playlistAppendFiles({sample(), hidden, sample()}, false);
    QTRY_COMPARE(session.rowCount(), 3);
    const auto original = ids(player);
    session.setSelectedId(original.at(2));
    session.setQuery("SAMPLE");
    QCOMPARE(session.rowCount(), 2);
    QCOMPARE(session.totalCount(), 3);
    QCOMPARE(session.selectedRow(), 1);
    QCOMPARE(session.idAt(1), original.at(2));
    QCOMPARE(session.entryForId(original.at(2)).value("queueIndex").toInt(), 2);
    QCOMPARE(ids(player), original);
    QVERIFY(session.moveBy(original.at(2), -1));
    QTRY_COMPARE(ids(player), (QStringList{original.at(2), original.at(0), original.at(1)}));
    QTRY_COMPARE(session.selectedRow(), 0);
    QVERIFY(session.moveBy(original.at(2), 1));
    QTRY_COMPARE(ids(player), (QStringList{original.at(0), original.at(2), original.at(1)}));
    QTRY_COMPARE(session.selectedRow(), 1);
    session.setQuery("no matches");
    QCOMPARE(session.rowCount(), 0);
    QCOMPARE(session.selectedId(), original.at(2));
    session.setQuery("");
    QCOMPARE(session.rowCount(), 3);
    QCOMPARE(session.selectedId(), original.at(2));
    QCOMPARE(session.selectedRow(), 1);
    QCOMPARE(files(player), (QStringList{sample(), sample(), hidden}));
    QCOMPARE(player.mpvProperty("playlist-playing-pos").toInt(), -1);
    QVERIFY(player.currentFile().isEmpty());
}

void TestPlaylistSession::restoreRoundTripWithoutAutoplay()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("session.json");
    const auto missing = dir.filePath("missing.mp4");
    SettingsStore settings;
    settings.setValue("general/restorePlaylist", true);
    {
        PlayerController player;
        PlaylistSession session(&player, &settings, path);
        QVERIFY(!session.restoreIfEmpty(true));
        player.playlistAppendFiles({sample(), missing, sample()}, false);
        QTRY_COMPARE(session.rowCount(), 3);
        session.setSelectedId(session.idAt(2));
        session.setQuery("missing"); // Persist full order, not the filtered view.
        QVERIFY(session.saveNow());
        const auto saved = QJsonDocument::fromJson(contents(path)).object();
        QCOMPARE(saved.value("entries").toArray().size(), 3);
        QCOMPARE(saved.value("selectedIndex").toInt(), 2);
        session.finish();
    }
    PlayerController restoredPlayer;
    PlaylistSession restored(&restoredPlayer, &settings, path);
    QSignalSpy loaded(&restoredPlayer, &PlayerController::fileLoaded);
    QVERIFY(restored.restoreIfEmpty(false));
    QVERIFY(!restored.restoreIfEmpty(false));
    QTRY_VERIFY(!restored.restoring());
    QCOMPARE(restored.rowCount(), 3);
    QCOMPARE(restored.selectedRow(), 2);
    QCOMPARE(restored.selectedId(), restored.idAt(2));
    QCOMPARE(files(restoredPlayer), (QStringList{sample(), missing, sample()}));
    QVERIFY(restored.entryForId(restored.idAt(1)).value("unavailable").toBool());
    QTest::qWait(150);
    QCOMPARE(loaded.count(), 0);
    QTRY_COMPARE(restoredPlayer.playlistPos(), 2);
    QCOMPARE(restoredPlayer.mpvProperty("playlist-playing-pos").toInt(), -1);
    QVERIFY(restoredPlayer.currentFile().isEmpty());
    QCOMPARE(restoredPlayer.state(), PlayerController::State::Idle);
    // The normal Play action deliberately activates the selected occurrence.
    restoredPlayer.play();
    QTRY_VERIFY(restoredPlayer.duration() > 0);
    QTRY_COMPARE(restoredPlayer.playlistPos(), 2);
    // Duration observations may precede A60's async metadata completion.
    QTRY_COMPARE(loaded.count(), 1);
}

void TestPlaylistSession::rapidMovesAndRemovalUseStableIds()
{
    QTemporaryDir dir;
    SettingsStore settings;
    PlayerController player;
    PlaylistSession session(&player, &settings, dir.filePath("session.json"));
    player.playlistAppendFiles({sample(), sample(), sample(), sample()}, false);
    QTRY_COMPARE(session.rowCount(), 4);
    const auto original = ids(player);
    // Dispatch together, before any observer or command reply can run.
    QVERIFY(session.moveBy(original.at(0), 1));
    QVERIFY(session.moveBy(original.at(0), 1));
    QVERIFY(session.moveBy(original.at(0), 1));
    QVERIFY(session.remove(original.at(1)));
    QTRY_COMPARE(ids(player), (QStringList{original.at(2), original.at(3), original.at(0)}));
    QTRY_COMPARE(session.rowCount(), 3);
    QCOMPARE(session.selectedId(), original.at(0));
    QVERIFY(session.remove(original.at(0)));
    QVERIFY(session.remove(original.at(2)));
    QVERIFY(session.remove(original.at(3)));
    QTRY_COMPARE(session.rowCount(), 0);
    QVERIFY(session.selectedId().isEmpty());
}

void TestPlaylistSession::rapidMixedActionsUseStableIds()
{
    QTemporaryDir dir;
    SettingsStore settings;
    PlayerController player;
    PlaylistSession session(&player, &settings, dir.filePath("session.json"));
    QAbstractItemModelTester tester(&session, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy errors(&session, &PlaylistSession::errorOccurred);
    player.playlistAppendFiles({sample(), sample(), sample(), sample()}, false);
    QTRY_COMPARE(session.rowCount(), 4);
    const auto original = ids(player);
    // No event processing between requests: later indexes depend on earlier
    // moves, and two selections must retain the controller's Play ordering.
    QVERIFY(session.play(original.at(2)));
    QVERIFY(session.moveBy(original.at(2), -1));
    QVERIFY(session.play(original.at(0)));
    QVERIFY(session.moveRelative(original.at(0), original.at(3), true));
    QVERIFY(session.remove(original.at(1)));
    QTRY_COMPARE(ids(player), (QStringList{original.at(2), original.at(3), original.at(0)}));
    QTRY_COMPARE(session.rowCount(), 3);
    QTRY_COMPARE(session.currentId(), original.at(0));
    QTRY_COMPARE(player.playlistPos(), 2);
    QTRY_VERIFY(player.duration() > 0 && !player.isPaused());
    QCOMPARE(session.selectedId(), original.at(0));
    QCOMPARE(errors.count(), 0);
}

void TestPlaylistSession::replayAtEofWithQueuedActions()
{
    QTemporaryDir dir;
    SettingsStore settings;
    PlayerController player;
    PlaylistSession session(&player, &settings, dir.filePath("session.json"));
    QSignalSpy errors(&session, &PlaylistSession::errorOccurred);
    player.playlistAppendFiles({sample(), sample()}, false);
    QTRY_COMPARE(session.rowCount(), 2);
    const auto original = ids(player);
    QVERIFY(session.play(original.at(1)));
    QTRY_COMPARE(session.currentId(), original.at(1));
    QTRY_VERIFY(player.duration() > 0 && !player.isPaused());
    QVERIFY(seekWhenReady(player, player.duration() - .05));
    QTRY_VERIFY_WITH_TIMEOUT(player.eofReached() && player.isPaused(), 5000);

    // Replaying an already-current entry need not change the observed order.
    // The subsequent move and selection still have to run, without leaving the
    // final selection paused or mistaking the old playing duplicate for it.
    QVERIFY(session.play(original.at(1)));
    QVERIFY(session.moveRelative(original.at(1), original.at(0), false));
    QVERIFY(session.play(original.at(0)));
    QTRY_COMPARE(ids(player), (QStringList{original.at(1), original.at(0)}));
    QTRY_COMPARE(session.currentId(), original.at(0));
    QTRY_VERIFY(!player.eofReached() && !player.isPaused() && player.position() > .05);
    QCOMPARE(session.selectedId(), original.at(0));
    QCOMPARE(errors.count(), 0);
}

void TestPlaylistSession::queuedActionsResolveExternalChanges()
{
    QFETCH(bool, removeTarget);
    QTemporaryDir dir;
    SettingsStore settings;
    PlayerController player;
    PlaylistSession session(&player, &settings, dir.filePath("session.json"));
    QAbstractItemModelTester tester(&session, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy errors(&session, &PlaylistSession::errorOccurred);
    player.playlistAppendFiles({sample(), sample(), sample(), sample()}, false);
    QTRY_COMPARE(session.rowCount(), 4);
    const auto original = ids(player);
    bool changed = false;
    bool removeAccepted = false;
    bool moveAccepted = false;
    int externalResult = -1;
    connect(&session, &PlaylistSession::modelUpdated, this, [&] {
        if (changed || session.idAt(0) != original.at(1)
            || session.idAt(1) != original.at(2) || session.idAt(2) != original.at(0)) return;
        changed = true;
        removeAccepted = session.remove(original.at(1));
        moveAccepted = session.moveRelative(original.at(3), original.at(2), false);
        // The first move is acknowledged, but the pump has not dispatched its
        // queued follow-ups. Mutate the engine without delivering a new Qt
        // observation: cached index zero now identifies the wrong duplicate.
        const char *move[] = {"playlist-move", "0", "4", nullptr};
        const char *remove[] = {"playlist-remove", "0", nullptr};
        externalResult = mpv_command(player.handle(), removeTarget ? remove : move);
    });
    QVERIFY(session.moveRelative(original.at(0), original.at(2), true));
    QTRY_VERIFY(changed);
    QVERIFY(removeAccepted);
    QVERIFY(moveAccepted);
    QCOMPARE(externalResult, 0);
    QTRY_COMPARE(ids(player), (QStringList{original.at(3), original.at(2), original.at(0)}));
    QTRY_COMPARE(session.rowCount(), 3);
    QTRY_COMPARE(session.selectedRow(), 0);
    QCOMPARE(session.selectedId(), original.at(3));
    QVERIFY(player.currentFile().isEmpty());
    QCOMPARE(errors.count(), 0);
}

void TestPlaylistSession::queuedActionsResolveExternalChanges_data()
{
    QTest::addColumn<bool>("removeTarget");
    QTest::newRow("external-reorder") << false;
    QTest::newRow("external-removal") << true;
}

void TestPlaylistSession::conflictingChangesTimeOutAndRecover()
{
    QTemporaryDir dir;
    SettingsStore settings;
    PlayerController player;
    PlaylistSession session(&player, &settings, dir.filePath("session.json"));
    QSignalSpy errors(&session, &PlaylistSession::errorOccurred);
    player.playlistAppendFiles({sample(), sample(), sample(), sample()}, false);
    QTRY_COMPARE(session.rowCount(), 4);
    const auto original = ids(player);
    QElapsedTimer elapsed;
    elapsed.start();
    QSignalBlocker blocked(&player);
    QVERIFY(session.moveRelative(original.at(0), original.at(3), true));
    const QStringList moved{original.at(1), original.at(2), original.at(3), original.at(0)};
    QTRY_COMPARE(queueIds(player.playlist()), moved);
    // Coalesce away the intermediate acknowledgement, as external queue edits
    // can do in the real player. A follow-up must not act on guessed indexes.
    const char *move[] = {"playlist-move", "0", "4", nullptr};
    QCOMPARE(mpv_command(player.handle(), move), 0);
    const QStringList conflict{original.at(2), original.at(3), original.at(0), original.at(1)};
    QTRY_COMPARE(queueIds(player.playlist()), conflict);
    QVERIFY(session.remove(original.at(2)));
    blocked.unblock();

    QSignalSpy observations(&player, &PlayerController::playlistChanged);
    QTimer changes;
    changes.setInterval(40);
    bool alternate = false;
    connect(&changes, &QTimer::timeout, this, [&] {
        alternate = !alternate;
        player.command({"set", "playlist-current-pos", alternate ? "1" : "0"});
    });
    changes.start();
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 3000);
    changes.stop();
    QVERIFY(elapsed.elapsed() < 3000); // Observations must not renew the deadline.
    QVERIFY(observations.count() > 1);
    QCOMPARE(ids(player), conflict); // The queued removal was discarded.
    QVERIFY(session.remove(original.at(2)));
    QTRY_COMPARE(ids(player), (QStringList{original.at(3), original.at(0), original.at(1)}));
    QTRY_COMPARE(session.rowCount(), 3);
    QVERIFY(player.currentFile().isEmpty());
    QCOMPARE(errors.count(), 1);
}

void TestPlaylistSession::matchingSnapshotStillRequiresCommandReply()
{
    QTemporaryDir dir;
    SettingsStore settings;
    PlayerController player;
    PlaylistSession session(&player, &settings, dir.filePath("session.json"));
    QSignalSpy errors(&session, &PlaylistSession::errorOccurred);
    player.playlistAppendFiles({sample(), sample(), sample()}, false);
    QTRY_COMPARE(session.rowCount(), 3);
    const auto original = ids(player);
    QSignalBlocker blocked(&player);
    QVERIFY(session.moveRelative(original.at(0), original.at(2), true));
    const QStringList moved{original.at(1), original.at(2), original.at(0)};
    QTRY_COMPARE(queueIds(player.playlist()), moved);
    // Drop the command reply as well as the model notification. A fresh
    // snapshot alone must not authorize the next asynchronous mutation.
    QVERIFY(session.remove(original.at(1)));
    blocked.unblock();
    QTest::qWait(100);
    QCOMPARE(ids(player), moved);
    QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 3000);
    QCOMPARE(ids(player), moved);
    QVERIFY(session.remove(original.at(1)));
    QTRY_COMPARE(ids(player), (QStringList{original.at(2), original.at(0)}));
}

void TestPlaylistSession::longQueueMovePreservesDelegates()
{
    QTemporaryDir dir;
    SettingsStore settings;
    PlayerController player;
    PlaylistSession session(&player, &settings, dir.filePath("session.json"));
    QStringList queue;
    for (int i = 0; i < 96; ++i) queue.append(sample());
    player.playlistAppendFiles(queue, false);
    QTRY_COMPARE(session.rowCount(), 96);
    const auto original = ids(player);
    QSignalSpy moved(&session, &QAbstractItemModel::rowsMoved);
    QSignalSpy reset(&session, &QAbstractItemModel::modelReset);
    const QPersistentModelIndex selected(session.index(0));
    QVERIFY(session.moveRelative(original.first(), original.last(), true));
    QTRY_COMPARE(session.selectedRow(), 95);
    QCOMPARE(selected.row(), 95);
    QCOMPARE(moved.count(), 1);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(ids(player).last(), original.first());
}

void TestPlaylistSession::explicitLaunchAndExistingQueueWin()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("session.json");
    writeSession(path, {sample(), sample()}, 1);
    SettingsStore settings;
    settings.setValue("general/restorePlaylist", true);
    PlayerController player;
    PlaylistSession session(&player, &settings, path);
    QVERIFY(!session.restoreIfEmpty(true));
    QVERIFY(ids(player).isEmpty());
    player.playlistAppendFiles({sample()}, false);
    QTRY_COMPARE(session.rowCount(), 1);
    session.finish();
    writeSession(path, {sample(), sample()}, 1);
    PlayerController other;
    other.playlistAppendFiles({dir.filePath("explicit.mp4")}, false);
    QTRY_COMPARE(ids(other).size(), 1);
    PlaylistSession otherSession(&other, &settings, path);
    QVERIFY(!otherSession.restoreIfEmpty(false));
    QCOMPARE(files(other), QStringList{dir.filePath("explicit.mp4")});
}

void TestPlaylistSession::restoreRejectsStreamsFoldersAndPlaylistFiles()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("session.json");
    const auto missing = dir.filePath("missing.mp4");
    writeSession(path, {"https://example.invalid/live.mp4", "memory://clip.mkv",
        "//server/share/network.mkv", dir.path(), dir.filePath("list.m3u"),
        "relative.mkv", sample(), missing, sample()}, 8);
    SettingsStore settings;
    settings.setValue("general/restorePlaylist", true);
    PlayerController player;
    PlaylistSession session(&player, &settings, path);
    QVERIFY(session.restoreIfEmpty(false));
    QTRY_VERIFY(!session.restoring());
    QCOMPARE(files(player), (QStringList{sample(), missing, sample()}));
    QCOMPARE(session.selectedRow(), 2);
    QVERIFY(player.currentFile().isEmpty());
}

void TestPlaylistSession::preferenceResetDisablesRestore()
{
    QTemporaryDir dir;
    SettingsStore settings;
    PlayerController player;
    PlaylistSession session(&player, &settings, dir.filePath("session.json"));
    session.restoreIfEmpty(true);
    session.setRestoreEnabled(true);
    QVERIFY(session.restoreEnabled());
    settings.resetAllSettings();
    QVERIFY(!session.restoreEnabled());
    QVERIFY(!session.saveNow());
}

void TestPlaylistSession::finishFlushesAndStopsPersistence()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("session.json");
    SettingsStore settings;
    settings.setValue("general/restorePlaylist", true);
    PlayerController player;
    PlaylistSession session(&player, &settings, path);
    session.restoreIfEmpty(true);
    player.playlistAppendFiles({sample(), sample()}, false);
    QTRY_COMPARE(session.rowCount(), 2);
    QVERIFY(!QFile::exists(path)); // Debounce has not written on every append.
    session.setSelectedId(session.idAt(1));
    // Apply a completed engine mutation without delivering Qt's coalesced
    // playlistChanged. Shutdown still needs the updated duplicate index.
    const char *move[] = {"playlist-move", "0", "2", nullptr};
    QCOMPARE(mpv_command(player.handle(), move), 0);
    session.finish();
    const auto saved = contents(path);
    QCOMPARE(QJsonDocument::fromJson(saved).object().value("selectedIndex").toInt(), 0);
    player.command({"playlist-remove", "0"});
    QTRY_COMPARE(session.rowCount(), 1);
    QTest::qWait(700);
    QCOMPARE(contents(path), saved);
}

void TestPlaylistSession::finishCancelsQueuedActions()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("session.json");
    SettingsStore settings;
    settings.setValue("general/restorePlaylist", true);
    PlayerController player;
    PlaylistSession session(&player, &settings, path);
    QSignalSpy errors(&session, &PlaylistSession::errorOccurred);
    session.restoreIfEmpty(true);
    player.playlistAppendFiles({sample(), sample(), sample()}, false);
    QTRY_COMPARE(session.rowCount(), 3);
    const auto original = ids(player);
    QSignalBlocker blocked(&player);
    QVERIFY(session.moveRelative(original.at(0), original.at(2), true));
    const QStringList moved{original.at(1), original.at(2), original.at(0)};
    QTRY_COMPARE(queueIds(player.playlist()), moved);
    QVERIFY(session.remove(original.at(1)));
    // Finish before the queued pump can run. Save the completed move, discard
    // the unsent removal, and cancel both the pump and its failure deadline.
    session.finish();
    blocked.unblock();
    const auto saved = contents(path);
    const auto object = QJsonDocument::fromJson(saved).object();
    QCOMPARE(object.value("entries").toArray().size(), 3);
    QCOMPARE(object.value("selectedIndex").toInt(), 2);
    QVERIFY(!session.remove(original.at(0)));
    QTest::qWait(2100);
    QCOMPARE(ids(player), moved);
    QCOMPARE(contents(path), saved);
    QCOMPARE(errors.count(), 0);
    QVERIFY(player.currentFile().isEmpty());
}

void TestPlaylistSession::chapterSeekPreservesPause()
{
    PlayerController player;
    player.openFile(sample());
    QTRY_VERIFY(player.duration() > 0);
    player.pause();
    QTRY_VERIFY(player.isPaused());
    player.seek(0.1); // ChapterPicker uses exactly this public operation.
    QTest::qWait(80);
    QVERIFY(player.isPaused());
    player.play();
    QTRY_VERIFY(!player.isPaused());
    player.seek(0.2);
    QTest::qWait(80);
    QVERIFY(!player.isPaused());
}

void TestPlaylistSession::qmlPlaylistKeyboardFilterAndDrag()
{
    QFETCH(bool, sidebar);
    QTemporaryDir directory;
    QVERIFY(copyQml(directory.path()));
    SettingsStore settings;
    PlayerController player;
    PlaylistSession session(&player, &settings, directory.filePath("session.json"));
    player.playlistAppendFiles({sample(), sample(), sample()}, false);
    QTRY_COMPARE(session.rowCount(), 3);
    const auto original = ids(player);
    QQmlEngine engine;
    auto shortcuts = stub(engine, R"(
        property bool menuOpen: false; property bool wheelOverScroller: false; property bool textInputActive: false
        property var commandKeys: ({})
        signal overridesChanged()
        signal dismissMenusRequested()
        function keyForCommand(command) { return "" }
        function commandForKey(key) { return "" }
    )");
    auto native = stub(engine, "function animationsEnabled() { return false }");
    auto names = stub(engine, R"(
        property bool cleanupEnabled: true
        function display(file, clean) { return String(file).split(/[\\/]/).pop() }
    )");
    auto thumbs = stub(engine, R"(
        signal posterReady(string file, string key, var image, bool success)
        signal posterCancelled(string file, string key)
        signal posterEvicted(string key)
        signal metadataChanged()
        property bool backgroundWorkAllowed: false
        function setPlaybackPressure(opening, playing, steady) {}
        function canPreview(file) { return false }
        function refreshMetadata(file, force) {}
        function requestPoster(file) { return false }
    )");
    engine.rootContext()->setContextProperty("Player", &player);
    engine.rootContext()->setContextProperty("Settings", &settings);
    engine.rootContext()->setContextProperty("Playlist", &session);
    engine.rootContext()->setContextProperty("Shortcuts", shortcuts.get());
    engine.rootContext()->setContextProperty("Native", native.get());
    engine.rootContext()->setContextProperty("MediaNames", names.get());
    engine.rootContext()->setContextProperty("Thumbs", thumbs.get());
    QQmlComponent component(&engine, QUrl::fromLocalFile(directory.filePath("PlaylistDrawer.qml")));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *drawer = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(drawer);
    QQuickWindow window;
    window.resize(sidebar ? 300 : 960, sidebar ? 740 : 260);
    drawer->setParentItem(window.contentItem());
    drawer->setWidth(sidebar ? 300 : 960);
    drawer->setHeight(sidebar ? 720 : 216);
    drawer->setProperty("sidebar", sidebar);
    drawer->setProperty("shown", true);
    window.show();
    window.requestActivate();
    QTest::qWait(30);
    QVERIFY(QMetaObject::invokeMethod(drawer, "focusCard", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(window.activeFocusItem());
    QCOMPARE(window.activeFocusItem()->property("entryId").toString(), original.at(0));
    QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
    QTRY_COMPARE(ids(player), (QStringList{original.at(1), original.at(0), original.at(2)}));
    QTRY_COMPARE(session.selectedRow(), 1);
    QTRY_COMPARE(window.activeFocusItem()->property("entryId").toString(), original.at(0));
    QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
    auto *search = drawer->findChild<QQuickItem *>("playlistSearch");
    QVERIFY(search);
    QTRY_VERIFY(search->hasActiveFocus());
    QVERIFY(shortcuts->property("textInputActive").toBool());
    for (const auto key : {Qt::Key_C, Qt::Key_H, Qt::Key_A, Qt::Key_P, Qt::Key_T, Qt::Key_E, Qt::Key_R, Qt::Key_S}) {
        QTest::keyClick(&window, key);
        QTest::qWait(5); // Allow model/focus restoration callbacks between keys.
        QVERIFY(search->hasActiveFocus());
    }
    QCOMPARE(session.query(), QStringLiteral("chapters"));
    QCOMPARE(session.rowCount(), 0);
    QCOMPARE(ids(player), (QStringList{original.at(1), original.at(0), original.at(2)}));
    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_COMPARE(session.rowCount(), 3);
    QTRY_COMPARE(window.activeFocusItem()->property("entryId").toString(), original.at(0));
    QVERIFY(!shortcuts->property("textInputActive").toBool());

    // The menu supplies the same operation without requiring a drag chord.
    QTest::keyClick(&window, Qt::Key_F10, Qt::ShiftModifier);
    auto *options = drawer->findChild<QQuickItem *>("playlistOptions");
    QVERIFY(options);
    QTRY_VERIFY(options->isVisible());
    QVERIFY(options->mapToScene({}).y() >= 0);
    QVERIFY(options->mapToScene(QPointF(0, options->height())).y() <= window.height());
    QTest::keyClick(&window, Qt::Key_Down);
    QTest::keyClick(&window, Qt::Key_Return);
    QTRY_COMPARE(ids(player), original);
    QTRY_COMPARE(window.activeFocusItem()->property("entryId").toString(), original.at(0));
    QTest::keyClick(&window, Qt::Key_Menu);
    QTRY_VERIFY(options->isVisible());
    QTest::keyClick(&window, Qt::Key_Down);
    QTest::keyClick(&window, Qt::Key_Down);
    QTest::keyClick(&window, Qt::Key_Return);
    QTRY_COMPARE(ids(player), (QStringList{original.at(1), original.at(0), original.at(2)}));

    auto *source = visualItem(drawer, "playlistCard_" + original.at(0));
    auto *target = visualItem(drawer, "playlistCard_" + original.at(2));
    QVERIFY(source);
    QVERIFY(target);
    const QPoint start = source->mapToScene(QPointF(40, 42)).toPoint();
    const QPoint end = target->mapToScene(sidebar ? QPointF(40, target->height() - 30)
                                                 : QPointF(target->width() - 30, 42)).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(&window, start + (sidebar ? QPoint(0, 20) : QPoint(20, 0)), 10);
    QTest::qWait(20);
    QTest::mouseMove(&window, end, 10);
    QTest::qWait(20);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, end);
    QTRY_COMPARE(ids(player), (QStringList{original.at(1), original.at(2), original.at(0)}));
    QVERIFY(player.currentFile().isEmpty()); // Drag never activates a card.
    drawer->setParentItem(nullptr);
}

void TestPlaylistSession::qmlPlaylistKeyboardFilterAndDrag_data()
{
    QTest::addColumn<bool>("sidebar");
    QTest::newRow("horizontal") << false;
    QTest::newRow("theater-sidebar") << true;
}

void TestPlaylistSession::qmlChaptersKeyboardAndCurrent()
{
    QTemporaryDir directory;
    QVERIFY(copyQml(directory.path()));
    QQmlEngine engine;
    auto player = stub(engine, R"(
        property var chapterList: [{title: "Opening", time: 0}, {title: "", time: 60}, {title: "Finale", time: 128}]
        property real position: 65
        property bool isPaused: true
        property real lastSeek: -1
        function seek(seconds) { lastSeek = seconds }
    )");
    auto shortcuts = stub(engine, R"(
        property bool menuOpen: false; property bool wheelOverScroller: false
        property var commandKeys: ({"script-message app-chapters": "Ctrl+h"})
    )");
    engine.rootContext()->setContextProperty("Player", player.get());
    engine.rootContext()->setContextProperty("Shortcuts", shortcuts.get());
    QQmlComponent component(&engine, QUrl::fromLocalFile(directory.filePath("ChapterPicker.qml")));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *picker = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(picker);
    QQuickWindow window;
    window.resize(600, 500);
    picker->setParentItem(window.contentItem());
    window.show();
    window.requestActivate();
    QVERIFY(QMetaObject::invokeMethod(picker, "open"));
    QTRY_VERIFY(window.activeFocusItem());
    auto *hint = picker->findChild<QQuickItem *>("chapterMenuShortcut");
    QVERIFY(hint);
    QCOMPARE(hint->property("sequence").toString(), QStringLiteral("Ctrl+H"));
    shortcuts->setProperty("commandKeys", QVariantMap{{"script-message app-chapters", "Alt+c"}});
    QTRY_COMPARE(hint->property("sequence").toString(), QStringLiteral("Alt+C"));
    QCOMPARE(picker->property("currentChapter").toInt(), 1);
    QVERIFY(shortcuts->property("menuOpen").toBool());
    QTest::keyClick(&window, Qt::Key_Down);
    QTest::keyClick(&window, Qt::Key_Return);
    QCOMPARE(player->property("lastSeek").toDouble(), 128.0);
    QVERIFY(player->property("isPaused").toBool());
    QVERIFY(!picker->isVisible());
    QVERIFY(!shortcuts->property("menuOpen").toBool());
    picker->setParentItem(nullptr);
}

QTEST_MAIN(TestPlaylistSession)
#include "test_playlistsession.moc"
