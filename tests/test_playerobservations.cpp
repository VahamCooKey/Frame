#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <memory>

#include "core/PlayerController.h"
#include "core/SettingsStore.h"

class FakeInfoPlayer : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap playbackInfo MEMBER info NOTIFY playbackInfoChanged)
    Q_PROPERTY(QVariantList audioDevices MEMBER devices NOTIFY audioDevicesChanged)
    Q_PROPERTY(bool playbackInfoActive MEMBER active NOTIFY playbackInfoActiveChanged)
    Q_PROPERTY(int videoWidth MEMBER width NOTIFY mediaChanged)
    Q_PROPERTY(int videoHeight MEMBER height NOTIFY mediaChanged)
    Q_PROPERTY(double containerFps MEMBER fps NOTIFY mediaChanged)
    Q_PROPERTY(double estimatedVfFps MEMBER fps NOTIFY mediaChanged)
    Q_PROPERTY(QString hwdecCurrent MEMBER hwdec NOTIFY mediaChanged)
    Q_PROPERTY(QString currentFile MEMBER file NOTIFY mediaChanged)
public:
    QVariantMap info;
    QVariantList devices;
    bool active{false};
    int width{1920}, height{1080}, reads{0};
    double fps{24};
    QString hwdec{QStringLiteral("no")}, file{QStringLiteral("sample.mkv")};
    // Keeps old UI code executable for a before/after diagnostic, and catches
    // regressions that put synchronous reads back into either component.
    Q_INVOKABLE QVariant mpvProperty(const QString &name) {
        ++reads;
        return name == QStringLiteral("audio-device-list") ? QVariant(devices) : info.value(name);
    }
signals:
    void playbackInfoChanged();
    void playbackInfoActiveChanged();
    void audioDevicesChanged();
    void mediaChanged();
};

namespace {
class PreviewProvider : public QQuickImageProvider {
public:
    PreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &, QSize *size, const QSize &) override {
        QImage image(24, 14, QImage::Format_RGB32);
        image.fill(Qt::green);
        if (size) *size = image.size();
        return image;
    }
};
bool copyQml(const QString &directory)
{
    const QDir source(qEnvironmentVariable("FRAME_OBSERVATIONS_QML_DIR", QStringLiteral(FRAME_QML_DIR)));
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
    std::unique_ptr<QObject> result(component.create());
    if (!result) qFatal("UI stub: %s", qPrintable(component.errorString()));
    return result;
}
QVariantList listProperty(QObject *object, const char *name)
{
    const QVariant value = object->property(name);
    return value.metaType() == QMetaType::fromType<QJSValue>()
        ? value.value<QJSValue>().toVariant().toList() : value.toList();
}
QString rowValue(QObject *info, int index)
{
    return listProperty(info, "rows").value(index).toMap().value("value").toString();
}
QVariantMap device(const QString &name, const QString &description)
{
    return {{"name", name}, {"description", description}};
}
}

class TestPlayerObservations : public QObject {
    Q_OBJECT
    QTemporaryDir preferences;
private slots:
    void subtitleMarginTracksPlainTextAndRestores()
    {
        PlayerController player;
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(FRAME_QML_DIR "/SubtitleSafeArea.qml")));
        std::unique_ptr<QObject> safeArea(component.createWithInitialProperties({
            {"player", QVariant::fromValue(&player)}, {"viewportHeight", 360}, {"coveredHeight", 104}}));
        QVERIFY2(safeArea, qPrintable(component.errorString()));
        // Baseline before the file opens: it may select its SRT track on
        // load, and the fallback would already have raised sub-margin-y.
        const bool offsetSupported = player.mpvProperty("sub-margin-y-offset").isValid();
        const auto margin = player.mpvProperty("sub-margin-y");
        QVERIFY(margin.isValid());
        QSignalSpy loaded(&player, &PlayerController::fileLoaded);
        player.openFile(QFileInfo("assets/test/track-choice-a.mkv").absoluteFilePath());
        QTRY_COMPARE(loaded.count(), 1);
        QString plainId;
        for (const auto &entry : player.trackList()) {
            const auto track = entry.toMap();
            if (track.value("type") == "sub" && track.value("codec") == "subrip") {
                plainId = track.value("id").toString();
                break;
            }
        }
        QVERIFY(!plainId.isEmpty());
        // Recent libmpv (the Windows pin) has the transient
        // sub-margin-y-offset; older system builds (Ubuntu 26.04's 0.41) do
        // not, and the safe area raises sub-margin-y from its baseline
        // instead. Either way the subtitle is lifted by the same amount and
        // the user's own position and margin come back afterwards.
        const auto raised = [&]() {
            return offsetSupported ? player.mpvProperty("sub-margin-y-offset").toInt()
                                   : player.mpvProperty("sub-margin-y").toInt() - margin.toInt();
        };
        player.command({"set", "sid", plainId});
        QTRY_VERIFY(safeArea->property("plainTextSelected").toBool());
        QTRY_COMPARE(raised(), 208);
        QCOMPARE(safeArea->property("offsetSupported").toBool(), offsetSupported);
        const auto position = player.mpvProperty("sub-pos");
        safeArea->setProperty("coveredHeight", 0); // hidden controls / theater
        QTRY_COMPARE(raised(), 0);
        QCOMPARE(player.mpvProperty("sub-margin-y"), margin);
        safeArea->setProperty("viewportHeight", 1080);
        safeArea->setProperty("coveredHeight", 104);
        QTRY_COMPARE(raised(), 70);
        QCOMPARE(player.mpvProperty("sub-pos"), position);
        if (offsetSupported)
            QCOMPARE(player.mpvProperty("sub-margin-y"), margin);
        player.command({"set", "sid", "no"});
        QTRY_COMPARE(raised(), 0);
        player.command({"set", "sid", plainId});
        QTRY_COMPARE(raised(), 70);

        QTemporaryDir directory;
        QFile ass(directory.filePath("positioned.ass"));
        QVERIFY(ass.open(QIODevice::WriteOnly));
        ass.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n"
                  "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                  "Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1\n"
                  "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                  "Dialogue: 0,0:00:00.00,0:00:10.00,Default,,0,0,0,,{\\pos(100,200)}Positioned sign\n");
        ass.close();
        player.loadSubtitleFile(ass.fileName());
        QTRY_VERIFY(!safeArea->property("plainTextSelected").toBool());
        QTRY_COMPARE(raised(), 0);
        QCOMPARE(player.mpvProperty("sub-pos"), position);
        QCOMPARE(player.mpvProperty("sub-margin-y"), margin);
        safeArea->setProperty("viewportHeight", 0);
        QCOMPARE(safeArea->property("marginOffset").toInt(), 0);
    }

    void subtitleMarginLeavesOtherTracksAlone()
    {
        QQmlEngine engine;
        auto player = stub(engine, "property var trackList: []; property var lastCommand: []; function command(args) { lastCommand = args; }");
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(FRAME_QML_DIR "/SubtitleSafeArea.qml")));
        std::unique_ptr<QObject> safeArea(component.createWithInitialProperties({
            {"player", QVariant::fromValue(player.get())}, {"viewportHeight", 360}, {"coveredHeight", 104}}));
        QVERIFY2(safeArea, qPrintable(component.errorString()));
        for (const QString &codec : {"ass", "ssa", "hdmv_pgs_subtitle", "dvd_subtitle", "unknown", ""}) {
            player->setProperty("trackList", QVariantList{QVariantMap{{"type", "sub"}, {"selected", true}, {"main-selection", 0}, {"codec", codec}}});
            QVERIFY(!safeArea->property("plainTextSelected").toBool());
            QCOMPARE(safeArea->property("marginOffset").toInt(), 0);
        }
        player->setProperty("trackList", QVariantList{QVariantMap{{"type", "sub"}, {"selected", true}, {"main-selection", 1}, {"codec", "subrip"}}});
        QVERIFY(!safeArea->property("plainTextSelected").toBool()); // secondary track
        player->setProperty("trackList", QVariantList{QVariantMap{{"type", "sub"}, {"selected", true}, {"main-selection", 0}, {"codec", "webvtt"}}});
        QCOMPARE(safeArea->property("marginOffset").toInt(), 208);
        player->setProperty("trackList", QVariantList{});
        QCOMPARE(safeArea->property("marginOffset").toInt(), 0);
        QCOMPARE(listProperty(player.get(), "lastCommand"), QVariantList({"set", "sub-margin-y-offset", "0"}));
    }

    void subtitleNoticeDelaysAndCancels()
    {
        QQuickWindow window;
        window.resize(400, 120);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(FRAME_QML_DIR "/SubtitleLoadingNotice.qml")));
        std::unique_ptr<QObject> notice(component.create());
        QVERIFY2(notice, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(notice.get());
        QVERIFY(item);
        item->setParentItem(window.contentItem());
        item->setWidth(240);
        item->setHeight(38);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QSignalSpy visible(notice.get(), SIGNAL(visibleChanged()));
        notice->setProperty("loading", true);
        QVERIFY(!notice->property("shown").toBool());
        notice->setProperty("loading", false);
        QTest::qWait(220);
        QCOMPARE(visible.count(), 0); // quick completion never flashes
        notice->setProperty("loading", true);
        QTRY_VERIFY(notice->property("shown").toBool());
        notice->setProperty("loading", false);
        QVERIFY(!notice->property("shown").toBool());
        notice->setProperty("loading", true);
        notice->setProperty("loading", false);
        QTest::qWait(220);
        QVERIFY(!notice->property("shown").toBool());
    }

    void initTestCase() {
        QVERIFY(preferences.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
        qputenv("FRAME_MPV_OPTS", "vo=null;ao=null;pause=yes;hwdec=no;sub-auto=no");
    }
    void infoActivationAndFileChanges() {
        PlayerController player;
        const auto video = QFileInfo("assets/test/sample.mkv").absoluteFilePath();
        const auto audio = QFileInfo("assets/test/tone.wav").absoluteFilePath();
        player.openFile(video);
        QTRY_COMPARE(player.currentFile(), video);
        QVERIFY(!player.playbackInfoActive());
        QVERIFY(player.playbackInfo().isEmpty());
        QSignalSpy changed(&player, &PlayerController::playbackInfoChanged);
        player.setPlaybackInfoActive(true);
        QTRY_VERIFY(!player.playbackInfo().value("video-codec").toString().isEmpty());
        QTRY_VERIFY(player.playbackInfo().value("audio-params").toMap().value("samplerate").toInt() > 0);
        QTRY_VERIFY(!changed.isEmpty());
        QCOMPARE(player.playbackInfo().value("video-codec"), player.mpvProperty("video-codec"));
        player.setPlaybackInfoActive(false);
        QVERIFY(player.playbackInfo().isEmpty());
        changed.clear();
        player.setPaused(false);
        player.setVolume(42);
        QTRY_COMPARE(player.volume(), 42.0); // Unobserving info must preserve normal observers.
        QTest::qWait(1100);
        QCOMPARE(changed.size(), 0);
        QVERIFY(player.playbackInfo().isEmpty());
        // Pending notifications from rapid activation must not refill a
        // closed panel or cross the next file's observation generation.
        for (int i = 0; i < 20; ++i) {
            player.setPlaybackInfoActive(true);
            player.setPlaybackInfoActive(false);
        }
        player.setPlaybackInfoActive(true);
        player.openFile(audio);
        QTRY_COMPARE(player.currentFile(), audio);
        QTRY_VERIFY(!player.playbackInfo().value("audio-codec").toString().isEmpty());
        QVERIFY(!player.playbackInfo().contains("video-codec"));
        QVERIFY(!player.playbackInfo().contains("video-dec-params"));
        QVERIFY(!player.playbackInfo().contains("video-params"));
        player.stop();
        QTRY_COMPARE(player.state(), PlayerController::State::Idle);
        QVERIFY(player.playbackInfo().isEmpty());
        player.prepareForShutdown();
        QVERIFY(!player.playbackInfoActive());
        player.setPlaybackInfoActive(true);
        QVERIFY(!player.playbackInfoActive());
        QVERIFY(player.playbackInfo().isEmpty());
    }
    void unavailableAudioClearsObservedValues() {
        PlayerController player;
        player.setPlaybackInfoActive(true);
        player.openFile(QFileInfo("assets/test/sample.mkv").absoluteFilePath());
        QTRY_VERIFY(!player.playbackInfo().value("audio-params").toMap().isEmpty());
        player.command({"set", "aid", "no"});
        QTRY_VERIFY(!player.playbackInfo().contains("audio-params"));
        QTRY_VERIFY(!player.playbackInfo().contains("audio-codec"));
        QVERIFY(!player.playbackInfo().value("video-codec").toString().isEmpty());
    }
    void cachedDevicesSurviveShutdown() {
        PlayerController player;
        QTRY_VERIFY(!player.audioDevices().isEmpty());
        QCOMPARE(player.audioDevices().first().toMap().value("name").toString(), QStringLiteral("auto"));
        const auto snapshot = player.audioDevices();
        player.prepareForShutdown();
        QCOMPARE(player.audioDevices(), snapshot);
    }
    void qmlInfoUsesNotificationsAndCloses() {
        QTemporaryDir directory;
        QVERIFY(copyQml(directory.path()));
        QQmlEngine engine;
        FakeInfoPlayer player;
        player.info = {{"video-codec", "h264"}, {"audio-codec", "aac"},
            {"frame-drop-count", 0}, {"decoder-frame-drop-count", 0}};
        auto shortcuts = stub(engine, "property bool textInputActive: false; property var commandKeys: ({}); function commandForKey(key) { return ''; }");
        auto media = stub(engine, "property bool cleanupEnabled: false; function display(path, clean) { return path; }");
        engine.rootContext()->setContextProperty("Player", &player);
        engine.rootContext()->setContextProperty("Shortcuts", shortcuts.get());
        engine.rootContext()->setContextProperty("MediaNames", media.get());
        QQmlComponent component(&engine, QUrl::fromLocalFile(directory.filePath("DeferredPlaybackInfo.qml")));
        std::unique_ptr<QObject> wrapper(component.create());
        QVERIFY2(wrapper, qPrintable(component.errorString()));
        QVERIFY(!player.active);
        QVERIFY(QMetaObject::invokeMethod(wrapper.get(), "openDialog"));
        QCOMPARE(player.reads, 0);
        QTRY_VERIFY(player.active);
        QObject *panel = wrapper->findChild<QObject *>("playbackInfo");
        QVERIFY(panel);
        QTRY_COMPARE(rowValue(panel, 0), QStringLiteral("h264"));
        QCOMPARE(player.reads, 0);
        player.info["video-codec"] = "hevc";
        player.info["frame-drop-count"] = 7;
        emit player.playbackInfoChanged();
        QTRY_COMPARE(rowValue(panel, 0), QStringLiteral("hevc"));
        QCOMPARE(rowValue(panel, 6), QStringLiteral("7 (display)"));
        player.info.clear();
        emit player.playbackInfoChanged();
        QTRY_COMPARE(rowValue(panel, 0), QStringLiteral("Unavailable"));
        QCOMPARE(rowValue(panel, 4), QStringLiteral("No audio track"));
        QVERIFY(QMetaObject::invokeMethod(wrapper.get(), "closeDialog"));
        QTRY_VERIFY(!player.active);
        QVERIFY(QMetaObject::invokeMethod(wrapper.get(), "openDialog"));
        QTRY_VERIFY(player.active);
        wrapper.reset();
        QVERIFY(!player.active);
        QCOMPARE(player.reads, 0);
    }
    void qmlDeviceRemovalAndReturn() {
        QTemporaryDir directory;
        QVERIFY(copyQml(directory.path()));
        QQmlEngine engine;
        FakeInfoPlayer player;
        SettingsStore settings;
        settings.setValue("audio/device", "wasapi/test-headphones");
        const auto headphones = device("wasapi/test-headphones", "Test headphones");
        player.devices = {device("auto", "Automatic"), headphones, headphones};
        auto shortcuts = stub(engine, "property var commandKeys: ({}); function commandForKey(key) { return ''; }");
        auto prefs = stub(engine, "property string preferredAudioLanguage: ''; property var languageOptions: [];");
        engine.rootContext()->setContextProperty("Player", &player);
        engine.rootContext()->setContextProperty("Settings", &settings);
        engine.rootContext()->setContextProperty("Shortcuts", shortcuts.get());
        engine.rootContext()->setContextProperty("Preferences", prefs.get());
        QQmlComponent component(&engine, QUrl::fromLocalFile(directory.filePath("SettingsAudioPane.qml")));
        std::unique_ptr<QObject> pane(component.create());
        QVERIFY2(pane, qPrintable(component.errorString()));
        QCOMPARE(player.reads, 0);
        QCOMPARE(listProperty(pane.get(), "deviceOptions").size(), 2);
        QVERIFY(pane->property("selectedDeviceAvailable").toBool());
        auto *dropdown = pane->findChild<QObject *>("audioDeviceDropdown");
        QVERIFY(dropdown);
        player.devices = {device("auto", "Automatic")};
        emit player.audioDevicesChanged();
        QTRY_VERIFY(!pane->property("selectedDeviceAvailable").toBool());
        QCOMPARE(settings.value("audio/device").toString(), QStringLiteral("wasapi/test-headphones"));
        QCOMPARE(dropdown->property("value").toString(), QStringLiteral("wasapi/test-headphones"));
        QCOMPARE(listProperty(pane.get(), "deviceOptions").last().toMap().value("label").toString(), QStringLiteral("Unavailable device"));
        player.devices.append(headphones);
        emit player.audioDevicesChanged();
        QTRY_VERIFY(pane->property("selectedDeviceAvailable").toBool());
        QCOMPARE(listProperty(pane.get(), "deviceOptions").last().toMap().value("label").toString(), QStringLiteral("Test headphones"));
        player.devices.clear();
        emit player.audioDevicesChanged();
        QTRY_VERIFY(!pane->property("selectedDeviceAvailable").toBool());
        dropdown->setProperty("value", QStringLiteral("auto"));
        QTRY_VERIFY(pane->property("selectedDeviceAvailable").toBool());
        QCOMPARE(listProperty(pane.get(), "deviceOptions").size(), 1);
        QCOMPARE(player.reads, 0);
        settings.resetSetting("audio/device");
    }
    void qmlHoverExitDragAndReentry() {
        QTemporaryDir directory;
        QVERIFY(copyQml(directory.path()));
        QQmlEngine engine;
        engine.addImageProvider("preview", new PreviewProvider);
        auto player = stub(engine, R"(
            property string currentFile: "sample.mkv"; property real duration: 120;
            property real position: 0; property bool scrubbing: false;
            property bool seekPending: false; property real seekTarget: 0;
            property int seekCount: 0; property real lastSeek: -1;
            property int state: 3; property bool eofReached: false;
            property bool isPaused: false; property bool isMuted: false;
            property real volume: 100; property real speed: 1;
            property string playbackError: ""; property int cacheBuffering: 100;
            property var chapterList: []; property var trackList: [];
            property real containerFps: 24; property real estimatedVfFps: 24;
            property real abLoopA: -1; property real abLoopB: -1;
            signal fileLoaded(string path)
            function seek(seconds) { seekCount++; lastSeek = seconds; seekPending = true; seekTarget = seconds; }
            function reload() { fileLoaded(currentFile); }
        )");
        auto thumbs = stub(engine, R"(
            property int requests: 0; property int clears: 0;
            property string lastFile: ""; property real lastSeconds: 0;
            signal previewInvalidated()
            signal thumbnailReady(string file, real seconds, var image, bool success)
            function requestThumbnail(file, seconds, width) { requests++; lastFile = file; lastSeconds = seconds; }
            function clear() { clears++; previewInvalidated(); }
            function finish() { thumbnailReady(lastFile, lastSeconds, null, true); }
        )");
        auto native = stub(engine, "property bool alwaysOnTop: false; property bool motion: false; function animationsEnabled() { return motion; }");
        auto shortcuts = stub(engine, "property var commandKeys: ({}); function commandForKey(key) { return ''; }");
        SettingsStore settings;
        engine.rootContext()->setContextProperty("Player", player.get());
        engine.rootContext()->setContextProperty("Thumbs", thumbs.get());
        engine.rootContext()->setContextProperty("Native", native.get());
        engine.rootContext()->setContextProperty("Shortcuts", shortcuts.get());
        engine.rootContext()->setContextProperty("Settings", &settings);
        QQuickWindow window;
        window.resize(800, 300);
        QQmlComponent component(&engine, QUrl::fromLocalFile(directory.filePath("OnScreenController.qml")));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *controls = qobject_cast<QQuickItem *>(object.get());
        QVERIFY(controls);
        controls->setParentItem(window.contentItem());
        controls->setSize(QSizeF(800, 300));
        controls->setProperty("pinned", true);
        QVERIFY(QMetaObject::invokeMethod(controls, "showControlsNow"));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *area = controls->findChild<QQuickItem *>("timelineMouseArea");
        auto *preview = controls->findChild<QQuickItem *>("timelinePreviewImage");
        QVERIFY(area && preview);
        const QPoint inside = area->mapToScene(QPointF(area->width() * .3, area->height() / 2)).toPoint();
        const QPoint outside(790, 10);
        QTest::mouseMove(&window, inside);
        QTRY_VERIFY(thumbs->property("requests").toInt() > 0);
        QTest::mouseMove(&window, inside + QPoint(10, 0), 1);
        QTest::mouseMove(&window, outside, 1);
        QTRY_VERIFY(!controls->property("previewActive").toBool());
        const int requestsAfterExit = thumbs->property("requests").toInt();
        QVERIFY(thumbs->property("clears").toInt() > 0);
        QTest::qWait(160);
        QCOMPARE(thumbs->property("requests").toInt(), requestsAfterExit);
        QCOMPARE(player->property("seekCount").toInt(), 0);

        QTest::mouseMove(&window, inside, 1);
        QTRY_VERIFY(thumbs->property("requests").toInt() > requestsAfterExit);
        // A completion already queued for image publication must not win
        // after cancelling and immediately requesting the same position.
        const QVariant samePosition = controls->property("previewMs");
        QVERIFY(QMetaObject::invokeMethod(thumbs.get(), "finish"));
        QVERIFY(QMetaObject::invokeMethod(controls, "cancelPreview"));
        QVERIFY(QMetaObject::invokeMethod(controls, "requestPreview", Q_ARG(QVariant, samePosition)));
        QTest::qWait(20);
        QVERIFY(!preview->property("latestOk").toBool());
        QVERIFY(QMetaObject::invokeMethod(thumbs.get(), "finish"));
        QTRY_VERIFY(preview->property("latestOk").toBool());
        const int clearsAfterCompletion = thumbs->property("clears").toInt();
        QTest::mouseMove(&window, outside, 1);
        QTRY_VERIFY(!controls->property("previewActive").toBool());
        // A completed preview no longer owns the shared worker: leaving it
        // must not cancel poster work started in the meantime.
        QCOMPARE(thumbs->property("clears").toInt(), clearsAfterCompletion);

        QTest::mouseMove(&window, inside, 1);
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, inside);
        QTRY_VERIFY(player->property("scrubbing").toBool());
        const int clearsBeforeDrag = thumbs->property("clears").toInt();
        QTest::mouseMove(&window, outside, 1);
        QTest::qWait(160);
        QVERIFY(controls->property("previewActive").toBool());
        QCOMPARE(thumbs->property("clears").toInt(), clearsBeforeDrag);
        QCOMPARE(player->property("seekCount").toInt(), 0);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, outside);
        QCOMPARE(player->property("seekCount").toInt(), 1);
        QCOMPARE(player->property("lastSeek").toDouble(), 120.0);
        QVERIFY(!player->property("scrubbing").toBool());
        QVERIFY(!controls->property("previewActive").toBool());
        const int afterRelease = thumbs->property("requests").toInt();
        QTest::qWait(160);
        QCOMPARE(thumbs->property("requests").toInt(), afterRelease);

        QTest::mouseMove(&window, inside, 1);
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, inside);
        QVERIFY(player->property("scrubbing").toBool());
        // Hiding a control during a held press cancels its real mouse grab.
        controls->setVisible(false);
        QTRY_VERIFY(!player->property("scrubbing").toBool());
        QVERIFY(!controls->property("previewActive").toBool());
        QCOMPARE(player->property("seekCount").toInt(), 1);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, outside);
        QVERIFY(QMetaObject::invokeMethod(controls, "showControlsNow"));
        QTest::mouseMove(&window, inside, 1);
        QVERIFY(QMetaObject::invokeMethod(thumbs.get(), "finish"));
        QVERIFY(QMetaObject::invokeMethod(player.get(), "reload"));
        QTest::qWait(20);
        QVERIFY(!preview->property("latestOk").toBool());
        QVERIFY(!controls->property("previewActive").toBool());
        controls->setVisible(false);
        native->setProperty("motion", true);
        QQmlComponent wrapperComponent(&engine, QUrl::fromLocalFile(directory.filePath("DeferredOnScreenController.qml")));
        std::unique_ptr<QObject> wrapper(wrapperComponent.create());
        QVERIFY2(wrapper, qPrintable(wrapperComponent.errorString()));
        auto *wrapperItem = qobject_cast<QQuickItem *>(wrapper.get());
        QVERIFY(wrapperItem);
        wrapperItem->setParentItem(window.contentItem());
        wrapperItem->setSize(QSizeF(800, 96));
        wrapper->setProperty("active", true);
        QVERIFY(QMetaObject::invokeMethod(wrapper.get(), "showControlsNow"));
        auto *paintedControls = wrapper->findChild<QQuickItem *>("playbackControls");
        QVERIFY(paintedControls);
        QTRY_COMPARE(paintedControls->opacity(), 1.0);
        QVERIFY(wrapper->property("painting").toBool());
        QVERIFY(QMetaObject::invokeMethod(wrapper.get(), "hideNow"));
        QVERIFY(!wrapper->property("shown").toBool());
        QVERIFY(wrapper->property("painting").toBool()); // preserve margin during fade
        QTRY_VERIFY(!wrapper->property("painting").toBool());
        QVERIFY(QMetaObject::invokeMethod(wrapper.get(), "showControlsNow"));
        QVERIFY(wrapper->property("painting").toBool());
    }
};

QTEST_MAIN(TestPlayerObservations)
#include "test_playerobservations.moc"
