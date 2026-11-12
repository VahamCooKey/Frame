#include "core/PlayerController.h"

#include "core/ClipboardCapture.h"

#include <QCoreApplication>
#include <QCollator>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMetaObject>
#include <QSet>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace {

constexpr const char *kPlaybackInfoProperties[] = {
    "video-dec-params", "video-params", "audio-params", "video-codec",
    "audio-codec", "frame-drop-count", "decoder-frame-drop-count"
};

QVariantMap selectedTrack(const QVariantList &tracks, const QString &type)
{
    for (const auto &entry : tracks) {
        const auto track = entry.toMap();
        if (track.value(QStringLiteral("type")).toString() == type
            && track.value(QStringLiteral("selected")).toBool()) return track;
    }
    return {};
}

QString trackFeedbackLabel(const QVariantMap &track)
{
    if (track.isEmpty()) return QStringLiteral("Off");
    const auto title = track.value(QStringLiteral("title")).toString().trimmed();
    const auto lang = track.value(QStringLiteral("lang")).toString();
    const auto language = QLocale::codeToLanguage(lang);
    const auto languageName = language == QLocale::C ? lang : QLocale::languageToString(language);
    if (!title.isEmpty()) return languageName.isEmpty() ? title : title + QStringLiteral(" · ") + languageName;
    if (!languageName.isEmpty()) return languageName;
    return QStringLiteral("Track %1").arg(track.value(QStringLiteral("id")).toInt());
}

// For MPV_FORMAT_STRING property events, mpv_event_property::data is a
// char** (pointer to the string pointer), NOT an mpv_node*. Casting it to
// mpv_node* happens to work on some builds only when the bytes following
// the string pointer in mpv's event storage read back as MPV_FORMAT_STRING;
// that is why hwdec-current came back empty (HANDOFF first revision §5.4).
QString stringPropToText(const mpv_event_property *prop)
{
    if (prop->format != MPV_FORMAT_STRING || !prop->data)
        return {};
    const char *value = *static_cast<char *const *>(prop->data);
    return value ? QString::fromUtf8(value) : QString();
}

// Shared by the F16 sibling scan and the F45 clipboard classifier, so
// "what counts as media" is defined exactly once.
bool isMediaSuffix(const QString &suffix)
{
    static const QSet<QString> mediaExtensions{
        QStringLiteral("mkv"),  QStringLiteral("mp4"),  QStringLiteral("m4v"),
        QStringLiteral("avi"),  QStringLiteral("mov"),  QStringLiteral("webm"),
        QStringLiteral("wmv"),  QStringLiteral("flv"),  QStringLiteral("ts"),
        QStringLiteral("m2ts"), QStringLiteral("mpg"),  QStringLiteral("mpeg"),
        QStringLiteral("ogv"),  QStringLiteral("3gp"),  QStringLiteral("mp3"),
        QStringLiteral("m4a"),  QStringLiteral("flac"), QStringLiteral("wav"),
        QStringLiteral("aac"),  QStringLiteral("ogg"),  QStringLiteral("opus"),
    };
    return mediaExtensions.contains(suffix.toLower());
}

bool isStreamScheme(const QString &scheme)
{
    static const QSet<QString> schemes{
        QStringLiteral("http"),   QStringLiteral("https"),  QStringLiteral("rtsp"),  QStringLiteral("rtsps"),
        QStringLiteral("rtmp"),   QStringLiteral("rtmps"),  QStringLiteral("rtmpe"), QStringLiteral("rtmpt"),
        QStringLiteral("rtmpte"), QStringLiteral("rtmpts"), QStringLiteral("mms"),   QStringLiteral("mmsh"),
        QStringLiteral("mmst"),   QStringLiteral("udp"),    QStringLiteral("rtp"),   QStringLiteral("srt"),
        QStringLiteral("ftp"),    QStringLiteral("ftps"),   QStringLiteral("sftp"),  QStringLiteral("smb"),
        QStringLiteral("tcp"),    QStringLiteral("tls"),
    };
    return schemes.contains(scheme.toLower());
}

QString normalizeLocalPath(const QString &path)
{
    if (path.isEmpty())
        return {};

    // Strip file:// to keep storage keys stable and human-readable.
    const QUrl candidate(path);
    if (candidate.isValid() && candidate.isLocalFile())
        return QDir::cleanPath(candidate.toLocalFile());

    // F45: a remote URL is not a path and must survive untouched.
    // QDir::cleanPath collapses the "//" in "https://host" to "https:/host",
    // which reached m_currentFile and therefore the recents list — storing a
    // stream that could never be reopened.
    if (candidate.isValid() && candidate.scheme().size() > 1 && !candidate.host().isEmpty())
        return path;

    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QString loadArgFromPath(const QString &path)
{
    if (path.isEmpty())
        return {};

    const QUrl candidate(path);
    if (candidate.isValid() && candidate.isLocalFile())
        return candidate.toString();

    const QChar drive = path.size() > 1 ? path.at(1) : QChar();
    if (path.size() > 2 && path.at(0).isLetter() && drive == QChar(':')
        && (path.at(2) == QLatin1Char('/') || path.at(2) == QLatin1Char('\\')))
        return QUrl::fromLocalFile(QDir::cleanPath(QDir::fromNativeSeparators(path))).toString();

    return QDir::fromNativeSeparators(path);
}

QVariant nodeToVariant(const mpv_node *node)
{
    if (!node)
        return {};
    switch (node->format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(node->u.string);
    case MPV_FORMAT_FLAG:
        return node->u.flag != 0;
    case MPV_FORMAT_INT64:
        return QVariant::fromValue(node->u.int64);
    case MPV_FORMAT_DOUBLE:
        return node->u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        for (int i = 0; i < node->u.list->num; ++i)
            list.append(nodeToVariant(&node->u.list->values[i]));
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        for (int i = 0; i < node->u.list->num; ++i)
            map.insert(QString::fromUtf8(node->u.list->keys[i]),
                       nodeToVariant(&node->u.list->values[i]));
        return map;
    }
    default:
        return {};
    }
}

} // namespace

void PlayerController::mpvWakeup(void *ctx)
{
    auto *self = static_cast<PlayerController *>(ctx);
    if (self->m_traceEventDelivery)
        self->m_eventWakeups.fetch_add(1, std::memory_order_relaxed);
    self->scheduleMpvEvents();
}

void PlayerController::scheduleMpvEvents()
{
    if (!m_eventDeliveryEnabled.load() || m_eventWakeupQueued.exchange(true))
        return;
    if (m_traceEventDelivery) m_eventPosts.fetch_add(1, std::memory_order_relaxed);
    if (!QMetaObject::invokeMethod(this, [this] {
            // Clear before draining so a callback at the empty-queue boundary
            // cannot be lost. At most one further GUI invocation is pending.
            m_eventWakeupQueued.store(false);
            processMpvEvents();
        }, Qt::QueuedConnection))
        m_eventWakeupQueued.store(false);
}

PlayerController::PlayerController(QObject *parent)
    : QObject(parent)
{
    initMpv();
    if (m_mpv) {
        m_clipboardCapture = new ClipboardCapture(m_mpv, [this] { return m_nextReplyId++; }, this);
        connect(m_clipboardCapture, &ClipboardCapture::finished,
                this, &PlayerController::screenshotCopied);
    }
    m_seekTimer.setSingleShot(true);
    m_seekTimer.setInterval(5000);
    connect(&m_seekTimer, &QTimer::timeout, this, [this] {
        qWarning("playback: seek did not settle within 5 seconds; showing backend position");
        clearPendingSeek(true);
    });
    m_seekQueryTimer.setSingleShot(true);
    m_seekQueryTimer.setInterval(50);
    connect(&m_seekQueryTimer, &QTimer::timeout, this, &PlayerController::querySeekPosition);
    // Keep the previous one-second info cadence without querying the core
    // from QML. This timer only publishes already-owned observations.
    m_playbackInfoTimer.setSingleShot(true);
    m_playbackInfoTimer.setInterval(1000);
    connect(&m_playbackInfoTimer, &QTimer::timeout, this, &PlayerController::playbackInfoChanged);
}

PlayerController::~PlayerController()
{
    m_stopping = true;
    m_eventDeliveryEnabled.store(false);
    delete m_clipboardCapture;
    m_clipboardCapture = nullptr;
    if (m_mpv) {
        if (m_loadHookPending) {
            const char *args[] = {"stop", nullptr};
            m_shutdownStopConfirmed = mpv_command(m_mpv, args) >= 0;
            continueLoadHook();
        }
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
    }
}

QString PlayerController::defaultInputConfPath() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/input.conf");
}

QString PlayerController::userInputConfPath() const
{
    // Same per-user dir as the log dir and main.cpp's MenuFiles entry:
    // %APPDATA%\Frame (AppDataLocation itself ends in Frame/Frame).
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                           + QStringLiteral("/../input.conf"));
}

void PlayerController::initMpv()
{
    m_mpv = mpv_create();
    if (!m_mpv) {
        m_lastError = QStringLiteral("Failed to create mpv handle");
        emit errorOccurred(m_lastError);
        return;
    }

    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "gpu-api", "opengl");
    // Diagnostic override: FRAME_HWDEC=no / auto-copy / d3d11va ... lets a
    // fullscreen-blanking isolation test run without a rebuild. Unset = auto.
    {
        const QString hwdecOverride = qEnvironmentVariable("FRAME_HWDEC");
        mpv_set_option_string(m_mpv, "hwdec",
                              hwdecOverride.isEmpty() ? "auto"
                                                      : hwdecOverride.toUtf8().constData());
    }
    mpv_set_option_string(m_mpv, "idle", "yes");
    // Advance through the queue; retain the last frame only at its end.
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "osd-level", "0");
    mpv_set_option_string(m_mpv, "config", "no");
    // yt-dlp removed (Frame is minimalist by design): direct media URLs
    // (http(s), HLS/DASH, rtsp/srt, Jellyfin links) still open via plain
    // mpv, but page-URL extraction (YouTube/Vimeo) needed mpv's ytdl_hook
    // Lua script, which is no longer worth loading at all.
    mpv_set_option_string(m_mpv, "load-scripts", "no");
    mpv_set_option_string(m_mpv, "ytdl", "no");
    // load-scripts covers script folders only; mpv's built-in Lua scripts
    // have their own switches. Frame draws its own chrome and binds its own
    // keys, so none of them is used, and the core waits for every one to
    // start before it reaches idle (see mpv_wait_event below).
    for (const char *builtin : {"load-stats-overlay", "load-osd-console", "load-auto-profiles",
                                "load-select", "load-positioning", "load-context-menu",
                                "load-commands"})
        mpv_set_option_string(m_mpv, builtin, "no");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(m_mpv, "input-cursor", "no");
    mpv_set_option_string(m_mpv, "video-sync", "display-resample");
    mpv_set_option_string(m_mpv, "dither-depth", "auto");
    mpv_set_option_string(m_mpv, "target-colorspace-hint", "yes");

    // F44: libass builds its DirectWrite font provider the moment a file
    // loads and enumerates every installed font — even for files with no
    // subtitle track at all. Measured on a machine with a large font library
    // that is ~180 MB of working set, ~600 MB of mapped font sections and
    // ~4000 handles, paid on every open. Start both providers off; the
    // subtitle one is armed on demand in armSubtitleFontsIfNeeded() the
    // first time a subtitle track actually goes live. The OSD provider stays
    // off for good: osd-level=0 + osc=no mean mpv never draws OSD text — the
    // whole OSD is QML — so its font provider is pure dead weight.
    mpv_set_option_string(m_mpv, "sub-font-provider", "none");
    mpv_set_option_string(m_mpv, "osd-font-provider", "none");

    const QString userConf = userInputConfPath();
    const QString confPath = QFile::exists(userConf) ? userConf : defaultInputConfPath();
    mpv_set_option_string(m_mpv, "input-conf",
                          QDir::toNativeSeparators(confPath).toUtf8().constData());

    // Diagnostic override, same spirit as FRAME_HWDEC above: a ';'-separated
    // list of raw mpv options applied last, so an isolation test can change
    // core behaviour without a rebuild. Example:
    //   FRAME_MPV_OPTS="sub-font-provider=none;demuxer-max-bytes=32MiB"
    // Unset in normal runs; never read from settings.
    {
        const QString extra = qEnvironmentVariable("FRAME_MPV_OPTS");
        const QStringList parts = extra.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const int eq = part.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            const QByteArray key = part.left(eq).trimmed().toUtf8();
            const QByteArray value = part.mid(eq + 1).trimmed().toUtf8();
            const int rc = mpv_set_option_string(m_mpv, key.constData(), value.constData());
            qInfo("FRAME_MPV_OPTS: %s=%s -> %d", key.constData(), value.constData(), rc);
        }
    }

    if (mpv_initialize(m_mpv) < 0) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        m_lastError = QStringLiteral("Failed to initialize mpv");
        emit errorOccurred(m_lastError);
        return;
    }

    // mpv's core holds playback until every client has read its events once
    // ("Waiting for scripts"), and queue commands sent during that wait were
    // accepted: when it ended mpv treated them as a startup playlist and
    // played entry zero. A restored queue then autoplayed, and a drop that
    // selected entry N briefly started entry zero. With the built-in scripts
    // off, this handle is the only client; reading once here, before anything
    // can be queued, lets the core reach idle ahead of the first command.
    mpv_wait_event(m_mpv, 0);

    mpv_set_wakeup_callback(m_mpv, mpvWakeup, this);
    // info: pipe mpv's own log into the rotating file log; the logger rotates at 5MB.
    // FRAME_MPV_LOG raises it for a diagnostic run without an edit-and-
    // rebuild cycle (the F44 font hunt and the F16 sibling cascade are both
    // only visible at "debug"): mirrors FRAME_HWDEC / FRAME_MPV_OPTS.
    const QByteArray logLevel = qgetenv("FRAME_MPV_LOG");
    mpv_request_log_messages(m_mpv, logLevel.isEmpty() ? "info" : logLevel.constData());
    if (!logLevel.isEmpty())
        qInfo("FRAME_MPV_LOG: mpv log level '%s'", logLevel.constData());
    // A load hook runs before demuxing/playback can advance. Pausing at
    // FILE_LOADED is already too late for audio on a slow first video frame.
    mpv_hook_add(m_mpv, 0, "on_load", 0);
    // Pick matching tracks before decoders/audio start; capture native mpv
    // shortcut changes while the outgoing file's tracks are still available.
    mpv_hook_add(m_mpv, 0, "on_preloaded", 0);
    mpv_hook_add(m_mpv, 0, "on_unload", 0);
    observeProperties();

    // F44 determinism hunt: log the provider state the core actually ended up
    // with, not the one we asked for — the notes' first suspect was a second
    // writer (or an env override) disagreeing with the option set above, and
    // that can only be seen by reading the property back on every launch.
    {
        char *subProv = mpv_get_property_string(m_mpv, "sub-font-provider");
        if (subProv) m_subtitleFontProvider = QString::fromUtf8(subProv);
        char *osdProv = mpv_get_property_string(m_mpv, "osd-font-provider");
        char *subAuto = mpv_get_property_string(m_mpv, "sub-auto");
        qInfo("fonts: init sub-font-provider='%s' osd-font-provider='%s' sub-auto='%s'",
              subProv ? subProv : "?", osdProv ? osdProv : "?", subAuto ? subAuto : "?");
        if (subProv)
            mpv_free(subProv);
        if (osdProv)
            mpv_free(osdProv);
        if (subAuto)
            mpv_free(subAuto);
    }
}

void PlayerController::observeProperties()
{
    mpv_observe_property(m_mpv, 0, "audio-device-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "media-title", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "speed", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "sub-delay", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "sub-visibility", MPV_FORMAT_FLAG);
    // F44: the trigger for arming libass's system font provider. This is
    // current-tracks/sub/id, not sid: sid defaults to the string "auto"
    // (meaning "pick one if there is one"), which is NOT evidence that any
    // subtitle exists — keying off it armed on every file and defeated the
    // whole optimization. current-tracks/sub/id redirects into track-list
    // and only resolves once a track is actually selected; with no subtitle
    // it reports MPV_FORMAT_NONE, which the handler ignores. Observing a
    // property rather than hooking the QML call sites catches every route
    // into a live subtitle — track menu, C/CC, and mpv's own --sub-auto
    // pick on load, which no UI code sees.
    mpv_observe_property(m_mpv, 0, "current-tracks/sub/id", MPV_FORMAT_INT64);
    // F44 determinism hunt: any write to the provider — ours or anything
    // else's — must surface in the log, or a second arming path stays
    // invisible behind the option we set at init.
    mpv_observe_property(m_mpv, 0, "sub-font-provider", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "chapter-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "hwdec-current", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "core-idle", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "idle-active", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "cache-buffering-state", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "seeking", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "playlist", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "playlist-pos", MPV_FORMAT_INT64);
    // A single node delivers a coherent width/height pair when tracks change.
    mpv_observe_property(m_mpv, 0, "video-out-params", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "container-fps", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "audio-delay", MPV_FORMAT_DOUBLE);
    // F6a/F6c. ab-loop-a/b are observed as DOUBLE: when unset mpv reports
    // the string "no", which arrives as MPV_FORMAT_NONE on a DOUBLE
    // observation and is mapped to -1 below.
    mpv_observe_property(m_mpv, 0, "ab-loop-a", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "ab-loop-b", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "estimated-vf-fps", MPV_FORMAT_DOUBLE);
}

void PlayerController::setPlaybackInfoActive(bool active)
{
    if (active && (!m_mpv || m_stopping)) return;
    if (m_playbackInfoActive == active) return;
    m_playbackInfoActive = active;
    if (active) startPlaybackInfoObservation();
    else clearPlaybackInfoObservation();
    emit playbackInfoActiveChanged();
}

void PlayerController::startPlaybackInfoObservation()
{
    if (!m_mpv || m_stopping || !m_playbackInfoActive || !m_presentationFileLoaded
        || m_playbackInfoObservation) return;
    m_playbackInfoObservation = m_nextReplyId++;
    for (const char *name : kPlaybackInfoProperties) {
        const int result = mpv_observe_property(m_mpv, m_playbackInfoObservation, name, MPV_FORMAT_NODE);
        if (result < 0)
            qWarning("playback info: cannot observe %s: %s", name, mpv_error_string(result));
    }
}

void PlayerController::clearPlaybackInfoObservation()
{
    const quint64 previous = m_playbackInfoObservation;
    m_playbackInfoObservation = 0;
    if (m_mpv && previous) mpv_unobserve_property(m_mpv, previous);
    m_playbackInfoTimer.stop();
    if (!m_playbackInfo.isEmpty()) {
        m_playbackInfo.clear();
        emit playbackInfoChanged();
    }
}

void PlayerController::invalidateFileMetadata()
{
    ++m_fileMetadataGeneration;
    m_fileMetadataPending = false;
    m_fileMetadataCommand = 0;
    m_fileMetadataStop = false;
    m_metadataPublished = false;
    m_fileMetadataReplies.fill(0);
    m_fileMetadata.fill(QVariant());
    m_metadataTrackObserved = false;
}

void PlayerController::beginFileMetadata()
{
    invalidateFileMetadata();
    if (m_stopping) return;
    m_fileMetadataPending = true;
    // Requests may complete in any order. IDs are never reused, and clearing
    // this bounded set on START_FILE/END_FILE rejects older file replies.
    constexpr const char *names[] = {"track-list", "sub-visibility", "path",
                                    "current-tracks/video/id", "seekable", "container-fps"};
    static_assert(std::size(names) == MetadataCount);
    for (size_t i = 0; i < m_fileMetadataReplies.size(); ++i) {
        const quint64 id = m_nextReplyId++;
        if (mpv_get_property_async(m_mpv, id, names[i], MPV_FORMAT_NODE) >= 0)
            m_fileMetadataReplies[i] = id;
        else
            qWarning("playback: cannot request startup property %s", names[i]);
    }
    finishFileMetadata(); // Also settles immediate request failures.
}

bool PlayerController::acceptFileMetadataReply(quint64 replyId, const QVariant &value)
{
    if (!replyId || !m_fileMetadataPending) return false;
    const auto found = std::find(m_fileMetadataReplies.begin(), m_fileMetadataReplies.end(), replyId);
    if (found == m_fileMetadataReplies.end()) return false;
    m_fileMetadata[std::distance(m_fileMetadataReplies.begin(), found)] = value;
    *found = 0;
    finishFileMetadata();
    return true;
}

void PlayerController::finishFileMetadata()
{
    if (!m_fileMetadataPending || m_stopping || m_fileMetadataCommand
        || std::any_of(m_fileMetadataReplies.begin(), m_fileMetadataReplies.end(),
                       [](quint64 id) { return id != 0; })) return;
    const quint64 generation = m_fileMetadataGeneration;
    const auto current = [this, generation] {
        return !m_stopping && m_fileMetadataPending && generation == m_fileMetadataGeneration;
    };
    if (!m_metadataPublished) {
        m_initialTracks = m_fileMetadata[Tracks].toList();
        m_initialSubtitlesVisible = m_fileMetadata[SubVisibility].toBool();
        m_haveInitialTracks = m_fileMetadata[Tracks].isValid() && m_fileMetadata[SubVisibility].isValid();
        // An observation may be newer than a snapshot reply. Keep it for UI and
        // user selection; only the initial comparison baseline uses the snapshot.
        if (!m_metadataTrackObserved) {
            m_trackList = m_initialTracks;
            emit trackListChanged();
            if (!current()) return;
        }
        m_seekable = m_fileMetadata[Seekable].toBool();
        const double fps = m_fileMetadata[Fps].toDouble();
        m_containerFps = std::isfinite(fps) && fps > 0 ? fps : 0;
        emit containerFpsChanged();
        if (!current()) return;
        clearLoadPending();
        if (!current()) return;
        m_currentFile = normalizeLocalPath(m_fileMetadata[Path].toString());
        if (m_unavailableFiles.removeAll(m_currentFile)) emit unavailableFilesChanged();
        if (!current()) return;
        m_captureReady = true;
        m_lastEmittedPosition = -1e9;
        m_presentationHasVideo = m_fileMetadata[VideoTrack].toLongLong() > 0;
        m_metadataPublished = true;
        emit fileLoaded(m_currentFile);
        if (!current()) return;
    }
    m_fileMetadataPending = false;
    m_fileMetadata.fill(QVariant());
    // Presentation may have happened while metadata was in flight. Keep the
    // pause hold through fileLoaded's resume/start-position signal receivers.
    if (m_waitingForPresentation && !m_seekPending && (m_videoPresented || !m_presentationHasVideo))
        releasePresentationHold();
}

void PlayerController::updateState()
{
    State s;
    if (m_waitingForPresentation || m_loadPending)
        // Hold Loading from the open request through demuxing and initial
        // presentation, even while an older idle observation is cached.
        s = State::Loading;
    else if (!m_playbackError.isEmpty())
        s = State::Error;
    else if (m_idleActive)
        s = State::Idle;
    else if (m_pausedForCache)
        s = State::Buffering;
    else if (m_seeking)
        s = State::Seeking;
    else if (m_paused)
        // mpv's core-idle is true whenever playback is paused, so the
        // paused test must precede the core-idle test — otherwise Paused
        // is unreachable and the OSD shows "Loading..." on every pause.
        s = State::Paused;
    else if (m_coreIdle)
        // keep-open=always parks the core idle after the last frame; without
        // the eof check that end state showed as a permanent "Loading...".
        s = m_eofReached ? State::Idle : State::Loading;
    else
        s = State::Playing;

    if (s != m_state) {
        m_state = s;
        emit stateChanged();
    }
}

void PlayerController::markLoadPending(quint64 replyId)
{
    m_loadReply = replyId;
    if (m_loadPending)
        return;
    m_loadPending = true;
    updateState();
}

void PlayerController::clearLoadPending()
{
    m_loadReply = 0;
    if (!m_loadPending)
        return;
    m_loadPending = false;
    updateState();
}

void PlayerController::processMpvEvents()
{
    if (!m_mpv || !m_eventDeliveryEnabled.load())
        return;
    if (m_processingEvents) {
        // A direct signal handler may enter a nested Qt event loop. It must
        // not call wait_event while the outer handler uses borrowed data.
        m_eventDrainRequested = true;
        if (m_traceEventDelivery) ++m_eventReentries;
        return;
    }
    {
        QScopedValueRollback processing(m_processingEvents, true);
        m_eventDrainRequested = false;
        drainMpvEvents();
    }
    if (m_finishShutdownPending)
        finishShutdown(); // the current borrowed payload is no longer in use
}

void PlayerController::drainMpvEvents()
{
    constexpr int maxEvents = 128;
    constexpr qint64 budgetNs = 4'000'000;
    QElapsedTimer clock;
    clock.start();
    int processed = 0;
    bool drained = false;
    m_eventQueueDrained = false;

    while (m_eventDeliveryEnabled.load() && !m_finishShutdownPending) {
        // Cooperative budget: an individual handler (including its direct
        // slots) can exceed it. Never yield halfway through borrowed data.
        if (processed >= maxEvents || (processed && clock.nsecsElapsed() >= budgetNs))
            break;
        QElapsedTimer eventClock;
        if (m_traceEventDelivery) eventClock.start();
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        const qint64 waitNs = m_traceEventDelivery ? eventClock.nsecsElapsed() : 0;
        if (event->event_id == MPV_EVENT_NONE) {
            drained = true;
            m_eventQueueDrained = true;
            break;
        }
        ++processed;
        // Opt-in attribution, including time in synchronous signal receivers.
        // Copy labels before handling: nested callbacks may initiate shutdown.
        const int eventId = event->event_id;
        const QString detail = m_traceEventDelivery && eventId == MPV_EVENT_PROPERTY_CHANGE
            && event->data ? QString::fromUtf8(static_cast<mpv_event_property *>(event->data)->name) : QString();
        const auto traceHandler = qScopeGuard([&] {
            if (!m_traceEventDelivery) return;
            const qint64 handlerNs = eventClock.nsecsElapsed() - waitNs;
            if (waitNs >= 16'000'000 || handlerNs >= 16'000'000)
                qInfo("perf: mpv-handler event=%s property=%s wait-ms=%.2f handler-ms=%.2f",
                      mpv_event_name(static_cast<mpv_event_id>(eventId)), qPrintable(detail),
                      waitNs / 1e6, handlerNs / 1e6);
        });

        // Event payloads are borrowed until the next wait. The helper copies
        // raw pixels here, before another event can invalidate their storage.
        if (m_clipboardCapture && m_clipboardCapture->handleEvent(*event))
            continue;

        switch (event->event_id) {
        case MPV_EVENT_HOOK: {
            auto *hook = static_cast<mpv_event_hook *>(event->data);
            if (hook && QByteArray(hook->name) == "on_load") {
                // Apply saved aspect/sync defaults before decoding the next
                // file. Temporary shortcut corrections never leak forward.
                for (auto it = m_fileDefaults.cbegin(); it != m_fileDefaults.cend(); ++it) {
                    const QByteArray key = (QStringLiteral("file-local-options/") + it.key()).toUtf8();
                    const QByteArray value = (it.value().metaType().id() == QMetaType::Double
                        ? QString::number(it.value().toDouble(), 'g', 15) : it.value().toString()).toUtf8();
                    const int result = mpv_set_property_string(m_mpv, key.constData(), value.constData());
                    if (result < 0) qWarning("preferences: %s: %s", key.constData(), mpv_error_string(result));
                }
                ++m_presentationGeneration;
                m_presentationFileLoaded = false;
                m_presentationFrameReady = false;
                m_videoPresented = false;
                m_waitingForPresentation = m_requireVideoPresentation;
                if (m_waitingForPresentation) {
                    // Async writes may execute out of order. Drain an older
                    // unpause before synchronously establishing this hold,
                    // then continue the hook exactly once. Copy the ID; the
                    // event itself is borrowed only until the next wait.
                    m_pauseTarget = true;
                    m_pauseQueryReply = 0;
                    m_loadHookId = hook->id;
                    m_loadHookPending = true;
                    continueLoadHook();
                }
                emit presentationStateChanged();
                updateState();
                if (m_waitingForPresentation) break; // continued above or deferred
            } else if (hook && QByteArray(hook->name) == "on_preloaded") {
                applyTrackPreferences();
            } else if (hook && QByteArray(hook->name) == "on_unload") {
                rememberChangedTracks();
                clearSubtitleRequests();
                invalidateFileMetadata();
            }
            if (hook) mpv_hook_continue(m_mpv, hook->id);
            break;
        }
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto *prop = static_cast<mpv_event_property *>(event->data);
            if (!prop || !prop->name) break;
            const QString name = QString::fromUtf8(prop->name);

            if (std::any_of(std::begin(kPlaybackInfoProperties), std::end(kPlaybackInfoProperties),
                            [&name](const char *key) { return name == QLatin1String(key); })) {
                // A new observation ID on every activation/file excludes
                // events still queued from a closed panel or previous file.
                if (!m_playbackInfoObservation || event->reply_userdata != m_playbackInfoObservation
                    || !m_playbackInfoActive || m_stopping) break;
                const QVariant value = prop->format == MPV_FORMAT_NODE && prop->data
                    ? nodeToVariant(static_cast<mpv_node *>(prop->data)) : QVariant();
                if (m_playbackInfo.value(name) != value) {
                    if (value.isValid()) m_playbackInfo.insert(name, value);
                    else m_playbackInfo.remove(name); // NONE means unavailable, not stale data.
                    if (!m_playbackInfoTimer.isActive()) m_playbackInfoTimer.start();
                }
                break;
            }
            if (name == QStringLiteral("audio-device-list")) {
                const QVariantList devices = prop->format == MPV_FORMAT_NODE && prop->data
                    ? nodeToVariant(static_cast<mpv_node *>(prop->data)).toList() : QVariantList();
                if (devices != m_audioDevices) {
                    m_audioDevices = devices;
                    emit audioDevicesChanged();
                }
                break;
            }

            // F6a: these must be handled BEFORE the MPV_FORMAT_NONE guard
            // below. Clearing an A-B point sets the property to the string
            // "no", which on a DOUBLE observation arrives as FORMAT_NONE —
            // the guard would drop it and leave a stale marker on screen.
            if (name == "ab-loop-a" || name == "ab-loop-b") {
                const double value = (prop->format == MPV_FORMAT_DOUBLE && prop->data)
                                         ? *static_cast<double *>(prop->data)
                                         : -1.0;
                double &slot = (name == "ab-loop-a") ? m_abLoopA : m_abLoopB;
                if (!qFuzzyCompare(slot, value)) {
                    slot = value;
                    emit abLoopChanged();
                }
                break;
            }

            if (name == "current-tracks/sub/id") {
                m_activeSubtitleId = prop->format == MPV_FORMAT_INT64 && prop->data
                    ? *static_cast<int64_t *>(prop->data) : -1;
            }
            if (prop->format == MPV_FORMAT_NONE)
                break;
            if (name == "time-pos") {
                if (prop->format == MPV_FORMAT_DOUBLE) {
                    m_position = prop->data ? *static_cast<double *>(prop->data) : 0.0;
                    // time-pos changes on every rendered frame; repainting the
                    // chrome at 60+ Hz for the time label and 12px playhead is
                    // waste. Store always, announce throttled (~10 Hz). Seeks
                    // and scrub release still land immediately because their
                    // deltas exceed the threshold.
                    if (std::fabs(m_position - m_lastEmittedPosition) >= 0.1) {
                        m_lastEmittedPosition = m_position;
                        emit positionChanged(m_position);
                    }
                }
            } else if (name == "current-tracks/sub/id") {
                // F44: reaching a real track id means libass is about to
                // render text, so it needs real fonts now. Unavailable
                // (no subtitle selected) arrives as MPV_FORMAT_NONE. The
                // determinism hunt needs every event shape in the log — the
                // old silent filter was exactly what made a second trigger
                // invisible.
                if (prop->format == MPV_FORMAT_INT64 && prop->data && m_activeSubtitleId >= 0) {
                    qInfo("fonts: sub track live, id=%lld",
                          static_cast<long long>(*static_cast<int64_t *>(prop->data)));
                    armSubtitleFontsIfNeeded();
                } else {
                    qInfo("fonts: sub track event with format=%d (no live track)",
                          static_cast<int>(prop->format));
                }
            } else if (name == "sub-font-provider") {
                // F44 determinism hunt: the provider flipped while running.
                // String property events carry char** (the same trap the
                // hwdec-current handler documents).
                if (prop->format == MPV_FORMAT_STRING && prop->data) {
                    const char *value = *static_cast<char *const *>(prop->data);
                    m_subtitleFontProvider = QString::fromUtf8(value ? value : "");
                    qInfo("fonts: sub-font-provider changed to '%s'",
                          value ? value : "?");
                }
            } else if (name == "duration") {
                m_duration = prop->data ? *static_cast<double *>(prop->data) : 0.0;
                emit durationChanged(m_duration);
            } else if (name == "pause") {
                m_paused = prop->data ? *static_cast<int *>(prop->data) != 0 : false;
                // Native mpv key bindings can change pause without setPaused.
                // Remember their intent, but never let them bypass startup.
                if (!m_waitingForPresentation && !m_interactionPause) {
                    // A queued EOF seek can still report the old pause state;
                    // it must not overwrite the user's pending Play request.
                    if (!m_pauseReply && !m_pauseQueryReply && !m_frameStepReply && m_frameSteps.isEmpty() && !m_loadHookPending
                        && !m_replaySeekReply && !m_playAppendedStartReply && !m_selectionPlayPending)
                        m_requestedPaused = m_paused;
                }
                else if (!m_paused && !m_pauseReply && !m_pauseQueryReply && !m_frameStepReply && !m_loadHookPending)
                    applyPause(true);
                emit pauseChanged(m_paused);
                updateState();
            } else if (name == "volume") {
                m_volume = prop->data ? *static_cast<double *>(prop->data) : 100.0;
                emit volumeChanged(m_volume);
            } else if (name == "mute") {
                m_muted = prop->data ? *static_cast<int *>(prop->data) != 0 : false;
                emit muteChanged(m_muted);
            } else if (name == "media-title") {
                m_mediaTitle = stringPropToText(prop);
                emit mediaTitleChanged(m_mediaTitle);
            } else if (name == "speed") {
                m_speed = prop->data ? *static_cast<double *>(prop->data) : 1.0;
                emit speedChanged(m_speed);
            } else if (name == "sub-delay") {
                m_subtitleDelay = prop->data ? *static_cast<double *>(prop->data) : 0.0;
                emit subtitleDelayChanged(m_subtitleDelay);
            } else if (name == "track-list") {
                if (m_fileMetadataPending) m_metadataTrackObserved = true;
                const auto tracks = nodeToVariant(prop->data ? static_cast<mpv_node *>(prop->data) : nullptr).toList();
                if (m_trackFeedbackReady && !tracks.isEmpty()) {
                    for (const auto &type : {QStringLiteral("audio"), QStringLiteral("sub")}) {
                        const auto before = selectedTrack(m_trackList, type);
                        const auto after = selectedTrack(tracks, type);
                        if (before.value(QStringLiteral("id")) != after.value(QStringLiteral("id"))) {
                            // Initial observations may arrive after metadata or
                            // PLAYBACK_RESTART. Matching the load baseline is
                            // not a manual choice. Explicit pending commands
                            // are recorded separately in rememberTrackCommand.
                            if (m_haveInitialTracks
                                && after.value(QStringLiteral("id"))
                                    != selectedTrack(m_initialTracks, type).value(QStringLiteral("id")))
                                (type == "audio" ? m_audioTrackTouched : m_subtitleTrackTouched) = true;
                            emit trackSelectionFeedback(type, trackFeedbackLabel(after)
                                + (type == QStringLiteral("sub") && !after.isEmpty() && !m_subtitlesVisible
                                   ? QStringLiteral(" (hidden)") : QString()));
                        }
                    }
                }
                m_trackList = tracks;
                emit trackListChanged();
            } else if (name == "sub-visibility" && prop->data) {
                const bool visible = *static_cast<int *>(prop->data) != 0;
                if (m_trackFeedbackReady && visible != m_subtitlesVisible)
                    emit trackSelectionFeedback(QStringLiteral("sub"), visible
                        ? trackFeedbackLabel(selectedTrack(m_trackList, QStringLiteral("sub"))) : QStringLiteral("Off"));
                m_subtitlesVisible = visible;
                emit subtitleLoadingChanged();
            } else if (name == "chapter-list") {
                m_chapterList = nodeToVariant(prop->data ? static_cast<mpv_node *>(prop->data) : nullptr).toList();
                emit chapterListChanged();
            } else if (name == "eof-reached") {
                m_eofReached = prop->data ? *static_cast<int *>(prop->data) != 0 : false;
                emit eofReachedChanged(m_eofReached);
                updateState();
            } else if (name == "hwdec-current") {
                m_hwdecCurrent = stringPropToText(prop);
                emit hwdecCurrentChanged(m_hwdecCurrent);
            } else if (name == "core-idle") {
                m_coreIdle = prop->data ? *static_cast<int *>(prop->data) != 0 : true;
                updateState();
            } else if (name == "idle-active") {
                m_idleActive = prop->data ? *static_cast<int *>(prop->data) != 0 : true;
                updateState();
            } else if (name == "paused-for-cache") {
                m_pausedForCache = prop->data ? *static_cast<int *>(prop->data) != 0 : false;
                emit cacheBufferingChanged(m_cacheBuffering);
                updateState();
            } else if (name == "cache-buffering-state") {
                m_cacheBuffering = prop->data ? static_cast<int>(*static_cast<int64_t *>(prop->data)) : 0;
                emit cacheBufferingChanged(m_cacheBuffering);
            } else if (name == "seeking") {
                m_seeking = prop->data ? *static_cast<int *>(prop->data) != 0 : false;
                updateState();
            } else if (name == "playlist") {
                m_playlist = nodeToVariant(prop->data ? static_cast<mpv_node *>(prop->data) : nullptr).toList();
                QSet<QString> queuedFiles;
                for (auto &entry : m_playlist) {
                    auto item = entry.toMap();
                    const auto path = normalizeLocalPath(item.value(QStringLiteral("filename")).toString());
                    item.insert(QStringLiteral("filename"), path);
                    entry = item;
                    queuedFiles.insert(path);
                }
                // Current/failed files use local paths too. Keep the UI model
                // consistent with them, and forget errors for removed items.
                if (m_unavailableFiles.removeIf([&](const QString &path) { return !queuedFiles.contains(path); }))
                    emit unavailableFilesChanged();
                // mpv re-emits the full playlist node on every mutation; a
                // multi-file drop would otherwise rebuild and reset the QML
                // model once per file (O(N^2) delegate churn). Coalesce the
                // notifies into one per event-loop turn.
                if (!m_playlistFlushPending) {
                    m_playlistFlushPending = true;
                    QTimer::singleShot(16, this, [this] {
                        m_playlistFlushPending = false;
                        emit playlistChanged();
                    });
                }
            } else if (name == "playlist-pos") {
                m_playlistPos = prop->data ? static_cast<int>(*static_cast<int64_t *>(prop->data)) : -1;
                emit playlistPosChanged(m_playlistPos);
            } else if (name == "video-out-params") {
                const auto params = nodeToVariant(prop->data
                    ? static_cast<mpv_node *>(prop->data) : nullptr).toMap();
                m_videoWidth = params.value(QStringLiteral("dw")).toInt();
                m_videoHeight = params.value(QStringLiteral("dh")).toInt();
                const int rotation = params.value(QStringLiteral("rotate")).toInt();
                if (rotation % 180 != 0)
                    std::swap(m_videoWidth, m_videoHeight);
                emit videoSizeChanged();
            } else if (name == "container-fps") {
                m_containerFps = prop->data ? *static_cast<double *>(prop->data) : 0.0;
                emit containerFpsChanged();
            } else if (name == "audio-delay") {
                m_audioDelay = prop->data ? *static_cast<double *>(prop->data) : 0.0;
                emit audioDelayChanged();
            } else if (name == "estimated-vf-fps") {
                m_estimatedVfFps = prop->data ? *static_cast<double *>(prop->data) : 0.0;
                emit estimatedVfFpsChanged();
            }
            break;
        }
        case MPV_EVENT_START_FILE: {
            clearSubtitleRequests();
            invalidateFileMetadata();
            m_haveInitialTracks = false;
            m_audioTrackTouched = m_subtitleTrackTouched = false;
            m_seekable = false;
            m_trackList.clear();
            emit trackListChanged();
            clearPlaybackInfoObservation();
            m_captureReady = false;
            if (m_clipboardCapture) m_clipboardCapture->invalidate();
            clearPendingSeek();
            setScrubbing(false);
            // START_FILE precedes demuxing and the presentation hook. Keep
            // Loading across that gap, including automatic queue advances.
            markLoadPending(0);
            clearPlaybackError();
            const auto *start = static_cast<mpv_event_start_file *>(event->data);
            const QString loadingPath = start ? fileForPlaylistEntry(start->playlist_entry_id) : QString();
            if (!loadingPath.isEmpty()) m_loadingFile = loadingPath;
            m_trackFeedbackReady = false;
            m_replaySeekReply = 0;
            m_eofReached = false;
            emit eofReachedChanged(false);
            // Playlist navigation and drop-to-play bypass openFile(). Clear
            // the previous geometry before FILE_LOADED can trigger auto-fit.
            m_videoWidth = 0;
            m_videoHeight = 0;
            emit videoSizeChanged();
            break;
        }
        case MPV_EVENT_FILE_LOADED: {
            m_presentationFileLoaded = true;
            startPlaybackInfoObservation();
            beginFileMetadata();
            break;
        }
        case MPV_EVENT_SEEK:
            if (m_seekActive) {
                m_seekStarted = true;
                m_seekRestarted = false;
                m_seekIdleQueryReply = m_seekQueryReply = 0;
                m_seekRestartClock.invalidate();
            }
            break;
        case MPV_EVENT_PLAYBACK_RESTART:
            if (m_seekActive && m_seekStarted) {
                m_seekRestarted = true;
                m_seekRestartClock.start();
            }
            querySeekPosition();
            m_trackFeedbackReady = m_presentationFileLoaded;
            if (!m_requireVideoPresentation) m_selectionPlayPending = false;
            if (m_waitingForPresentation && m_presentationFileLoaded) {
                // mpv has decoded the paused initial/seek frame. Request a
                // redraw even if it was uploaded before this event arrived.
                m_presentationFrameReady = true;
                emit presentationStateChanged();
            }
            break;
        case MPV_EVENT_END_FILE: {
            clearSubtitleRequests();
            invalidateFileMetadata();
            clearPlaybackInfoObservation();
            m_captureReady = false;
            if (m_clipboardCapture) m_clipboardCapture->invalidate();
            clearPendingSeek();
            auto *endFile = static_cast<mpv_event_end_file *>(event->data);
            if (endFile && endFile->reason == MPV_END_FILE_REASON_ERROR && !m_stopReply) {
                const QString path = fileForPlaylistEntry(endFile->playlist_entry_id);
                reportPlaybackError(path.isEmpty() ? m_loadingFile : path);
            }
            clearLoadPending();
            m_trackFeedbackReady = false;
            m_presentationFileLoaded = false;
            if (m_waitingForPresentation) releasePresentationHold();
            if (!endFile) break;
            if (endFile->reason == MPV_END_FILE_REASON_EOF) {
                m_eofReached = true;
                emit eofReachedChanged(true);
                emit endOfFile();
            } else if (endFile->reason == MPV_END_FILE_REASON_ERROR && !m_stopReply) {
                m_lastError = QStringLiteral("Playback failed: %1")
                                  .arg(QString::fromUtf8(mpv_error_string(static_cast<int>(endFile->error))));
                emit errorOccurred(m_lastError);
            }
            break;
        }
        case MPV_EVENT_CLIENT_MESSAGE: {
            auto *msg = static_cast<mpv_event_client_message *>(event->data);
            if (msg->num_args >= 1) {
                const QString name = QString::fromUtf8(msg->args[0]);
                if (name.startsWith(QStringLiteral("app-"))) {
                    QStringList args;
                    for (int i = 1; i < msg->num_args; ++i)
                        args.append(QString::fromUtf8(msg->args[i]));
                    emit appCommand(name, args);
                }
            }
            break;
        }
        case MPV_EVENT_COMMAND_REPLY: {
            finishSubtitleRequest(event->reply_userdata, event->error >= 0);
            if (m_fileMetadataCommand && event->reply_userdata == m_fileMetadataCommand) {
                m_fileMetadataCommand = 0;
                if (event->error >= 0 && m_fileMetadataStop) invalidateFileMetadata();
                else finishFileMetadata(); // Rejection/no-op must keep the current file usable.
            }
            if (m_frameStepReply && event->reply_userdata == m_frameStepReply) {
                m_frameStepReply = 0;
                if (m_loadHookPending) continueLoadHook();
                else if (!m_frameSteps.isEmpty() || !m_pauseTarget || event->error < 0) submitPause();
                else reconcilePause();
            }
            if (m_seekReply && event->reply_userdata == m_seekReply) {
                m_seekReply = 0;
                if (event->error < 0 || !m_seekActive) {
                    m_seekActive = false;
                    if (m_seekQueued) submitSeek();
                    else clearPendingSeek(true);
                } else querySeekPosition();
            }
            if (event->reply_userdata == m_audioTrackReply) m_audioTrackReply = 0;
            if (event->reply_userdata == m_subtitleTrackReply) m_subtitleTrackReply = 0;
            if (m_stopReply && event->reply_userdata == m_stopReply) {
                m_stopReply = 0;
                // A failure already queued before Stop must not restore the
                // recovery view after the user's newer stop request.
                if (event->error >= 0) clearPlaybackError();
            }
            if (m_loadReply != 0 && event->reply_userdata == m_loadReply) {
                m_loadReply = 0;
                if (event->error < 0)
                    clearLoadPending(); // mpv refused the open; nothing will start
            }
            if (m_replaySeekReply && event->reply_userdata == m_replaySeekReply) {
                m_replaySeekReply = 0;
                if (event->error >= 0)
                    applyPause(m_waitingForPresentation || m_interactionPause || m_requestedPaused);
            }
            if (m_playAppendedStartReply != 0 && event->reply_userdata == m_playAppendedStartReply) {
                m_playAppendedStartReply = 0;
                if (event->error >= 0) setPaused(false);
                else m_selectionPlayPending = false;
            }
            if (m_playAppendedReply != 0 && event->reply_userdata == m_playAppendedReply) {
                m_playAppendedReply = 0;
                if (event->error >= 0 && event->data) {
                    const auto *reply = static_cast<mpv_event_command *>(event->data);
                    bool valid = false;
                    const auto id = nodeToVariant(&reply->result).toMap()
                        .value(QStringLiteral("playlist_entry_id")).toLongLong(&valid);
                    m_playAppendedEntry = valid ? id : -1;
                    if (!valid) qWarning("mpv: append did not return a playlist entry ID");
                }
            }
            if (event->error < 0)
                qWarning("mpv: asynchronous command %llu failed: %s",
                         static_cast<unsigned long long>(event->reply_userdata), mpv_error_string(event->error));
            emit commandFinished(static_cast<quint64>(event->reply_userdata),
                                 event->error >= 0,
                                 event->error < 0 ? QString::fromUtf8(mpv_error_string(event->error)) : QString());
            break;
        }
        case MPV_EVENT_SET_PROPERTY_REPLY:
            if (m_subtitleFontReply && event->reply_userdata == m_subtitleFontReply) {
                m_subtitleFontReply = 0;
                if (event->error >= 0) {
                    m_subtitleFontProvider = QStringLiteral("auto");
                    // In the pinned mpv this option has UPDATE_SUB_HARD:
                    // changing it rebuilds the ASS renderer and redecodes
                    // cached packets. Do not sub-reload: that only supports
                    // external tracks and needlessly replaces their IDs.
                }
                finishSubtitleRequest(event->reply_userdata, event->error >= 0);
            }
            if (m_pauseReply && event->reply_userdata == m_pauseReply) {
                m_pauseReply = 0;
                if (event->error < 0)
                    qWarning("playback: pause write failed: %s", mpv_error_string(event->error));
                if (m_loadHookPending) continueLoadHook();
                else if (!m_stopping) {
                    if (!m_frameSteps.isEmpty() || m_pauseTarget != m_pauseSubmitted) submitPause();
                    else reconcilePause();
                }
            }
            break;
        case MPV_EVENT_GET_PROPERTY_REPLY: {
            const auto *metadata = static_cast<mpv_event_property *>(event->data);
            if (acceptFileMetadataReply(event->reply_userdata,
                    event->error >= 0 && metadata && metadata->format == MPV_FORMAT_NODE && metadata->data
                        ? nodeToVariant(static_cast<mpv_node *>(metadata->data)) : QVariant())) break;
            if (m_pauseQueryReply && event->reply_userdata == m_pauseQueryReply) {
                m_pauseQueryReply = 0;
                const auto *prop = static_cast<mpv_event_property *>(event->data);
                if (event->error >= 0 && prop && prop->format == MPV_FORMAT_FLAG && prop->data) {
                    m_paused = *static_cast<int *>(prop->data) != 0;
                    if (!m_waitingForPresentation && !m_interactionPause
                        && !m_replaySeekReply && !m_playAppendedStartReply && !m_selectionPlayPending)
                        m_requestedPaused = m_paused;
                    else if (!m_paused && (m_waitingForPresentation || m_interactionPause))
                        applyPause(true);
                    emit pauseChanged(m_paused);
                    updateState();
                } else {
                    qWarning("playback: pause reconciliation failed: %s", mpv_error_string(event->error));
                }
            } else if (m_seekIdleQueryReply && event->reply_userdata == m_seekIdleQueryReply) {
                m_seekIdleQueryReply = 0;
                const auto *prop = static_cast<mpv_event_property *>(event->data);
                if (event->error >= 0 && prop && prop->format == MPV_FORMAT_FLAG
                    && prop->data && !*static_cast<int *>(prop->data)) {
                    m_seekQueryReply = m_nextReplyId++;
                    if (mpv_get_property_async(m_mpv, m_seekQueryReply, "time-pos", MPV_FORMAT_DOUBLE) < 0) {
                        m_seekQueryReply = 0;
                        clearPendingSeek(true);
                    }
                } else if (m_seekPending) {
                    m_seekQueryTimer.start();
                }
            } else if (m_seekQueryReply && event->reply_userdata == m_seekQueryReply) {
                m_seekQueryReply = 0;
                const auto *prop = static_cast<mpv_event_property *>(event->data);
                if (event->error >= 0 && prop && prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                    const double actual = *static_cast<double *>(prop->data);
                    // A restart or command reply alone can belong to another
                    // seek. Require a fresh, correlated position near this
                    // request after the backend reports that seeking ended.
                    const double tolerance = m_containerFps > 0 ? qMax(.05, 1.5 / m_containerFps) : .05;
                    const double advance = m_seekRestartClock.isValid()
                        ? m_seekRestartClock.elapsed() / 1000.0 * qAbs(m_speed) : 0.0;
                    if (std::isfinite(actual) && ((actual >= m_activeSeekTarget - tolerance
                            && actual <= m_activeSeekTarget + tolerance + advance)
                        || (m_eofReached && m_duration > 0 && m_activeSeekTarget >= m_duration - tolerance))) {
                        m_position = actual;
                        m_lastEmittedPosition = actual;
                        emit positionChanged(actual);
                        m_seekActive = false;
                        if (m_seekQueued) submitSeek();
                        else clearPendingSeek(true);
                    }
                }
                if (m_seekPending) m_seekQueryTimer.start();
            }
            break;
        }
        case MPV_EVENT_LOG_MESSAGE: {
            auto *log = static_cast<mpv_event_log_message *>(event->data);
            const QString level = QString::fromUtf8(log->level ? log->level : "info");
            // mpv terminates every line itself; the file logger adds its
            // own, which left a blank line after each mpv entry.
            QString text = QString::fromUtf8(log->text ? log->text : "");
            while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r')))
                text.chop(1);
            if (level == QStringLiteral("error") || level == QStringLiteral("fatal")
                || level == QStringLiteral("warn"))
                qWarning("mpv: %ls", qUtf16Printable(text));
            else if (level == QStringLiteral("info"))
                qInfo("mpv: %ls", qUtf16Printable(text));
            else
                qDebug("mpv: %ls", qUtf16Printable(text));
            handleScreenshotLog(level, text);
            break;
        }
        case MPV_EVENT_SHUTDOWN:
            m_eventDeliveryEnabled.store(false);
            break;
        case MPV_EVENT_QUEUE_OVERFLOW:
            if (m_traceEventDelivery) ++m_eventOverflows;
            qWarning("mpv: client event queue overflow; notifications were lost");
            break;
        default:
            break;
        }
    }

    // Drain the property and command-reply events first: either can arrive
    // first, and the UI's coalesced playlist notification can lag both.
    // Entry identity also disambiguates duplicate files and rapid drops.
    // Budget yields are not an empty queue. Preserve the append-selection
    // barrier across batches instead of resolving against a partial snapshot.
    if (drained && !m_stopping && m_playAppendedEntry >= 0) {
        for (qsizetype i = 0; i < m_playlist.size(); ++i) {
            if (m_playlist.at(i).toMap().value(QStringLiteral("id")).toLongLong()
                    != m_playAppendedEntry)
                continue;
            m_playAppendedEntry = -1;
            const auto reply = playlistPlayIndex(static_cast<int>(i));
            // mpv explicitly permits asynchronous calls to be reordered.
            // Unpausing an idle queue before selection completes can start
            // entry zero. Wait for this selection's command acknowledgement.
            m_playAppendedStartReply = reply;
            break;
        }
    }

    if (m_traceEventDelivery) {
        ++m_eventBatches;
        m_eventsProcessed += processed;
        if (!drained && !m_finishShutdownPending && m_eventDeliveryEnabled.load())
            ++m_eventBudgetYields;
        m_maxEventBatch = qMax(m_maxEventBatch, processed);
        m_maxEventBatchNs = qMax(m_maxEventBatchNs, clock.nsecsElapsed());
        if (!m_eventReportClock.isValid()) m_eventReportClock.start();
        else if (m_eventReportClock.elapsed() >= 5000) {
            m_eventReportClock.restart();
            qInfo("perf: mpv-events wakeups=%llu posts=%llu batches=%llu events=%llu yields=%llu reentries=%llu overflows=%llu max-events=%d max-ms=%.2f",
                  static_cast<unsigned long long>(m_eventWakeups.load()),
                  static_cast<unsigned long long>(m_eventPosts.load()),
                  static_cast<unsigned long long>(m_eventBatches),
                  static_cast<unsigned long long>(m_eventsProcessed),
                  static_cast<unsigned long long>(m_eventBudgetYields),
                  static_cast<unsigned long long>(m_eventReentries),
                  static_cast<unsigned long long>(m_eventOverflows), m_maxEventBatch,
                  m_maxEventBatchNs / 1e6);
        }
    }
    // mpv may issue just one callback for the whole backlog. A budget yield
    // therefore owes a continuation even without any further callback.
    if (!m_finishShutdownPending && (!drained || m_eventDrainRequested))
        scheduleMpvEvents();
}

void PlayerController::openFile(const QString &filePath)
{
    if (m_stopping) return;
    m_loadingFile = normalizeLocalPath(filePath);
    command({QStringLiteral("loadfile"), loadArgFromPath(filePath)});
    m_eofReached = false;
    m_position = 0.0;
    m_duration = 0.0;
    m_currentFile.clear();
    // Per-file observed properties must not leak in from the previous file.
    // dwidth/dheight 0 = audio-only is meaningful, so reset to 0 and let QML
    // gate on it.
    m_videoWidth = 0;
    m_videoHeight = 0;
    emit videoSizeChanged();
    m_containerFps = 0.0;
    emit containerFpsChanged();
    // m_audioDelay is deliberately NOT reset: mpv's audio-delay is a sticky
    // global option (like sub-delay), so resetting only the cached member
    // would desync the HUD from mpv until the next nudge.
}

QString PlayerController::fileForPlaylistEntry(qint64 id) const
{
    for (const auto &entry : mpvProperty(QStringLiteral("playlist")).toList()) {
        const auto map = entry.toMap();
        if (map.value(QStringLiteral("id")).toLongLong() == id)
            return normalizeLocalPath(map.value(QStringLiteral("filename")).toString());
    }
    return {};
}

void PlayerController::clearPlaybackError()
{
    if (m_playbackError.isEmpty() && m_failedFile.isEmpty()) return;
    m_playbackError.clear();
    m_failedFile.clear();
    emit playbackErrorChanged();
    updateState();
}

void PlayerController::reportPlaybackError(const QString &path)
{
    m_failedFile = path;
    const bool local = !path.contains(QStringLiteral("://"));
    m_playbackError = local && !path.isEmpty() && !QFileInfo::exists(path)
        ? QStringLiteral("This file was moved, renamed or is no longer available.")
        : local ? QStringLiteral("Frame couldn't play this file. It may be damaged or use an unsupported format.")
                : QStringLiteral("Frame couldn't open this stream. Check the address and your connection.");
    if (!path.isEmpty() && !m_unavailableFiles.contains(path)) {
        m_unavailableFiles.append(path);
        emit unavailableFilesChanged();
    }
    m_position = 0;
    m_duration = 0;
    emit positionChanged(0);
    emit durationChanged(0);
    emit playbackErrorChanged();
}

void PlayerController::retryFailedFile()
{
    if (m_failedFile.isEmpty()) return;
    const QString path = m_failedFile;
    for (qsizetype i = 0; i < m_playlist.size(); ++i) {
        if (normalizeLocalPath(m_playlist.at(i).toMap().value(QStringLiteral("filename")).toString()) == path) {
            playlistPlayIndex(static_cast<int>(i));
            return;
        }
    }
    openFile(path);
}

void PlayerController::setInteractionPause(bool held)
{
    if (m_stopping) return;
    if (m_interactionPause == held) return;
    m_interactionPause = held;
    applyPause(held || m_waitingForPresentation || m_requestedPaused);
}

void PlayerController::play()
{
    m_interactionPause = false;
    emit playbackActionRequested(false);
    setPaused(false);
}

void PlayerController::pause()
{
    m_interactionPause = false;
    emit playbackActionRequested(true);
    setPaused(true);
}

void PlayerController::togglePause()
{
    const bool paused = m_eofReached && !m_replaySeekReply ? false
        : !m_requestedPaused;
    emit playbackActionRequested(paused);
    m_interactionPause = false;
    setPaused(paused);
}

void PlayerController::setPaused(bool paused)
{
    if (m_stopping) return;
    if (paused) {
        m_selectionPlayPending = false;
        m_playAppendedStartReply = 0;
        m_replaySeekReply = 0;
    }
    m_requestedPaused = paused;
    if (!paused && m_eofReached && !m_waitingForPresentation) {
        if (!m_replaySeekReply) {
            // Unpausing before this seek completes can immediately pause at
            // EOF again. Wait for its acknowledgement; Pause/Stop/new media
            // cancel the pending unpause.
            m_replaySeekReply = m_nextReplyId;
            command({QStringLiteral("seek"), QStringLiteral("0"), QStringLiteral("absolute+exact")});
        }
        return;
    }
    applyPause(m_waitingForPresentation || m_interactionPause || paused);
}

void PlayerController::applyPause(bool paused)
{
    if (!m_mpv || m_stopping) return;
    m_pauseTarget = paused;
    m_pauseQueryReply = 0; // A newer intent supersedes any read in flight.
    submitPause();
}

void PlayerController::submitPause()
{
    if (m_pauseReply || m_frameStepReply || m_loadHookPending || m_stopping) return;
    if (!m_frameSteps.isEmpty()) {
        const auto args = m_frameSteps.takeFirst();
        m_frameStepReply = m_nextReplyId;
        m_submittingFrameStep = true;
        command(args);
        m_submittingFrameStep = false;
        return;
    }
    m_pauseSubmitted = m_pauseTarget;
    m_pauseReply = m_nextReplyId++;
    int value = m_pauseSubmitted ? 1 : 0;
    const int result = mpv_set_property_async(m_mpv, m_pauseReply, "pause", MPV_FORMAT_FLAG, &value);
    if (result < 0) {
        m_pauseReply = 0;
        qWarning("playback: could not submit pause: %s", mpv_error_string(result));
        reconcilePause();
    }
}

void PlayerController::reconcilePause()
{
    if (m_stopping || m_pauseReply || m_frameStepReply || !m_frameSteps.isEmpty() || m_loadHookPending) return;
    m_pauseQueryReply = m_nextReplyId++;
    const int result = mpv_get_property_async(m_mpv, m_pauseQueryReply, "pause", MPV_FORMAT_FLAG);
    if (result < 0) {
        m_pauseQueryReply = 0;
        qWarning("playback: could not read pause: %s", mpv_error_string(result));
    }
}

void PlayerController::continueLoadHook()
{
    if (!m_loadHookPending || m_continuingLoadHook
        || ((m_pauseReply || m_frameStepReply) && !m_stopping)
        || (m_stopping && !m_shutdownStopConfirmed)) return;
    QScopedValueRollback guard(m_continuingLoadHook, true);
    if (!m_stopping) {
        int paused = 1;
        const int result = mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &paused);
        if (result < 0) {
            // Do not release a load into audible playback without its hold.
            // Stop is synchronous here, while the hook still blocks loading.
            m_lastError = QStringLiteral("Could not pause media before presentation: %1")
                .arg(QString::fromUtf8(mpv_error_string(result)));
            qWarning("playback: %ls", qUtf16Printable(m_lastError));
            emit errorOccurred(m_lastError);
            const char *args[] = {"stop", nullptr};
            if (mpv_command(m_mpv, args) < 0) return;
        }
        qInfo("playback: holding generation %llu for video presentation",
              static_cast<unsigned long long>(m_presentationGeneration));
    }
    const auto id = m_loadHookId;
    m_loadHookPending = false;
    const int result = mpv_hook_continue(m_mpv, id);
    if (result < 0) qWarning("playback: load hook continuation failed: %s", mpv_error_string(result));
    if (!m_stopping) reconcilePause();
}

void PlayerController::releasePresentationHold()
{
    m_waitingForPresentation = false;
    m_presentationFrameReady = false;
    applyPause(m_interactionPause || m_requestedPaused);
    m_selectionPlayPending = false;
    emit presentationStateChanged();
    updateState();
}

void PlayerController::videoFramePresented(quint64 generation)
{
    if (!m_waitingForPresentation || !m_presentationFrameReady || m_seekPending
        || generation != m_presentationGeneration)
        return; // An old queued swap must never start a newer playlist entry.
    qInfo("playback: first frame presented for generation %llu; paused=%d",
          static_cast<unsigned long long>(generation), m_requestedPaused);
    m_videoPresented = true;
    if (!m_fileMetadataPending) releasePresentationHold();
}

void PlayerController::stop()
{
    command({QStringLiteral("stop")});
}

void PlayerController::prepareForShutdown()
{
    clearSubtitleRequests();
    if (!m_mpv || m_stopping)
        return;
    m_stopping = true;
    invalidateFileMetadata();
    if (m_processingEvents) {
        // aboutToQuit can be emitted by a direct event handler. Finish the
        // handler before entering the shutdown pump; do not invalidate its
        // borrowed event or recursively dispatch commands/hooks.
        m_finishShutdownPending = true;
        return;
    }
    finishShutdown();
}

void PlayerController::finishShutdown()
{
    m_finishShutdownPending = false;
    setPlaybackInfoActive(false);
    if (m_clipboardCapture) m_clipboardCapture->close();
    clearPendingSeek();
    setScrubbing(false);

    // Stop playback first: mpv teardown with an active playback core is the
    // known exit-crash trigger (HANDOFF §6).
    // Shutdown is the one synchronous command: it also cancels a file load
    // queued just before close, before the render surface is destroyed.
    const char *stopArgs[] = {"stop", nullptr};
    const int result = mpv_command(m_mpv, stopArgs);
    if (result < 0) qWarning("playback: shutdown stop failed: %s", mpv_error_string(result));
    m_shutdownStopConfirmed = result >= 0;
    continueLoadHook();

    // Let queued mpv events (END_FILE, idle-active) drain through the
    // still-healthy event loop so the state machine settles before any
    // Qt object teardown starts.
    QElapsedTimer timer;
    timer.start();
    do {
        // A reentrant shutdown interrupted the previous batch. Stop need
        // not issue another wakeup while that backlog remains signalled.
        // Resume explicitly after the old borrowed event has unwound.
        processMpvEvents();
        if (m_idleActive && m_eventQueueDrained) break;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    } while (m_eventDeliveryEnabled.load() && timer.elapsed() < 300);

    // Suspend wakeups last: nothing queues processMpvEvents beyond this
    // point; anything already posted is removed by ~QObject or is a no-op
    // once mpv is gone.
    m_eventDeliveryEnabled.store(false);
    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
}

void PlayerController::seek(double seconds)
{
    if (!m_mpv || m_stopping || m_loadPending || m_currentFile.isEmpty() || !std::isfinite(seconds)) return;
    m_seekTarget = qMax(0.0, seconds);
    if (m_duration > 0) m_seekTarget = qMin(m_seekTarget, m_duration);
    m_seekPending = true;
    m_seekQueued = true;
    if (m_waitingForPresentation) {
        // Resume/CLI startup seeks can follow an already displayed initial
        // frame now that metadata is asynchronous. That old frame cannot
        // authorize audio at the requested new start position.
        m_videoPresented = false;
        ++m_presentationGeneration;
        emit presentationStateChanged();
    }
    // Let the active seek settle before dispatching the latest target. Its
    // checks use m_activeSeekTarget, never this newer display target.
    m_seekTimer.start();
    emit seekPendingChanged();
    submitSeek();
}

void PlayerController::seekRelative(double seconds)
{
    // Repeated presses accumulate against the latest requested target.
    seek((m_seekPending ? m_seekTarget : m_position) + seconds);
}

void PlayerController::submitSeek()
{
    if (!m_seekQueued || m_seekActive || m_seekReply || m_stopping) return;
    m_seekQueued = false;
    m_seekActive = true;
    m_seekStarted = m_seekRestarted = false;
    m_seekRestartClock.invalidate();
    m_seekIdleQueryReply = m_seekQueryReply = 0;
    m_activeSeekTarget = m_seekTarget;
    m_seekReply = m_nextReplyId;
    m_submittingSeek = true;
    command({QStringLiteral("seek"), QString::number(m_activeSeekTarget, 'g', 15), QStringLiteral("absolute+exact")});
    m_submittingSeek = false;
}

void PlayerController::querySeekPosition()
{
    if (!m_seekPending || !m_seekActive || !m_seekStarted || !m_seekRestarted
        || m_seekReply || m_seekQueryReply || m_seekIdleQueryReply || m_stopping) return;
    m_seekIdleQueryReply = m_nextReplyId++;
    if (mpv_get_property_async(m_mpv, m_seekIdleQueryReply, "seeking", MPV_FORMAT_FLAG) < 0) {
        m_seekIdleQueryReply = 0;
        clearPendingSeek(true);
    }
}

void PlayerController::clearPendingSeek(bool settled)
{
    m_seekTimer.stop();
    m_seekQueryTimer.stop();
    m_seekQueued = false;
    m_seekActive = m_seekStarted = m_seekRestarted = false;
    m_seekIdleQueryReply = m_seekQueryReply = 0;
    if (!m_seekPending) return;
    m_seekPending = false;
    emit seekPendingChanged();
    if (settled && !m_stopping && !m_seekPending && m_waitingForPresentation) {
        // Reject swaps queued before settlement; ask for a fresh presented
        // frame. Audio-only startup has no frame and can release here.
        m_videoPresented = false;
        ++m_presentationGeneration;
        emit presentationStateChanged();
        if (!m_fileMetadataPending && m_presentationFileLoaded && !m_presentationHasVideo && !m_stopReply)
            releasePresentationHold();
    }
}

void PlayerController::setVolume(double vol)
{
    command({QStringLiteral("set"), QStringLiteral("volume"), QString::number(vol)});
}

void PlayerController::setMuted(bool mute)
{
    command({QStringLiteral("set"), QStringLiteral("mute"), mute ? QStringLiteral("yes") : QStringLiteral("no")});
}

void PlayerController::setSpeed(double speed)
{
    command({QStringLiteral("set"), QStringLiteral("speed"), QString::number(speed)});
}

void PlayerController::setAudioTrack(int trackId)
{
    command({QStringLiteral("set"), QStringLiteral("aid"), QString::number(trackId)});
}

void PlayerController::applyTrackPreferences()
{
    const bool needsMatch = m_audioPreference.mode() == TrackPreference::Mode::Selected
        || m_subtitlePreference.mode() == TrackPreference::Mode::Selected;
    const auto tracks = needsMatch ? mpvProperty(QStringLiteral("track-list")).toList() : QVariantList();
    const auto apply = [this, &tracks](const TrackPreference &preference,
                                     const QString &type, const char *property) {
        if (preference.mode() == TrackPreference::Mode::Unset) return;
        const int match = preference.matchingId(tracks, type);
        const QByteArray value = preference.mode() == TrackPreference::Mode::Off ? QByteArrayLiteral("no")
            : match >= 0 ? QByteArray::number(match) : QByteArrayLiteral("auto");
        // File-local options avoid carrying the new numeric ID into another
        // file. Missing matches use normal selection without forgetting intent.
        const int result = mpv_set_property_string(m_mpv, property, value.constData());
        if (result < 0) qWarning("tracks: preference apply failed: %s", mpv_error_string(result));
    };
    apply(m_audioPreference, QStringLiteral("audio"), "file-local-options/aid");
    apply(m_subtitlePreference, QStringLiteral("sub"), "file-local-options/sid");
    if (m_haveSubtitleVisibilityPreference)
        mpv_set_property_string(m_mpv, "file-local-options/sub-visibility",
                                m_subtitleVisibilityPreference ? "yes" : "no");
}

void PlayerController::rememberChangedTracks()
{
    if (!m_haveInitialTracks) return;
    m_haveInitialTracks = false;
    const auto tracks = mpvProperty(QStringLiteral("track-list")).toList();
    for (const auto &type : {QStringLiteral("audio"), QStringLiteral("sub")}) {
        // A newer menu selection may be queued just before Next. Its recorded
        // intent wins over an older selection still visible to the backend.
        if (type == "audio" ? m_audioTrackReply != 0 : m_subtitleTrackReply != 0) continue;
        const auto before = selectedTrack(m_initialTracks, type);
        const auto after = selectedTrack(tracks, type);
        const bool touched = type == "audio" ? m_audioTrackTouched : m_subtitleTrackTouched;
        if (before.value("id") == after.value("id") && !touched) continue;
        auto &preference = type == "audio" ? m_audioPreference : m_subtitlePreference;
        preference.remember(tracks, type, after.isEmpty() ? -1 : after.value("id").toInt());
    }
    const bool visible = mpvProperty(QStringLiteral("sub-visibility")).toBool();
    if (visible != m_initialSubtitlesVisible) {
        m_haveSubtitleVisibilityPreference = true;
        m_subtitleVisibilityPreference = visible;
    }
}

void PlayerController::rememberTrackCommand(const QStringList &args, quint64 replyId)
{
    if ((!m_haveInitialTracks && !m_fileMetadataPending) || args.size() < 3 || args[0] != "set") return;
    const auto &property = args[1];
    if (property == "aid" || property == "audio" || property == "sid" || property == "sub") {
        const bool audio = property == "aid" || property == "audio";
        auto &preference = audio ? m_audioPreference : m_subtitlePreference;
        bool validId = false;
        const int id = args[2] == "no" ? -1 : args[2].toInt(&validId);
        if ((args[2] == "no" || validId)
            && preference.remember(m_trackList, audio ? QStringLiteral("audio") : QStringLiteral("sub"), id)) {
            (audio ? m_audioTrackReply : m_subtitleTrackReply) = replyId;
            (audio ? m_audioTrackTouched : m_subtitleTrackTouched) = true;
        }
    } else if (property == "sub-visibility") {
        m_haveSubtitleVisibilityPreference = true;
        m_subtitleVisibilityPreference = args[2] == "yes" || args[2] == "true" || args[2] == "1";
    }
}

bool PlayerController::isSubtitleRequest(const QStringList &args)
{
    const QString name = args.value(0);
    if (name == "sub-add" || name == "sub-reload") return true;
    if (name != "set" && name != "cycle" && name != "cycle-values" && name != "add") return false;
    const QString property = args.value(1);
    return property == "sid" || property == "sub" || property == "sub-visibility";
}

void PlayerController::beginSubtitleRequest(quint64 id)
{
    if (!id || m_stopping) return;
    const bool wasLoading = subtitleLoading();
    m_subtitleRequests.insert(id);
    if (wasLoading != subtitleLoading()) emit subtitleLoadingChanged();
}

void PlayerController::finishSubtitleRequest(quint64 id, bool success)
{
    const bool wasLoading = subtitleLoading();
    if (!m_subtitleRequests.remove(id)) return; // stale completion after a file change
    if (wasLoading != subtitleLoading()) emit subtitleLoadingChanged();
    if (!success && !m_stopping) emit subtitleLoadingFailed();
}

void PlayerController::clearSubtitleRequests()
{
    const bool wasLoading = subtitleLoading();
    m_subtitleFontReply = 0;
    m_activeSubtitleId = -1;
    m_subtitleRequests.clear();
    if (wasLoading) emit subtitleLoadingChanged();
}

void PlayerController::armSubtitleFontsIfNeeded()
{
    if (!m_mpv || m_stopping || m_subtitleFontReply || m_subtitleFontProvider != "none") return;
    const quint64 id = m_nextReplyId++;
    m_subtitleFontReply = id;
    beginSubtitleRequest(id);
    // A direct signal receiver may have stopped/closed playback.
    if (m_stopping || m_subtitleFontReply != id) return;
    const char *provider = "auto";
    const int result = mpv_set_property_async(m_mpv, id, "sub-font-provider", MPV_FORMAT_STRING, &provider);
    if (result < 0) {
        m_subtitleFontReply = 0;
        finishSubtitleRequest(id, false);
    }
}

void PlayerController::setSubtitleTrack(int trackId)
{
    command({QStringLiteral("set"), QStringLiteral("sub-visibility"), QStringLiteral("yes")});
    command({QStringLiteral("set"), QStringLiteral("sid"), QString::number(trackId)});
}

void PlayerController::setSubtitleDelay(double seconds)
{
    command({QStringLiteral("set"), QStringLiteral("sub-delay"), QString::number(seconds)});
}

// F45. Arbitrary text (clipboard, drop, argv) -> something worth handing to
// mpv, or empty. Returns a stream URL unchanged and a local media file as an
// absolute path; anything else is rejected so the caller can say so instead
// of letting mpv fail on nonsense.
QString PlayerController::resolveOpenTarget(const QString &text) const
{
    QString candidate = text.trimmed();
    if (candidate.isEmpty())
        return {};

    // Copying a file in Explorer, or from a terminal, commonly yields a
    // quoted path.
    if (candidate.size() > 1
        && ((candidate.startsWith(QLatin1Char('"')) && candidate.endsWith(QLatin1Char('"')))
            || (candidate.startsWith(QLatin1Char('\'')) && candidate.endsWith(QLatin1Char('\''))))) {
        candidate = candidate.mid(1, candidate.size() - 2).trimmed();
    }
    if (candidate.isEmpty() || candidate.contains(QLatin1Char('\n')))
        return {};

    // A Windows drive letter parses as a QUrl with a single-character
    // scheme ("c:/x" -> scheme "c"), so it must be ruled out before any
    // URL handling or every local path is mistaken for a stream.
    const bool drivePath = candidate.size() > 2 && candidate.at(0).isLetter()
                           && candidate.at(1) == QLatin1Char(':')
                           && (candidate.at(2) == QLatin1Char('/')
                               || candidate.at(2) == QLatin1Char('\\'));

    if (!drivePath) {
        const QUrl url(candidate);
        // Requiring a host keeps ordinary prose out: "note: something"
        // parses as a URL with scheme "note" but no host.
        if (url.isValid() && url.scheme().size() > 1 && !url.host().isEmpty()) {
            if (url.isLocalFile()) {
                const QFileInfo info(url.toLocalFile());
                return (info.isFile() && isMediaSuffix(info.suffix()))
                           ? info.absoluteFilePath() : QString();
            }
            // Only network stream schemes go to mpv. Its other protocol
            // handlers (edl://, memory://, fd://, mf://, av://, lavf://,
            // slice://, ...) exist for scripts and tests: pasted or dropped
            // text must not be able to read descriptors, glob local images
            // or open capture devices.
            if (!isStreamScheme(url.scheme()))
                return {};
            return candidate;   // stream: mpv owns whether it can play it
        }
    }

    const QFileInfo info(normalizeLocalPath(candidate));
    if (info.isFile() && isMediaSuffix(info.suffix()))
        return info.absoluteFilePath();
    // A folder opens as its media files (expandMediaPaths), like a drop.
    if (info.isDir())
        return info.absoluteFilePath();
    return {};
}

QVariant PlayerController::mpvProperty(const QString &name) const
{
    if (!m_mpv)
        return {};
    mpv_node node;
    if (mpv_get_property(m_mpv, name.toUtf8().constData(), MPV_FORMAT_NODE, &node) < 0)
        return {};
    const QVariant result = nodeToVariant(&node);
    mpv_free_node_contents(&node);
    return result;
}

// A54. The "screenshot" command runs inside mpv's core for a key binding
// (keypress -> input.conf) as well as for a menu click, so a command reply
// would only cover one of the two. What both share is the core's own log
// report, matched on its text (the prefix differs between mpv builds):
//   info  "Screenshot: '<filename>'"
//   error "Taking screenshot failed." / "Error writing screenshot!"
// <filename> is what mpv wrote: the template alone when no directory is
// set (relative to the process working directory), the joined path
// otherwise. screenshot-raw (previews, Ctrl+S) never logs here.
void PlayerController::handleScreenshotLog(const QString &level, const QString &text)
{
    if (m_stopping) return;
    static const QString savedPrefix = QStringLiteral("Screenshot: '");
    if (level == QStringLiteral("info")) {
        if (!text.startsWith(savedPrefix) || !text.endsWith(QLatin1Char('\'')))
            return;
        const QString written = text.mid(savedPrefix.size(), text.size() - savedPrefix.size() - 1);
        if (written.isEmpty())
            return;
        const QFileInfo info(QDir::fromNativeSeparators(written));
        const QString absolute = QDir::cleanPath(info.isAbsolute()
            ? info.filePath() : QDir::current().absoluteFilePath(info.filePath()));
        emit screenshotSaved(absolute);
        if (m_captureReady && m_clipboardCapture) m_clipboardCapture->requestFile(absolute);
    } else if ((level == QStringLiteral("error") || level == QStringLiteral("fatal"))
               && text.contains(QStringLiteral("screenshot"), Qt::CaseInsensitive)) {
        emit screenshotFailed(text);
    }
}

bool PlayerController::screenshotToClipboard()
{
    if (m_stopping || !m_captureReady || !m_clipboardCapture || m_loadPending || m_currentFile.isEmpty())
        return false;
    // Cached selection avoids a synchronous property read (and avoids
    // initializing libass for files without visible subtitles).
    const bool subtitlesActive = m_subtitlesVisible
        && !selectedTrack(m_trackList, QStringLiteral("sub")).isEmpty();
    return m_clipboardCapture->requestFrame(subtitlesActive);
}

// "90", "1:30", "01:30:00.250" -> seconds; -1 when the text is not a time.
// Only the last component may be fractional. Lives here rather than in
// main.cpp so the F6b go-to-time dialog and the F41 CLI --start flag accept
// exactly the same spellings; main.cpp calls through to this.
double PlayerController::parseTimeSpec(const QString &text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
    if (parts.isEmpty() || parts.size() > 3)
        return -1.0;
    double seconds = 0.0;
    for (int i = 0; i < parts.size(); ++i) {
        const QString &part = parts.at(i);
        if (part.isEmpty())
            return -1.0;
        bool ok = false;
        const double value = part.toDouble(&ok);
        // Every component except the last must be a whole number.
        if (!ok || value < 0.0 || (i < parts.size() - 1 && value != std::floor(value)))
            return -1.0;
        seconds = seconds * 60.0 + value;
    }
    return seconds;
}

// F6a. mpv wants the literal "no" to unset a loop point, so a negative
// request is translated rather than clamped. Setting A after B is allowed:
// mpv swaps them itself, so the UI never has to enforce an order.
void PlayerController::setAbLoopA(double seconds)
{
    if (!m_mpv)
        return;
    const QByteArray value = seconds < 0.0 ? QByteArrayLiteral("no")
                                           : QString::number(seconds, 'f', 3).toUtf8();
    command({QStringLiteral("set"), QStringLiteral("ab-loop-a"), QString::fromUtf8(value)});
}

void PlayerController::setAbLoopB(double seconds)
{
    if (!m_mpv)
        return;
    const QByteArray value = seconds < 0.0 ? QByteArrayLiteral("no")
                                           : QString::number(seconds, 'f', 3).toUtf8();
    command({QStringLiteral("set"), QStringLiteral("ab-loop-b"), QString::fromUtf8(value)});
}

void PlayerController::clearAbLoop()
{
    setAbLoopA(-1.0);
    setAbLoopB(-1.0);
}

void PlayerController::setScrubbing(bool scrubbing)
{
    if (m_scrubbing == scrubbing)
        return;
    m_scrubbing = scrubbing;
    emit scrubbingChanged(m_scrubbing);
}

void PlayerController::playlistAppendFiles(const QStringList &paths, bool startIfIdle)
{
    // append-play starts playback when nothing is playing YET — and "yet" is
    // the trap (A25). Loads are async, so while the first file is still
    // opening mpv is not playing anything, and the next append-play in the
    // same batch starts a load too, and so on: a folder of N siblings fired
    // N overlapping loads that aborted each other ("Loading failed." per
    // sibling, wrong files briefly on screen). Only the FIRST entry may
    // autostart; the rest always append passively.
    bool first = true;
    for (const QString &path : paths) {
        if (path.isEmpty())
            continue;
        const bool autostart = startIfIdle && first;
        first = false;
        command({QStringLiteral("loadfile"), loadArgFromPath(path),
                 autostart ? QStringLiteral("append-play") : QStringLiteral("append")});
    }
}

quint64 PlayerController::playlistPlayIndex(int index)
{
    const quint64 id = command({QStringLiteral("playlist-play-index"), QString::number(index)});
    if (!id) return 0;
    // Retain explicit Play intent across selection acknowledgement, the
    // on_load hook and first-frame presentation. Old pause observations
    // from the previous file must not silently turn this into Pause.
    m_selectionPlayPending = true;
    m_requestedPaused = false;
    // Selecting a card means Play this video, including after the queue
    // ended. Wait for selection before unpausing (same ordering as Play now).
    m_playAppendedStartReply = id;
    return id;
}

void PlayerController::playlistAppendAndPlay(const QStringList &paths, int selectedIndex)
{
    if (!m_mpv) return;
    if (paths.isEmpty()) return;
    m_playAppendedStartReply = 0;
    selectedIndex = qBound(0, selectedIndex, int(paths.size()) - 1);
    // Preserve the default's first nonempty-file behavior for drop lists.
    if (paths.at(selectedIndex).isEmpty()) {
        selectedIndex = 0;
        while (selectedIndex < paths.size() && paths.at(selectedIndex).isEmpty())
            ++selectedIndex;
        if (selectedIndex == paths.size()) return;
    }
    for (int i = 0; i < paths.size(); ++i) {
        const QString &path = paths.at(i);
        if (path.isEmpty()) continue;
        if (i == selectedIndex) {
            m_playAppendedEntry = -1;
            m_playAppendedReply = m_nextReplyId;
            // The selection's playlist-play-index is issued from the append
            // reply; show "opening" from here so the Open button does not
            // reappear in between. The reply error path clears it.
            if (m_idleActive)
                markLoadPending(m_nextReplyId);
        }
        command({QStringLiteral("loadfile"), loadArgFromPath(path), QStringLiteral("append")});
    }
}

void PlayerController::playlistNext()
{
    if (m_playlistPos + 1 < m_playlist.size()) playlistPlayIndex(m_playlistPos + 1);
}

void PlayerController::playlistPrev()
{
    if (m_playlistPos > 0) playlistPlayIndex(m_playlistPos - 1);
}

quint64 PlayerController::playlistRemove(int index)
{
    return command({QStringLiteral("playlist-remove"), QString::number(index)});
}

void PlayerController::playlistClear()
{
    command({QStringLiteral("playlist-clear")});
}

void PlayerController::loadSubtitleFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;
    command({QStringLiteral("sub-add"), filePath, QStringLiteral("select")});
}

void PlayerController::playlistShuffle()
{
    command({QStringLiteral("playlist-shuffle")});
}

// Folder enqueue (F16): the lead's QML decides which sibling to play and
// when to append; this only supplies the naturally sorted candidate list.
// Pure filesystem query — no mpv interaction, no state.
namespace {

// Media files directly inside <dir>, absolute, in natural order.
QStringList sortedMediaFilesIn(const QDir &dir)
{
    // Filter by extension by hand instead of QDir name filters: wildcard
    // matching case sensitivity is platform-dependent, while the contract
    // requires case-insensitive matching everywhere.
    const QFileInfoList allFiles = dir.entryInfoList(QDir::Files);
    QStringList names;
    names.reserve(allFiles.size());
    for (const QFileInfo &entry : allFiles) {
        if (isMediaSuffix(entry.suffix()))
            names.append(entry.fileName());
    }

    // QDir's own Name ordering is plain lexicographic (b10 before b2);
    // QCollator numeric mode gives the episode-style sequential order.
    //
    // Cross-platform trap: QCollator's numeric-mode attribute is a silent
    // no-op under the "C"/POSIX locale — confirmed on Linux, where Qt
    // short-circuits straight to strcoll for that locale name instead of
    // going through ICU. QLocale::system() resolves to exactly "C" on any
    // Linux box with no locale configured (common on minimal installs,
    // containers, and WSL), which would silently sort "episode 10" before
    // "episode 2" with no error or warning. Windows never hits this — its
    // system locale is essentially never "C". Fall back to English so the
    // one thing this code exists for always works; a real system locale is
    // still respected when there is one.
    QLocale sortLocale = QLocale::system();
    if (sortLocale.language() == QLocale::C)
        sortLocale = QLocale(QLocale::English);
    QCollator collator{sortLocale};
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(),
              [&collator](const QString &a, const QString &b) {
                  return collator.compare(a, b) < 0;
              });

    QStringList result;
    result.reserve(names.size());
    for (const QString &name : names)
        result.append(dir.absoluteFilePath(name));
    return result;
}

} // namespace

QStringList PlayerController::siblingMediaFiles(const QString &path) const
{
    // Accept the same path shapes as the loadfile plumbing (plain local
    // paths, file:// URLs from the playlist model) so the caller can pass
    // playlist "filename" entries as-is.
    const QFileInfo info(normalizeLocalPath(path));
    if (!info.isFile())
        return {};
    return sortedMediaFilesIn(QDir(info.absolutePath()));
}

QStringList PlayerController::expandMediaPaths(const QStringList &paths) const
{
    QStringList out;
    for (const QString &path : paths) {
        if (path.isEmpty())
            continue;
        const QFileInfo info(normalizeLocalPath(path));
        if (info.isDir())
            out.append(sortedMediaFilesIn(QDir(info.absoluteFilePath())));
        else
            out.append(path);
    }
    return out;
}

void PlayerController::loadShader(const QString &shaderPath)
{
    command({QStringLiteral("change-list"), QStringLiteral("glsl-shaders"),
             QStringLiteral("append"), QDir::fromNativeSeparators(shaderPath)});
}

quint64 PlayerController::command(const QStringList &args)
{
    if (!m_mpv || m_stopping || args.isEmpty())
        return 0;

    if (args.size() >= 2 && args.at(1) == QStringLiteral("pause")) {
        if (args.at(0) == QStringLiteral("cycle")) { togglePause(); return 0; }
        if (args.at(0) == QStringLiteral("set") && args.size() >= 3) {
            const bool paused = args.at(2) == QStringLiteral("yes") || args.at(2) == QStringLiteral("true")
                                || args.at(2) == QStringLiteral("1");
            if (paused) pause(); else play();
            return 0;
        }
    }

    // A later explicit playback/queue action supersedes a pending drop's
    // selection. Passive appends may continue without cancelling it.
    const QString &name = args.constFirst();
    if (name == QStringLiteral("seek") && !m_submittingSeek) clearPendingSeek();
    if (name == QStringLiteral("playlist-next")) { playlistNext(); return 0; }
    if (name == QStringLiteral("playlist-prev")) { playlistPrev(); return 0; }
    if (!m_submittingFrameStep && (name == QStringLiteral("frame-step") || name == QStringLiteral("frame-back-step"))) {
        m_interactionPause = false;
        m_requestedPaused = true;
        m_pauseTarget = true;
        m_pauseQueryReply = 0;
        m_frameSteps.append(args);
        submitPause();
        return 0;
    }
    if (name.startsWith(QStringLiteral("playlist-")) || name == QStringLiteral("stop")
        || (name == QStringLiteral("loadfile")
            && (args.size() < 3 || args.at(2) == QStringLiteral("replace")))
        || (name == QStringLiteral("set") && args.value(1) == QStringLiteral("playlist-pos"))) {
        m_playAppendedReply = 0;
        m_playAppendedStartReply = 0;
        m_playAppendedEntry = -1;
        m_replaySeekReply = 0;
        m_selectionPlayPending = false;
    }
    const bool changesFile = name == QStringLiteral("stop") || name == QStringLiteral("playlist-play-index")
        || name == QStringLiteral("playlist-remove") || name == QStringLiteral("playlist-clear")
        || (name == QStringLiteral("loadfile") && (args.size() < 3 || args.at(2) == QStringLiteral("replace")))
        || (name == QStringLiteral("set") && args.value(1) == QStringLiteral("playlist-pos"));
    if (changesFile) {
        m_frameSteps.clear();
        if (m_clipboardCapture) m_clipboardCapture->invalidate();
        if (name != QStringLiteral("playlist-clear")
            && (name != QStringLiteral("playlist-remove") || args.value(1).toInt() == m_playlistPos))
            m_captureReady = false;
        clearPendingSeek();
        setScrubbing(false);
    }
    const quint64 id = m_nextReplyId++;
    rememberTrackCommand(args, id);
    if (name == QStringLiteral("stop")) {
        clearSubtitleRequests();
        m_stopReply = id;
        clearPlaybackError();
        clearLoadPending();
    } else if (m_idleActive) {
        // From idle, mpv reports nothing until START_FILE, which comes
        // after the command is dequeued. Flag the request so the UI shows
        // "opening" from the click, not from the demuxer. Only commands that
        // must start a file (or fail with an error reply) qualify: a passive
        // append or a playlist-next at the end would never clear the flag.
        const bool starts = (name == QStringLiteral("loadfile")
                             && (args.size() < 3 || args.at(2) == QStringLiteral("replace")
                                 || args.at(2) == QStringLiteral("append-play")))
                            || name == QStringLiteral("playlist-play-index")
                            || (name == QStringLiteral("set") && args.value(1) == QStringLiteral("playlist-pos"));
        if (starts)
            markLoadPending(id);
    }

    QVector<QByteArray> storage;
    storage.reserve(args.size());
    QVector<const char *> argv;
    argv.reserve(args.size() + 1);
    for (const QString &arg : args) {
        storage.append(arg.toUtf8());
        argv.append(storage.last().constData());
    }
    argv.append(nullptr);
    // The GUI must remain free to animate and service the render thread
    // while mpv opens a decoder, switches devices, or loads subtitles.
    // Observed properties remain the source of truth for completed actions.
    const bool subtitleRequest = isSubtitleRequest(args);
    const int rc = mpv_command_async(m_mpv, id, argv.data());
    if (rc < 0) {
        if (subtitleRequest) emit subtitleLoadingFailed();
        if (m_frameStepReply == id) {
            m_frameStepReply = 0;
            m_frameSteps.clear();
            reconcilePause();
        }
        if (m_seekReply == id) {
            m_seekReply = 0;
            clearPendingSeek(true);
        }
        if (m_stopReply == id) m_stopReply = 0;
        if (m_playAppendedReply == id) m_playAppendedReply = 0;
        if (m_replaySeekReply == id) m_replaySeekReply = 0;
        if (m_loadReply == id) clearLoadPending();
        // A failed command used to vanish silently (e.g. the shutdown "stop"
        // failing after a core fault left the drain burning its full 300 ms
        // with nothing in the log explaining why).
        qWarning("mpv: command failed (%s): %ls", mpv_error_string(rc),
                 qUtf16Printable(args.join(QLatin1Char(' '))));
        emit commandFinished(id, false, QString::fromUtf8(mpv_error_string(rc)));
        return 0;
    }
    if (subtitleRequest) beginSubtitleRequest(id);
    const bool replacesCurrent = changesFile && name != QStringLiteral("playlist-clear")
        && (name != QStringLiteral("playlist-remove") || args.value(1) == QStringLiteral("current")
            || args.value(1).toInt() == m_playlistPos);
    if (replacesCurrent && m_fileMetadataPending) {
        // A newer Stop/Open/selection must win even if issued by a direct
        // metadata signal receiver. Wait for its acknowledgement; Stop drops
        // the snapshot, while START/unload/END invalidate actual transitions.
        // Successful no-op navigation must not strand an otherwise valid load.
        ++m_fileMetadataGeneration;
        m_fileMetadataCommand = id;
        m_fileMetadataStop = name == QStringLiteral("stop");
    }
    return id;
}

