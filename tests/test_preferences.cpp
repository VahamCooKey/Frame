#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <cmath>

#include "core/PlaybackPreferences.h"
#include "core/PlayerController.h"
#include "core/SettingsStore.h"

namespace {

QVariantMap selectedTrack(PlayerController &player, const QString &type)
{
    for (const auto &entry : player.mpvProperty(QStringLiteral("track-list")).toList()) {
        const auto track = entry.toMap();
        if (track.value(QStringLiteral("type")) == type && track.value(QStringLiteral("selected")).toBool())
            return track;
    }
    return {};
}

int selectedId(PlayerController &player, const QString &type)
{
    return selectedTrack(player, type).value(QStringLiteral("id"), -1).toInt();
}

QString fixture(const QString &name)
{
    return QFileInfo(QStringLiteral("assets/test/") + name).absoluteFilePath();
}

bool optionClose(const QVariant &actual, double expected)
{
    // mpv versions can store options as float even when MPV_FORMAT_NODE
    // returns a double. Verify the requested value within that precision.
    return actual.isValid() && std::isfinite(actual.toDouble())
        && std::abs(actual.toDouble() - expected) <= 1e-6;
}

} // namespace

class TestPreferences : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void typedDefaultsAndFallbacks();
    void resetPublishesDefaultsAndKeepsSessionData();
    void resetSingleSettingPublishesItsDefault();
    void cacheDefaultRestoresInitializedBackend();
    void startupAndResetApplyLiveDefaults();
    void fileDefaultsDescribeSavedIntent();
    void newFilesRestoreDefaultsBeforeFileLoaded();
    void languagesAreHumanReadableAndPersistCodes();
    void savedLanguagesApplyBeforeFileLoaded();
    void manualChoiceWinsOverSavedLanguagesAndReset();
    void subtitleOffWinsOverSavedLanguage();
    void missingManualMatchKeepsFutureIntent();
    void restartUsesSavedLanguageNotNumericTrackIds();
    void shaderDisableRemoveAndReset();
    void screenshotSequenceTemplates();
    void invalidBookmarkWrites_data();
    void invalidBookmarkWrites();
    void malformedStoredBookmarks();
    void bookmarkToggleKeepsValidTimes();

private:
    QTemporaryDir m_profile;
    QByteArray m_previousOptions;
};

void TestPreferences::initTestCase()
{
    QVERIFY(m_profile.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_profile.path() + QStringLiteral("/system"));
    m_previousOptions = qgetenv("FRAME_MPV_OPTS");
    qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;hwdec=no;pause=yes;demuxer-max-bytes=48MiB");
    for (const QString &name : {QStringLiteral("track-choice-a.mkv"), QStringLiteral("track-choice-b.mkv"),
                               QStringLiteral("track-choice-fallback.mkv")})
        QVERIFY2(QFileInfo::exists(fixture(name)), qPrintable(fixture(name)));
}

void TestPreferences::cleanupTestCase()
{
    if (m_previousOptions.isNull()) qunsetenv("FRAME_MPV_OPTS");
    else qputenv("FRAME_MPV_OPTS", m_previousOptions);
}

void TestPreferences::init()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Frame"), QStringLiteral("Frame"));
    settings.clear();
    settings.sync();
}

void TestPreferences::typedDefaultsAndFallbacks()
{
    SettingsStore settings;
    QCOMPARE(settings.value("video/aspectOverride").toString(), QStringLiteral("no"));
    QCOMPARE(settings.value("subtitles/delayDefault").toDouble(), 0.0);
    QCOMPARE(settings.value("interface/playlistMotion").toString(), QStringLiteral("system"));
    // A stale caller's fallback cannot disagree with a known default.
    QCOMPARE(settings.value("interface/osdHideMs", 999).toInt(), 1400);
    QCOMPARE(settings.value("custom/setting", 42).toInt(), 42);
    QVERIFY(settings.flag("general/resumeEnabled"));
    settings.setValue("general/resumeEnabled", QStringLiteral("false"));
    QVERIFY(!settings.flag("general/resumeEnabled"));
    QCOMPARE(settings.value("general/resumeEnabled").metaType(), QMetaType::fromType<bool>());
    settings.setValue("general/resumeEnabled", QStringLiteral("malformed"));
    QVERIFY(settings.flag("general/resumeEnabled"));
    settings.setValue("subtitles/fontSize", QStringLiteral("broken"));
    QCOMPARE(settings.value("subtitles/fontSize").toInt(), 55);
    settings.setValue("audio/delayDefault", qQNaN());
    QCOMPARE(settings.value("audio/delayDefault").toDouble(), 0.0);
    settings.setValue("general/screenshotFormat", QStringLiteral("jpeg"));
    QCOMPARE(settings.value("general/screenshotFormat").toString(), QStringLiteral("jpg"));
}

void TestPreferences::invalidBookmarkWrites_data()
{
    QTest::addColumn<double>("position");
    QTest::newRow("nan") << qQNaN();
    QTest::newRow("positive-infinity") << qInf();
    QTest::newRow("negative-infinity") << -qInf();
    QTest::newRow("negative-time") << -0.01;
}

void TestPreferences::invalidBookmarkWrites()
{
    QFETCH(double, position);
    const QString file = QStringLiteral("https://example.invalid/video.mkv");
    SettingsStore store;
    QVERIFY(store.toggleBookmark(file, 12.125));
    QSettings persisted(QSettings::IniFormat, QSettings::UserScope, "Frame", "Frame");
    const auto keysBefore = persisted.allKeys();
    const auto marksBefore = store.bookmarks(file);
    QVERIFY(!store.toggleBookmark(file, position));
    persisted.sync();
    QCOMPARE(persisted.allKeys(), keysBefore);
    QCOMPARE(store.bookmarks(file), marksBefore);
    const auto key = QStringLiteral("bookmarks/") + QString::fromLatin1(
        QCryptographicHash::hash(file.toUtf8(), QCryptographicHash::Md5).toHex());
    QCOMPARE(persisted.value(key).toStringList(), QStringList({"12.125"}));
}

void TestPreferences::malformedStoredBookmarks()
{
    const QString file = QStringLiteral("https://example.invalid/video.mkv");
    const auto key = QStringLiteral("bookmarks/") + QString::fromLatin1(
        QCryptographicHash::hash(file.toUtf8(), QCryptographicHash::Md5).toHex());
    QSettings persisted(QSettings::IniFormat, QSettings::UserScope, "Frame", "Frame");
    persisted.setValue(key, QStringList({"9.125", "inf", "+inf", "-inf", "nan", "NaN",
        "1e9999", "broken", "-2", "", "0.000", "2.500"}));
    persisted.sync();
    SettingsStore store;
    QCOMPARE(store.bookmarks(file), QVariantList({0.0, 2.5, 9.125}));
    QVERIFY(store.toggleBookmark(file, 5.25));
    persisted.sync();
    QCOMPARE(persisted.value(key).toStringList(), QStringList({"0.000", "2.500", "5.250", "9.125"}));
}

void TestPreferences::bookmarkToggleKeepsValidTimes()
{
    SettingsStore store;
    const QString file = QStringLiteral("https://example.invalid/video.mkv");
    QVERIFY(store.toggleBookmark(file, 12.125));
    QVERIFY(store.toggleBookmark(file, 0.0));
    QVERIFY(store.toggleBookmark(file, 2.5));
    QCOMPARE(store.bookmarks(file), QVariantList({0.0, 2.5, 12.125}));
    QVERIFY(!store.toggleBookmark(file, 12.625)); // Existing inclusive 0.5 s tolerance.
    QCOMPARE(store.bookmarks(file), QVariantList({0.0, 2.5}));
    SettingsStore reopened;
    QCOMPARE(reopened.bookmarks(file), QVariantList({0.0, 2.5}));
}

// The per-row "Reset to default" link: one key goes back to its typed
// default and is published like an edit; every other change stays.
void TestPreferences::resetSingleSettingPublishesItsDefault()
{
    SettingsStore settings;
    QVERIFY(settings.isDefault("subtitles/fontSize"));
    settings.setValue("subtitles/fontSize", 70);
    settings.setValue("audio/delayDefault", 0.8);
    settings.setValue("general/resumeEnabled", QStringLiteral("true"));
    QVERIFY(!settings.isDefault("subtitles/fontSize"));
    QVERIFY(!settings.isDefault("audio/delayDefault"));
    // Stored text equal to the default still counts as the default.
    QVERIFY(settings.isDefault("general/resumeEnabled"));
    // A value with no typed default is "default" only while unset.
    QVERIFY(settings.isDefault("custom/setting"));
    settings.setValue("custom/setting", 1);
    QVERIFY(!settings.isDefault("custom/setting"));

    QSignalSpy changes(&settings, &SettingsStore::settingChanged);
    QSignalSpy reset(&settings, &SettingsStore::settingsReset);
    settings.resetSetting("subtitles/fontSize");
    QCOMPARE(reset.count(), 0);
    QCOMPARE(changes.count(), 1);
    QCOMPARE(changes.first().at(0).toString(), QStringLiteral("subtitles/fontSize"));
    QCOMPARE(changes.first().at(1).toInt(), 55);
    QVERIFY(!settings.contains("subtitles/fontSize"));
    QVERIFY(settings.isDefault("subtitles/fontSize"));
    QCOMPARE(settings.value("audio/delayDefault").toDouble(), 0.8);
    settings.resetSetting("custom/setting");
    QVERIFY(settings.isDefault("custom/setting"));
}

void TestPreferences::resetPublishesDefaultsAndKeepsSessionData()
{
    SettingsStore settings;
    const QString file = m_profile.filePath(QStringLiteral("a.mkv"));
    settings.addRecent(file);
    settings.saveResume(file, 12.5);
    QVERIFY(settings.toggleBookmark(file, 4.0));
    settings.setShortcutOverride(QStringLiteral("cycle pause"), QStringLiteral("P"));
    settings.setWindowGeometry({{QStringLiteral("x"), 10}, {QStringLiteral("width"), 800}});
    settings.setVolume(64.0);
    settings.setAppliedShaders({QStringLiteral("keep-file.glsl")});
    settings.setValue("interface/playlistMotion", QStringLiteral("on"));
    settings.setValue("audio/delayDefault", 0.8);
    settings.setValue("custom/setting", 12);
    QSignalSpy changes(&settings, &SettingsStore::settingChanged);
    QSignalSpy reset(&settings, &SettingsStore::settingsReset);
    QSignalSpy shaders(&settings, &SettingsStore::appliedShadersChanged);
    settings.resetAllSettings();
    QCOMPARE(reset.count(), 1);
    QCOMPARE(shaders.count(), 1);
    QCOMPARE(changes.count(), SettingsStore::defaults().size() + 1);
    for (const auto &change : changes) {
        const QString key = change[0].toString();
        QCOMPARE(change[1], settings.defaultValue(key));
        QVERIFY(!settings.contains(key));
    }
    QCOMPARE(settings.recentFiles(), QVariantList{file});
    QCOMPARE(settings.resumePosition(file), 12.5);
    QCOMPARE(settings.bookmarks(file), QVariantList{4.0});
    QCOMPARE(settings.shortcutOverrides().value("cycle pause").toString(), QStringLiteral("P"));
    QCOMPARE(settings.windowGeometry().value("width").toInt(), 800);
    QCOMPARE(settings.volume(), 64.0);
    QVERIFY(settings.appliedShaders().isEmpty());
}

void TestPreferences::cacheDefaultRestoresInitializedBackend()
{
    SettingsStore settings;
    settings.setValue("advanced/demuxerMaxBytes", QStringLiteral("128MiB"));
    PlayerController player;
    const qint64 initialized = player.mpvProperty("demuxer-max-bytes").toLongLong();
    QCOMPARE(initialized, 48LL * 1024 * 1024);
    PlaybackPreferences preferences(&settings, &player);
    QTRY_COMPARE(player.mpvProperty("demuxer-max-bytes").toLongLong(), 128LL * 1024 * 1024);
    settings.setValue("advanced/demuxerMaxBytes", QString());
    QTRY_COMPARE(player.mpvProperty("demuxer-max-bytes").toLongLong(), initialized);
    settings.setValue("advanced/demuxerMaxBytes", QStringLiteral("512MiB"));
    QTRY_COMPARE(player.mpvProperty("demuxer-max-bytes").toLongLong(), 512LL * 1024 * 1024);
    settings.resetAllSettings();
    QTRY_COMPARE(player.mpvProperty("demuxer-max-bytes").toLongLong(), initialized);
}

void TestPreferences::startupAndResetApplyLiveDefaults()
{
    SettingsStore settings;
    settings.setValue("audio/delayDefault", 0.25);
    settings.setValue("subtitles/delayDefault", -0.5);
    settings.setValue("video/aspectOverride", QStringLiteral("4:3"));
    settings.setValue("video/deinterlace", true);
    settings.setValue("general/playlistRepeat", true);
    settings.setValue("subtitles/fontSize", 70);
    settings.setValue("general/screenshotDirectory", m_profile.path());
    PlayerController player;
    QSignalSpy commands(&player, &PlayerController::commandFinished);
    PlaybackPreferences preferences(&settings, &player);
    QTRY_VERIFY(commands.count() >= 23);
    for (const auto &command : commands)
        QVERIFY2(command[1].toBool(), qPrintable(command[2].toString()));
    QTRY_VERIFY(optionClose(player.mpvProperty("audio-delay"), 0.25));
    QTRY_VERIFY(optionClose(player.mpvProperty("sub-delay"), -0.5));
    QTRY_VERIFY(optionClose(player.mpvProperty("video-aspect-override"), 4.0 / 3.0));
    QTRY_VERIFY(player.mpvProperty("deinterlace").toBool());
    QTRY_COMPARE(player.mpvProperty("loop-playlist").toString(), QStringLiteral("inf"));
    settings.resetAllSettings();
    QTRY_VERIFY(optionClose(player.mpvProperty("audio-delay"), 0.0));
    QTRY_VERIFY(optionClose(player.mpvProperty("sub-delay"), 0.0));
    QTRY_VERIFY(player.mpvProperty("video-aspect-override").toDouble() < 0.0);
    QTRY_VERIFY(!player.mpvProperty("deinterlace").toBool());
    QTRY_COMPARE(player.mpvProperty("sub-font-size").toInt(), 55);
    QTRY_COMPARE(player.mpvProperty("screenshot-directory").toString(),
                 PlaybackPreferences::defaultScreenshotDirectoryPath());
    QTRY_VERIFY(player.mpvProperty("af").toList().isEmpty());
}

void TestPreferences::fileDefaultsDescribeSavedIntent()
{
    SettingsStore settings;
    PlayerController player;
    PlaybackPreferences preferences(&settings, &player);
    QSignalSpy changed(&preferences, &PlaybackPreferences::fileDefaultsChanged);
    settings.setValue("video/aspectOverride", QStringLiteral("16:9"));
    settings.setValue("audio/delayDefault", 0.4);
    settings.setValue("subtitles/delayDefault", -0.2);
    QCOMPARE(changed.count(), 3);
    QCOMPARE(preferences.defaultAspectIndex(), 1);
    const auto defaults = preferences.fileDefaults();
    QCOMPARE(defaults.value("video-aspect-override").toString(), QStringLiteral("16:9"));
    QCOMPARE(defaults.value("audio-delay").toDouble(), 0.4);
    QCOMPARE(defaults.value("sub-delay").toDouble(), -0.2);
    // Current-file commands never become the saved default.
    player.command({"set", "video-aspect-override", "no"});
    player.command({"set", "audio-delay", "-0.7"});
    player.command({"set", "sub-delay", "0.8"});
    QTRY_VERIFY(optionClose(player.mpvProperty("sub-delay"), 0.8));
    QCOMPARE(preferences.fileDefaults(), defaults);
    settings.resetAllSettings();
    QCOMPARE(preferences.defaultAspectIndex(), 0);
    QCOMPARE(preferences.fileDefaults().value("audio-delay").toDouble(), 0.0);
    QCOMPARE(preferences.fileDefaults().value("sub-delay").toDouble(), 0.0);
}

void TestPreferences::languagesAreHumanReadableAndPersistCodes()
{
    SettingsStore settings;
    PlayerController player;
    PlaybackPreferences preferences(&settings, &player);
    const auto options = preferences.languageOptions();
    QCOMPARE(options.first().toMap().value("label").toString(), QStringLiteral("Automatic"));
    QCOMPARE(options.first().toMap().value("value").toString(), QString());
    QVERIFY(options.size() > 25);
    for (const auto &option : options) {
        const auto entry = option.toMap();
        QVERIFY(!entry.value("label").toString().isEmpty());
        bool numeric = false;
        entry.value("value").toString().toInt(&numeric);
        QVERIFY(!numeric);
    }
    preferences.setPreferredAudioLanguage(QStringLiteral("JPN"));
    preferences.setPreferredSubtitleLanguage(QStringLiteral("eng"));
    QCOMPARE(settings.value("audio/preferredLanguage").toString(), QStringLiteral("ja"));
    QCOMPARE(settings.value("subtitles/preferredLanguage").toString(), QStringLiteral("en"));
    QTRY_COMPARE(player.mpvProperty("alang").toStringList(), QStringList({"ja", "jpn"}));
    QTRY_COMPARE(player.mpvProperty("slang").toStringList(), QStringList({"en", "eng"}));
    preferences.setPreferredAudioLanguage(QStringLiteral("7"));
    QCOMPARE(settings.value("audio/preferredLanguage").toString(), QString());
    QTRY_VERIFY(player.mpvProperty("alang").toList().isEmpty());
}

void TestPreferences::newFilesRestoreDefaultsBeforeFileLoaded()
{
    SettingsStore settings;
    settings.setValue("video/aspectOverride", QStringLiteral("4:3"));
    settings.setValue("audio/delayDefault", 0.35);
    settings.setValue("subtitles/delayDefault", -0.15);
    PlayerController player;
    PlaybackPreferences preferences(&settings, &player);
    player.setFileDefaults(preferences.fileDefaults());
    connect(&preferences, &PlaybackPreferences::fileDefaultsChanged, &player, [&] {
        player.setFileDefaults(preferences.fileDefaults());
    });
    player.setPaused(true);
    QSignalSpy loaded(&player, &PlayerController::fileLoaded);
    QVariantMap observed;
    connect(&player, &PlayerController::fileLoaded, &player, [&] {
        observed = {{"aspect", player.mpvProperty("video-aspect-override")},
                    {"audio", player.mpvProperty("audio-delay")},
                    {"subtitles", player.mpvProperty("sub-delay")}};
    });
    player.openFile(fixture("track-choice-a.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 6000);
    QVERIFY(optionClose(observed.value("aspect"), 4.0 / 3.0));
    QVERIFY(optionClose(observed.value("audio"), 0.35));
    QVERIFY(optionClose(observed.value("subtitles"), -0.15));
    player.command({"set", "video-aspect-override", "16:9"});
    player.command({"set", "audio-delay", "-0.6"});
    player.command({"set", "sub-delay", "0.9"});
    QTRY_VERIFY(optionClose(player.mpvProperty("sub-delay"), 0.9));
    player.openFile(fixture("track-choice-b.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 6000);
    QVERIFY(optionClose(observed.value("aspect"), 4.0 / 3.0));
    QVERIFY(optionClose(observed.value("audio"), 0.35));
    QVERIFY(optionClose(observed.value("subtitles"), -0.15));
    settings.resetAllSettings();
    player.openFile(fixture("track-choice-a.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 3, 6000);
    QVERIFY(observed.value("aspect").toDouble() < 0.0);
    QVERIFY(optionClose(observed.value("audio"), 0.0));
    QVERIFY(optionClose(observed.value("subtitles"), 0.0));
}

void TestPreferences::savedLanguagesApplyBeforeFileLoaded()
{
    SettingsStore settings;
    settings.setValue("audio/preferredLanguage", QStringLiteral("ja"));
    settings.setValue("subtitles/preferredLanguage", QStringLiteral("hi"));
    PlayerController player;
    PlaybackPreferences preferences(&settings, &player);
    player.setPaused(true);
    QSignalSpy loaded(&player, &PlayerController::fileLoaded);
    int audioAtLoad = -1, subtitlesAtLoad = -1;
    connect(&player, &PlayerController::fileLoaded, &player, [&] {
        audioAtLoad = selectedId(player, "audio");
        subtitlesAtLoad = selectedId(player, "sub");
    });
    player.openFile(fixture("track-choice-a.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 6000);
    QCOMPARE(audioAtLoad, 7);
    QCOMPARE(subtitlesAtLoad, 5);
    // Changes are defaults for future opens, not a new explicit session
    // selection. The outgoing automatic choices must not be remembered.
    preferences.setPreferredAudioLanguage(QStringLiteral("fr"));
    QTRY_COMPARE(player.mpvProperty("alang").toStringList(), QStringList({"fr", "fra", "fre"}));
    QCOMPARE(selectedId(player, "audio"), 7);
    player.openFile(fixture("track-choice-b.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 6000);
    QCOMPARE(audioAtLoad, 2);
    QCOMPARE(subtitlesAtLoad, 3);
}

void TestPreferences::manualChoiceWinsOverSavedLanguagesAndReset()
{
    SettingsStore settings;
    settings.setValue("audio/preferredLanguage", QStringLiteral("ja"));
    settings.setValue("subtitles/preferredLanguage", QStringLiteral("hi"));
    PlayerController player;
    PlaybackPreferences preferences(&settings, &player);
    player.setPaused(true);
    QSignalSpy loaded(&player, &PlayerController::fileLoaded);
    player.openFile(fixture("track-choice-a.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 6000);
    player.setAudioTrack(2); // Spanish
    player.setSubtitleTrack(9); // English SDH
    QTRY_COMPARE(selectedId(player, "audio"), 2);
    QTRY_COMPARE(selectedId(player, "sub"), 9);
    preferences.setPreferredAudioLanguage(QStringLiteral("ko"));
    preferences.setPreferredSubtitleLanguage(QStringLiteral("ta"));
    player.openFile(fixture("track-choice-b.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 6000);
    QCOMPARE(selectedId(player, "audio"), 4);
    QCOMPARE(selectedId(player, "sub"), 2);
    settings.resetAllSettings();
    player.openFile(fixture("track-choice-a.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 3, 6000);
    QCOMPARE(selectedId(player, "audio"), 2);
    QCOMPARE(selectedId(player, "sub"), 9);
}

void TestPreferences::subtitleOffWinsOverSavedLanguage()
{
    SettingsStore settings;
    settings.setValue("subtitles/preferredLanguage", QStringLiteral("en"));
    PlayerController player;
    PlaybackPreferences preferences(&settings, &player);
    player.setPaused(true);
    QSignalSpy loaded(&player, &PlayerController::fileLoaded);
    player.openFile(fixture("track-choice-a.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 6000);
    QVERIFY(selectedId(player, "sub") >= 0);
    player.command({"set", "sid", "no"});
    QTRY_COMPARE(selectedId(player, "sub"), -1);
    preferences.setPreferredSubtitleLanguage(QStringLiteral("hi"));
    player.openFile(fixture("track-choice-b.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 6000);
    QCOMPARE(selectedId(player, "sub"), -1);
    settings.resetAllSettings();
    player.openFile(fixture("track-choice-a.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 3, 6000);
    QCOMPARE(selectedId(player, "sub"), -1);
}

void TestPreferences::missingManualMatchKeepsFutureIntent()
{
    SettingsStore settings;
    settings.setValue("audio/preferredLanguage", QStringLiteral("fr"));
    PlayerController player;
    PlaybackPreferences preferences(&settings, &player);
    player.setPaused(true);
    QSignalSpy loaded(&player, &PlayerController::fileLoaded);
    player.openFile(fixture("track-choice-a.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 6000);
    player.setAudioTrack(7);
    QTRY_COMPARE(selectedId(player, "audio"), 7);
    player.openFile(fixture("track-choice-fallback.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 6000);
    QCOMPARE(selectedId(player, "audio"), 1);
    player.openFile(fixture("track-choice-b.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 3, 6000);
    QCOMPARE(selectedId(player, "audio"), 3);
}

void TestPreferences::restartUsesSavedLanguageNotNumericTrackIds()
{
    SettingsStore settings;
    {
        PlayerController player;
        PlaybackPreferences preferences(&settings, &player);
        preferences.setPreferredAudioLanguage(QStringLiteral("ja"));
        player.setPaused(true);
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        player.openFile(fixture("track-choice-a.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 6000);
        player.setAudioTrack(2);
        QTRY_COMPARE(selectedId(player, "audio"), 2);
    }
    SettingsStore reopened;
    PlayerController player;
    PlaybackPreferences preferences(&reopened, &player);
    player.setPaused(true);
    QSignalSpy loaded(&player, &PlayerController::fileLoaded);
    player.openFile(fixture("track-choice-b.mkv"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 6000);
    QCOMPARE(selectedId(player, "audio"), 3); // Japanese, not previous ID 2.
    QCOMPARE(reopened.value("audio/preferredLanguage").toString(), QStringLiteral("ja"));
}

void TestPreferences::screenshotSequenceTemplates()
{
    SettingsStore settings;
    PlayerController player;
    PlaybackPreferences preferences(&settings, &player);
    const QStringList sequenceTemplates{
        "Frame_%n", "Frame_%02n", "Frame_%#n", "Frame_%#02n",
        "Frame_%%%n", "Frame_%f%n", "Frame_%%_%#04n", "Frame_%#%n"};
    const QStringList literalTemplates{
        "Frame_%f_%P", "Frame_%%n", "Frame_%%02n", "Frame_%%#02n",
        "Frame_%%%%n", "Frame_%", "Frame_%#", "Frame_%02", "Frame_%2x"};
    for (const QString &value : sequenceTemplates) {
        settings.setValue("general/screenshotTemplate", value);
        QTRY_COMPARE(player.mpvProperty("screenshot-template").toString(), value);
    }
    for (const QString &value : literalTemplates) {
        settings.setValue("general/screenshotTemplate", value);
        QTRY_COMPARE(player.mpvProperty("screenshot-template").toString(),
                     value + QStringLiteral("_%#02n"));
    }
    settings.setValue("general/screenshotTemplate", QString());
    const QString defaultTemplate = SettingsStore::defaults().value("general/screenshotTemplate").toString();
    QTRY_COMPARE(player.mpvProperty("screenshot-template").toString(), defaultTemplate + QStringLiteral("_%#02n"));
}

void TestPreferences::shaderDisableRemoveAndReset()
{
    SettingsStore settings;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList paths{directory.filePath("first shader;one.glsl"), directory.filePath("two, hook.hook")};
    for (const QString &path : paths) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("// Frame preference test shader\n");
    }
    // A pre-coordinator profile may contain native Windows separators and
    // duplicate picker paths. Those must remain removable after upgrade.
    {
        QSettings legacy(QSettings::IniFormat, QSettings::UserScope, "Frame", "Frame");
        legacy.setValue("shaders/applied", QStringList{QDir::toNativeSeparators(paths.first()), paths.first()});
        legacy.sync();
    }
    QCOMPARE(settings.appliedShaders(), QStringList{paths.first()});
    settings.setAppliedShaders(paths);
    PlayerController player;
    PlaybackPreferences preferences(&settings, &player);
    QTRY_COMPARE(player.mpvProperty("glsl-shaders").toStringList(), paths);
    preferences.setShadersEnabled(false);
    QTRY_VERIFY(player.mpvProperty("glsl-shaders").toList().isEmpty());
    QCOMPARE(settings.appliedShaders(), paths);
    // Preserve the current main.qml picker workflow even while disabled.
    player.loadShader(paths.first());
    settings.setAppliedShaders(paths);
    QTRY_VERIFY(player.mpvProperty("glsl-shaders").toList().isEmpty());
    preferences.removeShader(paths.first());
    QCOMPARE(preferences.shaders(), QStringList{paths.last()});
    preferences.setShadersEnabled(true);
    QTRY_COMPARE(player.mpvProperty("glsl-shaders").toStringList(), QStringList{paths.last()});
    // Re-selecting an already saved shader must not leave duplicates.
    player.loadShader(paths.last());
    settings.setAppliedShaders(settings.appliedShaders());
    QTRY_COMPARE(player.mpvProperty("glsl-shaders").toStringList(), QStringList{paths.last()});
    preferences.resetShaders();
    QVERIFY(preferences.shadersEnabled());
    QVERIFY(preferences.shaders().isEmpty());
    QTRY_VERIFY(player.mpvProperty("glsl-shaders").toList().isEmpty());
    settings.setAppliedShaders(paths);
    QTRY_COMPARE(player.mpvProperty("glsl-shaders").toStringList(), paths);
    settings.resetAllSettings();
    QVERIFY(preferences.shadersEnabled());
    QVERIFY(preferences.shaders().isEmpty());
    QTRY_VERIFY(player.mpvProperty("glsl-shaders").toList().isEmpty());
    for (const QString &path : paths) QVERIFY(QFileInfo::exists(path));
}

QTEST_MAIN(TestPreferences)
#include "test_preferences.moc"
