#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include "core/FileLogger.h"
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantMap>
#include <cmath>
#include <memory>
#include <algorithm>

#include "core/MpvRenderItem.h"
#include "core/PlayerController.h"
#include "core/ReleaseChecker.h"
#include "core/SingleInstance.h"
#include "core/FontWarmup.h"
#include "core/GuiStallMonitor.h"
#include "core/SettingsStore.h"
#include "core/PlaylistSession.h"
#include "core/PlaybackPreferences.h"
#include "platform/SystemMediaControls.h"
#include "core/ThumbnailGenerator.h"
#include "core/AudioOverview.h"
#include "core/ThumbnailPosterProvider.h"
#include "core/PerformanceTrace.h"
#include "core/PerformanceApplication.h"
#include "core/InterfaceFonts.h"
#include "core/GraphicsBootstrap.h"
#include "core/MediaNameFormatter.h"
#include "input/MenuBuilder.h"
#include "input/ShortcutManager.h"
#include "platform/NativeWindowFactory.h"

namespace {

// Serves the latest captured thumbnail to QML via "image://preview". The
// QML side bumps the image ID (a counter in the URL) on each thumbnailReady
// so the Image item re-requests; the provider just returns the latest QImage.
class PreviewImageProvider final : public QQuickImageProvider {
public:
    PreviewImageProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString &, QSize *size, const QSize &) override
    {
        const QMutexLocker lock(&m_mutex);
        // The worker already scales captures to the requested preview width;
        // smooth-scaling here would run on the GUI thread for no benefit.
        if (size) *size = m_current.size();
        return m_current;
    }

    void setImage(const QImage &image) { const QMutexLocker lock(&m_mutex); m_current = image; }

private:
    QMutex m_mutex;
    QImage m_current;
};

// F41: "90", "90.5", "1:30", "01:30:00.250" -> seconds; -1 on garbage.
// A19: the parser moved to PlayerController so the F6b go-to-time dialog and
// this flag can never diverge on what a time looks like.
double parseTimeSpec(const QString &spec)
{
    return PlayerController::parseTimeSpec(spec);
}

// Case-insensitive, separator-insensitive file path comparison (mpv echoes
// the path it was given, so compare against the canonical absolute form).
bool sameFilePath(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    return QFileInfo(a).absoluteFilePath().compare(
               QFileInfo(b).absoluteFilePath(), Qt::CaseInsensitive) == 0;
}

// Frame rename: on first launch under the new identity, silently pull the
// old GDPlayer install's state across so recents, resume points, bookmarks,
// rebound shortcuts, window geometry and volume all survive. Runs once,
// before SettingsStore or PlayerController touch either file — SettingsStore
// is constructed AFTER PlayerController (main.cpp), and PlayerController's
// initMpv() reads input.conf immediately, so this cannot live in either
// object's constructor; it has to be an explicit step ahead of both.
//
// QSettings(format, scope, org, app) resolves a path without needing the
// global QGuiApplication org/app identity, which is how the OLD ("GDPlayer")
// and NEW ("Frame") locations can both be addressed here even though the
// app has already announced itself as "Frame" by the time this runs.
void migrateSettingsFromGDPlayer()
{
    const QSettings newSettings(QSettings::IniFormat, QSettings::UserScope,
                                QStringLiteral("Frame"), QStringLiteral("Frame"));
    // Already migrated (or a fresh Frame install with its own state) —
    // never overwrite. This alone is enough to make the migration run-once;
    // the marker below only guards the copy step itself, in case the INI
    // exists but is empty (e.g. QSettings touched it without writing keys).
    if (QFile::exists(newSettings.fileName()))
        return;

    const QSettings oldSettings(QSettings::IniFormat, QSettings::UserScope,
                                QStringLiteral("GDPlayer"), QStringLiteral("GDPlayer"));
    if (!QFile::exists(oldSettings.fileName()))
        return;   // nothing to migrate

    const QString newDir = QFileInfo(newSettings.fileName()).absolutePath();
    if (!QDir().mkpath(newDir)) {
        qWarning("migrate: could not create %ls", qUtf16Printable(newDir));
        return;
    }

    // Copy, never move: the old install stays untouched as a fallback if
    // something goes wrong reading the copy back.
    if (!QFile::copy(oldSettings.fileName(), newSettings.fileName())) {
        qWarning("migrate: failed to copy %ls -> %ls",
                 qUtf16Printable(oldSettings.fileName()),
                 qUtf16Printable(newSettings.fileName()));
        return;
    }

    // F11: the per-user input.conf override sits beside the INI, not inside
    // it, so it needs its own copy. Its absence is normal (most users never
    // hand-edit bindings) and not an error.
    const QString oldInputConf = QFileInfo(oldSettings.fileName()).absolutePath()
                                 + QStringLiteral("/input.conf");
    const QString newInputConf = newDir + QStringLiteral("/input.conf");
    if (QFile::exists(oldInputConf) && !QFile::exists(newInputConf)) {
        if (!QFile::copy(oldInputConf, newInputConf))
            qWarning("migrate: failed to copy input.conf (non-fatal)");
    }

    // Marks the copy as complete so a partially-written INI (crash mid-copy,
    // disk full) is never mistaken for a finished migration on the next
    // launch — QFile::copy above is all-or-nothing, but this is what a
    // future re-import policy would key on, and it costs one line now.
    QSettings migrated(newSettings.fileName(), QSettings::IniFormat);
    migrated.setValue(QStringLiteral("migratedFromGDPlayer"), true);
    migrated.sync();

    qInfo("migrate: copied settings from GDPlayer to Frame (%ls)",
          qUtf16Printable(newSettings.fileName()));
}

} // namespace

int main(int argc, char *argv[])
{
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    // BUG-3 / F44 residual. Qt's DEFAULT Windows font database is the
    // DirectWrite one, and populating it opens every installed font file:
    // measured here at 2,012 mapped files / 613 MB of mapped sections / ~4,100
    // handles, swept progressively over ~1.9 s starting during QML load. That
    // is the whole memory gap F44 was chasing — and it is NOT libass, which
    // this build already pins to `sub-font-provider=none` and which logs
    // "Setting up fonts... Done." in 1 ms.
    //
    // Measured on the same clip, same launch, single probe at t+7s:
    //   default (directwrite)  2012 files / 613.4 MB   WS 574 MB   hnd 4959
    //   fontengine=gdi            1 file  /  16.0 MB   WS 397 MB   hnd  850
    //   fontengine=freetype       0 files /   0.0 MB   WS 396 MB   hnd  852
    //
    // Left OFF by default because it changes how every glyph in the UI is
    // rasterized, which is a look decision and not mine to make silently.
    // Set FRAME_FONT_ENGINE=gdi (or freetype) to take the ~177 MB. Must
    // happen before QGuiApplication: the platform plugin reads this at
    // construction.
    // Windows-only: "windows:fontengine=" is the Windows platform plugin's
    // own option syntax, and the DirectWrite database this works around only
    // exists there. Handing that string to the xcb/wayland/cocoa plugin would
    // break startup, so it never leaves this guard.
    //
    // Interface pane "Font rendering": FRAME_FONT_ENGINE stays as an escape
    // hatch for diagnostics, but the panel writes the same choice into
    // settings/interface/fontEngine so it is no longer a hidden env var.
    // QSettings with explicit org/app args needs no QCoreApplication
    // instance to exist yet, so this can run before QGuiApplication below --
    // which it must, since the platform plugin reads QT_QPA_PLATFORM at
    // construction. The env var, if set, always wins (diagnostic override).
#ifdef Q_OS_WIN
    QByteArray fontEngine = qgetenv("FRAME_FONT_ENGINE");
    if (fontEngine.isEmpty()) {
        const QSettings pre(QSettings::IniFormat, QSettings::UserScope,
                            QStringLiteral("Frame"), QStringLiteral("Frame"));
        fontEngine = pre.value(QStringLiteral("settings/interface/fontEngine")).toByteArray();
    }
    // The INI is user-writable; only the two engines the Advanced pane
    // offers may reach the platform plugin's argument string.
    fontEngine = fontEngine.trimmed().toLower();
    if (fontEngine != "gdi" && fontEngine != "freetype")
        fontEngine.clear();
    if (!fontEngine.isEmpty() && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "windows:fontengine=" + fontEngine);
#endif

    // Outlive all application/Qt objects, then drain before static teardown.
    const auto loggingLifetime = qScopeGuard([] { FileLogger::shutdownInstalled(); });
    PerformanceApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Frame"));
    QGuiApplication::setApplicationName(QStringLiteral("Frame"));
    QGuiApplication::setApplicationVersion(QStringLiteral(FRAME_VERSION));

    // Must run before FileLogger::install() (which itself resolves
    // AppDataLocation) and, more importantly, before PlayerController or
    // SettingsStore are constructed below — both read from the Frame
    // location immediately.
    const QStringList earlyArguments = app.arguments();
    const bool selftestRun = std::any_of(earlyArguments.cbegin(), earlyArguments.cend(),
        [](const QString &argument) {
            return argument == QStringLiteral("--selftest") || argument.startsWith(QStringLiteral("--selftest="));
        });
    if (selftestRun)
        QStandardPaths::setTestModeEnabled(true);
    else
        migrateSettingsFromGDPlayer();

    FileLogger::install();
    installInterfaceFonts();
    const qint64 t1 = QDateTime::currentMSecsSinceEpoch();
    qInfo("startup: gui+logger %lld ms", static_cast<long long>(t1 - t0));
    // A55. Record recovered GUI heartbeat gaps without suspending threads.
    // A permanent hang still needs an external debugger/dump for attribution.
    GuiStallMonitor stallMonitor;

    QCommandLineParser parser;
    parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsOptions);
    parser.addHelpOption();
    QCommandLineOption selftestOption(QStringLiteral("selftest"),
                                      QStringLiteral("Verify rendered video and playback progress, then exit."),
                                      QStringLiteral("file"));
    parser.addOption(selftestOption);
    QCommandLineOption requireHwdecOption(QStringLiteral("require-hwdec"),
        QStringLiteral("Also require hardware decoding during --selftest."));
    parser.addOption(requireHwdecOption);
    // F41: start-position policy for the CLI-opened file. Industry default
    // (mpv, MPC-HC) is "play from the start"; resume is opt-in. VLC instead
    // asks via its "Continue playback?" dialog.
    QCommandLineOption startOption(QStringLiteral("start"),
                                   QStringLiteral("Start the CLI-opened file at <time>: ss, mm:ss or hh:mm:ss (fractional last part ok)."),
                                   QStringLiteral("time"));
    QCommandLineOption startFrameOption(QStringLiteral("start-frame"),
                                        QStringLiteral("Start the CLI-opened file at frame <n> (exact seek at the file's fps)."),
                                        QStringLiteral("n"));
    QCommandLineOption resumeOption(QStringLiteral("resume"),
                                    QStringLiteral("Resume the CLI-opened file from its last saved position (default: from the start)."));
    QCommandLineOption pausedOption(QStringLiteral("paused"),
                                    QStringLiteral("Start the CLI-opened file paused."));
    QCommandLineOption noEnqueueOption(QStringLiteral("no-enqueue"),
                                       QStringLiteral("Do not auto-enqueue the CLI-opened file's folder (F16)."));
    parser.addOption(startOption);
    parser.addOption(startFrameOption);
    parser.addOption(resumeOption);
    parser.addOption(pausedOption);
    parser.addOption(noEnqueueOption);
    QCommandLineOption newWindowOption(QStringLiteral("new-window"),
                                       QStringLiteral("Open a separate window even when Frame is already running."));
    parser.addOption(newWindowOption);
    parser.process(app);

    SettingsStore settings;
    std::unique_ptr<INativeWindow> native = createNativeWindow();

    // One window per user: hand the files to the running Frame and exit.
    // Self-tests and the UI probe always stand alone, so they never reach a
    // real session.
    SingleInstance instance;
    const bool standalone = parser.isSet(newWindowOption) || selftestRun
                            || QStandardPaths::isTestModeEnabled()
                            || !settings.value(QStringLiteral("general/singleInstance"), true).toBool();
    if (!standalone) {
        native->allowForegroundHandoff();
        if (SingleInstance::forwardToRunning(parser.positionalArguments(),
                                             !parser.isSet(noEnqueueOption))) {
            qInfo("startup: handed %lld file(s) to the running instance",
                  static_cast<long long>(parser.positionalArguments().size()));
            return 0;
        }
        instance.listen();
    }
    const QString selftestFile = parser.value(selftestOption);
    const bool requireHwdec = parser.isSet(requireHwdecOption);
    // Help/argument errors and forwarding to an existing window need no
    // graphics setup. A new window overlaps driver startup with core/QML.
    GraphicsBootstrap graphicsBootstrap;

    // CLI file open: positional arguments are media to play on launch
    // (frame.exe video.mkv) OR a direct stream URL (resolveOpenTarget()
    // is the same classifier Ctrl+U/Ctrl+V/drag-drop use, so a URL on the
    // command line opens instead of being rejected as a missing file).
    // The result is handed to QML as a context property so it opens
    // through the same path as the Open dialog (folder auto-enqueue,
    // recents, resume). Relative local paths resolve against the caller's
    // working directory.
    PlayerController player;
    PlaybackPreferences preferences(&settings, &player);
    player.setFileDefaults(preferences.fileDefaults());
    QObject::connect(&preferences, &PlaybackPreferences::fileDefaultsChanged, &player, [&] {
        player.setFileDefaults(preferences.fileDefaults());
    });
    PlaylistSession playlist(&player, &settings);
    playlist.restoreIfEmpty(!parser.positionalArguments().isEmpty() || selftestRun);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &playlist, &PlaylistSession::finish);
    QString startupFile;
    const QStringList positionalFiles = parser.positionalArguments();
    for (const QString &arg : positionalFiles) {
        const QString resolved = player.resolveOpenTarget(arg);
        if (!resolved.isEmpty()) {
            startupFile = resolved;
            break;
        }
        qWarning("startup: skipping unopenable argument '%ls'",
                 qUtf16Printable(arg));
    }

    // F41: CLI start policy. --start (time) beats --start-frame if both are
    // given; with neither, the file opens from 0 unless --resume asks for
    // its saved position. MPV_FORMAT-style validation happens at parse.
    double cliStartTime = -1.0;
    qint64 cliStartFrame = -1;
    const bool cliResume = parser.isSet(resumeOption);
    const bool cliPaused = parser.isSet(pausedOption);
    const bool cliEnqueue = !parser.isSet(noEnqueueOption);
    if (parser.isSet(startOption)) {
        cliStartTime = parseTimeSpec(parser.value(startOption));
        if (cliStartTime < 0)
            qWarning("startup: ignoring unparseable --start value '%ls'",
                     qUtf16Printable(parser.value(startOption)));
    } else if (parser.isSet(startFrameOption)) {
        bool ok = false;
        cliStartFrame = parser.value(startFrameOption).toLongLong(&ok);
        if (!ok || cliStartFrame < 0) {
            cliStartFrame = -1;
            qWarning("startup: ignoring unparseable --start-frame value '%ls'",
                     qUtf16Printable(parser.value(startFrameOption)));
        }
    }
    if (cliStartTime >= 0.0 && cliStartFrame >= 0)
        qWarning("startup: --start and --start-frame are mutually exclusive; using --start");

    ShortcutManager shortcuts;
    shortcuts.setPlayer(&player);
    ReleaseChecker releaseChecker(QStringLiteral(FRAME_VERSION));
    MenuBuilder menus;
    // The concrete platform class (native, above) lives entirely behind its
    // factory (A27) — main.cpp never names NativeWindowWin32/Linux/Mac.
    ThumbnailGenerator thumbnails;
    AudioOverview audioOverview;
    MediaNameFormatter mediaNames;

    // Restore the last session's volume before any QML exists, so the OSD
    // slider shows the real value from the first paint (-1 = never saved).
    const double savedVolume = settings.volume();
    if (savedVolume >= 0.0)
        player.setVolume(savedVolume);

    const QString userConf = QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/../input.conf"));
    QStringList menuFiles{QCoreApplication::applicationDirPath() + QStringLiteral("/input.conf")};
    if (QFile::exists(userConf))
        menuFiles.append(userConf);
    menus.watchFiles(menuFiles);

    // F11 (A22): input.conf is the default table; the user's overrides are
    // shadowed on top of it through mpv's own `keybind`. Both live here
    // rather than in QML because the bindings must be right before the first
    // key can arrive, which is earlier than the engine loads.
    shortcuts.setDefaultBindings(menus.bindingTable(menuFiles));
    shortcuts.applyOverrides(settings.shortcutOverrides());

    bool startupPolicyApplied = false;
    QObject::connect(&player, &PlayerController::fileLoaded, &settings,
                     [&](const QString &path) {
                         qInfo("fileLoaded signal: %ls", qUtf16Printable(path));
                         if (path.isEmpty())
                             return;
                         settings.addRecent(path);
                         // F41: the CLI-opened file follows the --start /
                         // --start-frame / --resume policy; every other
                         // playlist entry keeps the classic resume behavior.
                         const bool isStartupFile = !startupPolicyApplied && sameFilePath(path, startupFile);
                         if (isStartupFile)
                             startupPolicyApplied = true;
                         if (isStartupFile && cliStartTime >= 0.0) {
                             qInfo("cli-start: '%ls' at %.3fs (exact)",
                                   qUtf16Printable(path), cliStartTime);
                             player.seek(cliStartTime);
                         } else if (isStartupFile && cliStartFrame >= 0) {
                             // fileLoaded includes the asynchronous startup fps snapshot.
                             const double fps = player.containerFps();
                             if (fps > 0.0) {
                                 const double t = cliStartFrame / fps;
                                 qInfo("cli-start: '%ls' at frame %lld (%.3fs @ %.3f fps, exact)",
                                       qUtf16Printable(path), cliStartFrame, t, fps);
                                 player.seek(t);
                             } else {
                                 qWarning("cli-start: --start-frame needs a known fps; starting at 0");
                             }
                         } else if (isStartupFile && !cliResume) {
                             // Default: play from the start (mpv-style), even
                             // when an old resume point exists. The saved
                             // point is kept for future --resume / UI opens.
                             qInfo("cli-start: '%ls' from the start (resume point kept, not applied)",
                                   qUtf16Printable(path));
                         } else if (settings.value(QStringLiteral("general/resumeEnabled"), true).toBool()) {
                             const double resume = settings.resumePosition(path);
                             if (resume > 5.0) {
                                 qInfo("resume: seeking '%ls' to %.1fs",
                                       qUtf16Printable(path), resume);
                                 player.seek(resume);
                             }
                         }
                         if (isStartupFile && cliPaused) {
                             qInfo("cli-start: --paused");
                             player.pause();
                         }
                     });
    QObject::connect(&player, &PlayerController::endOfFile, &settings,
                     [&settings, &player]() {
                         if (!player.currentFile().isEmpty())
                             settings.clearResume(player.currentFile());
                     });
    // The quit handler below saves the resume point, but a crash, a forced
    // shutdown or a power cut never reach it. Checkpoint while playing and
    // on every pause so at most half a minute is lost. Same rules as quit:
    // past the first seconds and not parked at the end.
    auto *resumeCheckpoint = new QTimer(&player);
    resumeCheckpoint->setInterval(30000);
    auto saveResumeCheckpoint = [&settings, &player]() {
        const QString file = player.currentFile();
        const double pos = player.position();
        if (file.isEmpty() || pos <= 5.0 || player.eofReached())
            return;
        if (!settings.value(QStringLiteral("general/resumeEnabled"), true).toBool())
            return;
        settings.saveResume(file, pos);
    };
    QObject::connect(resumeCheckpoint, &QTimer::timeout, &player, saveResumeCheckpoint);
    QObject::connect(&player, &PlayerController::stateChanged, &player,
                     [&player, resumeCheckpoint, saveResumeCheckpoint]() {
                         const auto state = player.state();
                         if (state == PlayerController::State::Playing) {
                             if (!resumeCheckpoint->isActive())
                                 resumeCheckpoint->start();
                         } else {
                             resumeCheckpoint->stop();
                             if (state == PlayerController::State::Paused)
                                 saveResumeCheckpoint();
                         }
                     });

    QQmlApplicationEngine engine;
    const qint64 t2 = QDateTime::currentMSecsSinceEpoch();
    qInfo("startup: core+mpv %lld ms", static_cast<long long>(t2 - t1));
    auto *previewProvider = new PreviewImageProvider();
    engine.addImageProvider(QStringLiteral("preview"), previewProvider);
    auto *posterProvider = new ThumbnailPosterProvider(thumbnails.posterCache());
    engine.addImageProvider(QStringLiteral("poster"), posterProvider);
    engine.addImageProvider(QStringLiteral("audiocover"), new AudioCoverProvider(audioOverview.coverStore()));
    QObject::connect(&thumbnails, &ThumbnailGenerator::thumbnailReady, previewProvider,
                     [previewProvider](const QString &, double, const QImage &image, bool success) {
                         if (success)
                             previewProvider->setImage(image);
                     });
    QObject::connect(&thumbnails, &ThumbnailGenerator::previewInvalidated, &engine,
                     [previewProvider] { previewProvider->setImage({}); });
    const auto updatePreviewPressure = [&player, &thumbnails, &startupFile] {
        const auto state = player.state();
        const bool busy = player.scrubbing() || state == PlayerController::State::Loading
            || state == PlayerController::State::Buffering || state == PlayerController::State::Seeking;
        const bool firstPicturePending = (!startupFile.isEmpty() && player.currentFile().isEmpty())
            || (player.videoWidth() > 0 && !player.videoPresented());
        thumbnails.setPlaybackPressure(firstPicturePending, state == PlayerController::State::Playing, busy);
    };
    QObject::connect(&player, &PlayerController::stateChanged, &thumbnails, updatePreviewPressure);
    QObject::connect(&player, &PlayerController::presentationStateChanged, &thumbnails, updatePreviewPressure);
    QObject::connect(&player, &PlayerController::videoSizeChanged, &thumbnails, updatePreviewPressure);
    QObject::connect(&player, &PlayerController::scrubbingChanged, &thumbnails, updatePreviewPressure);
    updatePreviewPressure();
    qmlRegisterType<MpvRenderItem>("Frame.Media", 1, 0, "MpvItem");
    engine.rootContext()->setContextProperty(QStringLiteral("Player"), &player);
    engine.rootContext()->setContextProperty(QStringLiteral("Playlist"), &playlist);
    engine.rootContext()->setContextProperty(QStringLiteral("Preferences"), &preferences);
    engine.rootContext()->setContextProperty(QStringLiteral("Shortcuts"), &shortcuts);
    engine.rootContext()->setContextProperty(QStringLiteral("Settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("ReleaseChecker"), &releaseChecker);
    engine.rootContext()->setContextProperty(QStringLiteral("Menus"), &menus);
    engine.rootContext()->setContextProperty(QStringLiteral("Native"), native.get());
    engine.rootContext()->setContextProperty(QStringLiteral("Thumbs"), &thumbnails);
    engine.rootContext()->setContextProperty(QStringLiteral("AudioInfo"), &audioOverview);
    mediaNames.setCleanupEnabled(settings.value(QStringLiteral("interface/cleanMediaNames"), true).toBool());
    engine.rootContext()->setContextProperty(QStringLiteral("MediaNames"), &mediaNames);
    engine.rootContext()->setContextProperty(QStringLiteral("MenuFiles"), menuFiles);
    engine.rootContext()->setContextProperty(QStringLiteral("startupFile"), startupFile);
    engine.rootContext()->setContextProperty(QStringLiteral("startupEnqueue"), cliEnqueue);
    engine.rootContext()->setContextProperty(QStringLiteral("selftestActive"), selftestRun);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/Frame/qml/main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    const qint64 t3 = QDateTime::currentMSecsSinceEpoch();
    qInfo("startup: qml loaded %lld ms", static_cast<long long>(t3 - t2));
    qInfo("startup: total to window %lld ms", static_cast<long long>(t3 - t0));

    bool firstFrameLogged = false;
    bool videoSurfaceRequested = false;
    bool chromeRequested = false;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    installPerformanceTrace(window, &player);
    if (window) {
        // All surfaces share the user's canonical edited logo; assets/app.ico
        // is exported from this SVG for executable/installer/shortcut resources.
        // QML's Window type has no icon property in Qt 6, so it is set here.
        window->setIcon(QIcon(QStringLiteral(":/assets/branding/concepts/frame-player-icon-v1.svg")));
        native->attach(window);
        shortcuts.installFilter(window);
        SystemMediaControls::create(&player, window, window);

        // A61: bulk styles() enumeration holds Qt's shared font database
        // lock even from a worker. Local alternating runs reproduced long
        // GUI text-update stalls during this scan. Keep normal lazy font
        // loading; explicit diagnostics may still exercise the old warmup.
        // This does not change the font engine or mpv subtitle font policy.
        if (qEnvironmentVariableIntValue("FRAME_FONT_WARMUP") == 1) {
            auto *fontWarmupTimer = new QTimer(window);
            fontWarmupTimer->setSingleShot(true);
            fontWarmupTimer->setInterval(750);
            const auto fontWarmupStarted = std::make_shared<bool>(false);
            const auto backgroundReady = [&player, &startupFile, window] {
                const auto state = player.state();
                return window->isExposed() && !player.scrubbing()
                    && (player.currentFile().isEmpty() || player.isPaused() || player.eofReached())
                    && state != PlayerController::State::Loading
                    && state != PlayerController::State::Buffering
                    && state != PlayerController::State::Seeking
                    && (startupFile.isEmpty() || !player.currentFile().isEmpty())
                    && (player.currentFile().isEmpty() || player.videoWidth() == 0 || player.videoPresented());
            };
            const auto scheduleFontWarmup = [fontWarmupTimer, backgroundReady, fontWarmupStarted] {
                if (*fontWarmupStarted) return;
                if (!backgroundReady()) fontWarmupTimer->stop();
                else if (!fontWarmupTimer->isActive()) fontWarmupTimer->start();
            };
            QObject::connect(fontWarmupTimer, &QTimer::timeout, window,
                             [backgroundReady, fontWarmupStarted] {
                if (!backgroundReady() || *fontWarmupStarted) return;
                *fontWarmupStarted = true;
                startFontDatabaseWarmup();
            });
            QObject::connect(&player, &PlayerController::stateChanged, window, scheduleFontWarmup);
            QObject::connect(&player, &PlayerController::pauseChanged, window, scheduleFontWarmup);
            QObject::connect(&player, &PlayerController::eofReachedChanged, window, scheduleFontWarmup);
            QObject::connect(&player, &PlayerController::presentationStateChanged, window, scheduleFontWarmup);
            QObject::connect(&player, &PlayerController::scrubbingChanged, window, scheduleFontWarmup);
            QObject::connect(window, &QQuickWindow::frameSwapped, window, scheduleFontWarmup,
                             Qt::SingleShotConnection);
            QObject::connect(window, &QQuickWindow::frameSwapped, window, [fontWarmupStarted, window] {
                QTimer::singleShot(3000, window, [fontWarmupStarted] {
                    if (*fontWarmupStarted) return;
                    *fontWarmupStarted = true;
                    qInfo("fonts: opt-in warmup started on the startup timeout");
                    startFontDatabaseWarmup();
                });
            }, Qt::SingleShotConnection);
        }

        // Keep the machine and display awake only while something plays.
        const auto updateAwake = [&player, &native] {
            const auto state = player.state();
            const bool active = !player.isPaused() && !player.eofReached()
                && (state == PlayerController::State::Playing
                    || state == PlayerController::State::Buffering
                    || state == PlayerController::State::Seeking);
            native->setPlaybackActive(active, player.videoWidth() > 0);
        };
        QObject::connect(&player, &PlayerController::stateChanged, &app, updateAwake);
        QObject::connect(&player, &PlayerController::videoSizeChanged, &app, updateAwake);
        QObject::connect(&player, &PlayerController::pauseChanged, &app, updateAwake);
        QObject::connect(&player, &PlayerController::eofReachedChanged, &app, updateAwake);

        // Files from a later launch open here, through the same path as the
        // command line, and the window comes forward.
        QObject::connect(&instance, &SingleInstance::openRequested, window,
                         [window, &player, &native](const QStringList &files, bool enqueue) {
                             QStringList targets;
                             for (const QString &file : files) {
                                 const QString resolved = player.resolveOpenTarget(file);
                                 if (!resolved.isEmpty())
                                     targets.append(resolved);
                                 else
                                     qWarning("handoff: skipping unopenable '%ls'", qUtf16Printable(file));
                             }
                             if (!targets.isEmpty())
                                 QMetaObject::invokeMethod(window, "openExternal",
                                                           Q_ARG(QVariant, QVariant(targets)),
                                                           Q_ARG(QVariant, QVariant(enqueue)));
                             native->raiseToFront();
                         });
        QObject::connect(window, &QQuickWindow::frameSwapped, &app,
                         [&firstFrameLogged, t0]() {
                             if (firstFrameLogged)
                                 return;
                             firstFrameLogged = true;
                             qInfo("startup: first frame %lld ms",
                                   static_cast<long long>(QDateTime::currentMSecsSinceEpoch() - t0));
                         });
        QObject::connect(window, &QQuickWindow::frameSwapped, &app,
                         [window, &videoSurfaceRequested, &chromeRequested]() {
                             if (videoSurfaceRequested)
                                 return;
                             videoSurfaceRequested = true;
                             qInfo("startup: requesting video surface after initial frame");
                             window->setProperty("startupVideoReady", true);

                             // The OSD, playlist and menus are useful but are
                             // not required for the first native frame. Chrome
                             // construction is now cheap (SVG icons), so a
                             // plain extra event-loop turn is all the head
                             // start the first frame needs.
                             QTimer::singleShot(0, window,
                                                [window, &chromeRequested]() {
                                 if (chromeRequested)
                                     return;
                                 chromeRequested = true;
                                 qInfo("startup: requesting deferred chrome after idle");
                                 window->setProperty("startupChromeReady", true);
                                 // Off the critical path: the first text
                                 // needing a fallback glyph must not freeze
                                 // the window while every font is opened.
                             });
                         });
        // Showing a QML ApplicationWindow during engine.load blocks this
        // thread on the first graphics setup.  Let the event loop present the
        // native frame first, then attach the deferred mpv surface.
        QTimer::singleShot(0, window, [window] { window->show(); });
    }

    // aboutToQuit runs while the event loop is still fully alive, which is
    // the only safe place for the orderly shutdown sequence (HANDOFF §6):
    // settle mpv, persist resume, then drop the platform window before the
    // engine/stack teardown begins.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &player,
                     [&settings, &player, &native, &instance, window, done = false]() mutable {
                         // Destroying the last window below makes Qt quit
                         // again (quitOnLastWindowClosed), so this handler
                         // ran twice per exit: the log showed two
                         // "aboutToQuit" lines 17 ms apart with different
                         // positions, and the resume point was saved twice.
                         if (done)
                             return;
                         done = true;
                         const QString resumeFile = player.currentFile();
                         const double resumePos = player.position();
                         qInfo("aboutToQuit: file=%ls pos=%.3f",
                               qUtf16Printable(resumeFile), resumePos);
                         // Persist session state while the window and mpv are
                         // both still alive.
                         if (window) {
                             QVariantMap geo;
                             geo.insert(QStringLiteral("x"), window->x());
                             geo.insert(QStringLiteral("y"), window->y());
                             geo.insert(QStringLiteral("width"), window->width());
                             geo.insert(QStringLiteral("height"), window->height());
                             // The mini player remembers the normal window
                             // it came from, not its own small rectangle.
                             const QVariantMap restore = window->property("windowRestoreGeometry").toMap();
                             if (restore.value(QStringLiteral("width")).toDouble() > 0)
                                 for (const char *key : {"x", "y", "width", "height"})
                                     geo.insert(QString::fromLatin1(key), restore.value(QString::fromLatin1(key)));
                             settings.setWindowGeometry(geo);
                         }
                         settings.setVolume(player.volume());
                         instance.close();
                         native->setPlaybackActive(false, false);
                         player.prepareForShutdown();
                         // Watching to the end must not save a resume point
                         // at the very tail: the next launch would open the
                         // file parked past EOF instead of at the start.
                         if (!resumeFile.isEmpty() && resumePos > 5.0 && !player.eofReached())
                             settings.saveResume(resumeFile, resumePos);
                         else
                             qWarning("aboutToQuit: skip resume save (file empty, pos<=5, or played to end)");
                         const double checkResume = settings.resumePosition(resumeFile);
                         qInfo("resume verify: %ls -> %.3f",
                               qUtf16Printable(resumeFile), checkResume);
                         if (window) {
                             window->hide();
                             window->destroy();
                         }
                     });

    if (!selftestFile.isEmpty()) {
        auto *audioPlaceholder = window ? window->findChild<QQuickItem *>(QStringLiteral("audioPlaceholder")) : nullptr;
        const auto audioPlaceholderShown = std::make_shared<bool>(false);
        if (audioPlaceholder) {
            QObject::connect(audioPlaceholder, &QQuickItem::visibleChanged, &app,
                             [audioPlaceholder, audioPlaceholderShown] {
                *audioPlaceholderShown |= audioPlaceholder->isVisible();
            });
        }
        // Wait for the actual GL surface instead of guessing a startup delay.
        // Slow drivers must not turn this check into audio-only playback.
        auto *readyTimer = new QTimer(&app);
        readyTimer->setInterval(50);
        QObject::connect(readyTimer, &QTimer::timeout, &app,
                         [&, readyTimer, requireHwdec, audioPlaceholder, audioPlaceholderShown]() {
            auto *surface = window ? window->findChild<MpvRenderItem *>() : nullptr;
            if (!surface || !surface->renderReady())
                return;
            readyTimer->stop();
            // A previous run parked at EOF saved a resume point at the file
            // tail; the selftest must verify playback from a known state, so
            // drop any saved position for the selftest file. SettingsStore
            // keys are normalized absolute paths — the CLI may pass a
            // relative one.
            settings.clearResume(QFileInfo(selftestFile).absoluteFilePath());
            player.openFile(selftestFile);
            QTimer::singleShot(6000, &app, [&player, window, requireHwdec, audioPlaceholder, audioPlaceholderShown]() {
                const QString hwdec = player.hwdecCurrent();
                const bool active = player.state() == PlayerController::State::Playing
                                    || (player.eofReached()
                                        && (player.state() == PlayerController::State::Loading
                                            || player.state() == PlayerController::State::Idle
                                            || player.state() == PlayerController::State::Paused));
                auto *surface = window ? window->findChild<MpvRenderItem *>() : nullptr;
                const bool rendered = surface && surface->hasRenderedVideo();
                const bool advanced = player.position() > 0.1 || player.eofReached();
                const bool hardware = !hwdec.isEmpty() && hwdec != QStringLiteral("no");
                const bool correctPlaceholder = audioPlaceholder && !*audioPlaceholderShown;
                const bool pass = active && rendered && advanced && correctPlaceholder && (!requireHwdec || hardware);
                qInfo("SELFTEST %s: hwdec-current='%s' state=%d eof=%d rendered=%d advanced=%d require-hwdec=%d video-only-ui=%d",
                      pass ? "PASS" : "FAIL", qUtf8Printable(hwdec), static_cast<int>(player.state()),
                      player.eofReached() ? 1 : 0, rendered, advanced, requireHwdec, correctPlaceholder);
                QCoreApplication::exit(pass ? 0 : 4);
            });
        });
        QTimer::singleShot(15000, readyTimer, [readyTimer] {
            if (readyTimer->isActive()) {
                qWarning("SELFTEST FAIL: video surface did not initialize within 15 seconds");
                QCoreApplication::exit(4);
            }
        });
        readyTimer->start();
    }

    return app.exec();
}
