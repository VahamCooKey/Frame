// CONTRACT v20 — owned by lead. Amend via CONTRACTS.md only.
// v20: A62 — subtitle preparation state and asynchronous font-provider setup.
// v19: A60 — asynchronous file-loaded metadata and cached seekability.
// v18: A58 — coalesced wakeups and non-reentrant, bounded event batches.
// v17: A57 — observed audio devices and visibility-scoped Playback Info.
// v16: A55 — asynchronous screenshot completion (empty path for raw),
//     correlated pause writes, seekPending/seekTarget display reconciliation.
// v15: A54 — screenshotSaved(path) / screenshotFailed(message): mpv's
//     "screenshot" command reports where it wrote through its log stream
//     (prefix "screenshot"), which is the only route that covers key
//     bindings, menu entries and user input.conf alike.
// v14: A41 — playlistAppendAndPlay selects the first new entry by mpv ID.
// v13: A39 — command() queues asynchronously; observed properties and
//     commandFinished report completion. prepareForShutdown still drains.
// v12: amendment A36 — loadSubtitleFile() (loads external subtitle via
//     sub-add <path> select) and playlistShuffle() (mpv playlist-shuffle).
// v11: yt-dlp removed (Frame rename cleanup) — ytdlAvailable/ytdlPath,
//     setYtdlEnabled/ytdlEnabled/setYtdlFormat/setYtdlCookies/ytdlVersion,
//     m_ytdlPath/m_ytdlVersion/m_ytdlVersionProbed all deleted. Direct
//     stream URLs are plain mpv and are unaffected.
// v10: amendment A25 — playlistAppendFiles() gains startIfIdle (default true,
//     so every existing caller is unchanged); false appends without mpv's
//     append-play, which was starting one load per entry mid-batch (BUG-4).
// v8: amendment A21 — screenshotToClipboard() (F9 copy frame: mpv has
//     no clipboard screenshot command, so the pixels come back through
//     screenshot-raw).
// v7: amendment A20 — resolveOpenTarget() (F45 open-from-clipboard):
//     arbitrary text -> stream URL, absolute local media path, or empty.
// v6: amendment A19 — F6 editor kit: observed abLoopA/abLoopB (+ setAbLoopA/
//     setAbLoopB/clearAbLoop) for the A-B loop and its timeline region,
//     observed estimatedVfFps for the HH:MM:SS:FF fallback chain, and
//     parseTimeSpec() shared with the F41 CLI --start parser.
// v2: prepareForShutdown() (exit-crash mitigation).
// v3: playlist mirroring — mpv owns ordering/EOF-advance; we observe the
//     playlist/playlist-pos properties and expose thin command slots.
// v4: amendment A7 — observed per-file properties videoWidth/videoHeight
//     (mpv dwidth/dheight, shared NOTIFY videoSizeChanged), containerFps and
//     audioDelay.
// v5: amendment A11 — siblingMediaFiles() (F16 folder auto-enqueue helper).
#pragma once
#include <QObject>
#include <array>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QSet>
#include <atomic>
#include <QVariantList>
#include <QVariantMap>
#include <mpv/client.h>
#include "core/TrackPreference.h"

class ClipboardCapture;

class PlayerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool isPaused READ isPaused WRITE setPaused NOTIFY pauseChanged)
    Q_PROPERTY(bool isMuted READ isMuted WRITE setMuted NOTIFY muteChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaTitleChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY fileLoaded)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(double subtitleDelay READ subtitleDelay WRITE setSubtitleDelay NOTIFY subtitleDelayChanged)
    Q_PROPERTY(bool subtitleLoading READ subtitleLoading NOTIFY subtitleLoadingChanged)
    Q_PROPERTY(QVariantList trackList READ trackList NOTIFY trackListChanged)
    Q_PROPERTY(QVariantList chapterList READ chapterList NOTIFY chapterListChanged)
    Q_PROPERTY(bool eofReached READ eofReached NOTIFY eofReachedChanged)
    Q_PROPERTY(QString hwdecCurrent READ hwdecCurrent NOTIFY hwdecCurrentChanged)
    Q_PROPERTY(bool scrubbing READ scrubbing WRITE setScrubbing NOTIFY scrubbingChanged)
    Q_PROPERTY(bool seekPending READ seekPending NOTIFY seekPendingChanged)
    Q_PROPERTY(double seekTarget READ seekTarget NOTIFY seekPendingChanged)
    Q_PROPERTY(bool videoPresented READ videoPresented NOTIFY presentationStateChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(int cacheBuffering READ cacheBuffering NOTIFY cacheBufferingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)
    Q_PROPERTY(QString playbackError READ playbackError NOTIFY playbackErrorChanged)
    Q_PROPERTY(QString failedFile READ failedFile NOTIFY playbackErrorChanged)
    Q_PROPERTY(QStringList unavailableFiles READ unavailableFiles NOTIFY unavailableFilesChanged)
    Q_PROPERTY(QVariantList playlist READ playlist NOTIFY playlistChanged)
    Q_PROPERTY(QVariantList audioDevices READ audioDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QVariantMap playbackInfo READ playbackInfo NOTIFY playbackInfoChanged)
    Q_PROPERTY(bool playbackInfoActive READ playbackInfoActive WRITE setPlaybackInfoActive NOTIFY playbackInfoActiveChanged)
    Q_PROPERTY(int playlistPos READ playlistPos NOTIFY playlistPosChanged)
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoSizeChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoSizeChanged)
    Q_PROPERTY(double containerFps READ containerFps NOTIFY containerFpsChanged)
    Q_PROPERTY(double audioDelay READ audioDelay NOTIFY audioDelayChanged)
    // F6a A-B loop. Negative means "unset" — mpv reports these as the string
    // "no" when disabled, which has no place in a double.
    Q_PROPERTY(double abLoopA READ abLoopA NOTIFY abLoopChanged)
    Q_PROPERTY(double abLoopB READ abLoopB NOTIFY abLoopChanged)
    // F6c timecode: mpv's own estimate, the second link in the
    // container-fps -> estimated-vf-fps -> 24 fallback chain. container-fps
    // "can easily contain bogus values" (mpv manual), hence the chain.
    Q_PROPERTY(double estimatedVfFps READ estimatedVfFps NOTIFY estimatedVfFpsChanged)

public:
    enum class State { Idle, Loading, Buffering, Playing, Paused, Seeking, Error };
    Q_ENUM(State)

    explicit PlayerController(QObject *parent = nullptr);
    ~PlayerController() override;

    mpv_handle *handle() const { return m_mpv; }
    // Video surfaces opt in; headless controller users do not wait on a GUI.
    void requireVideoPresentation() { m_requireVideoPresentation = true; }
    quint64 presentationGeneration() const { return m_presentationGeneration; }
    bool presentationFrameReady() const { return m_presentationFrameReady; }
    bool videoPresented() const { return m_videoPresented; }
    void setFileDefaults(const QVariantMap &defaults) { m_fileDefaults = defaults; }
    void videoFramePresented(quint64 generation);

    double position() const { return m_position; }
    double duration() const { return m_duration; }
    double volume() const { return m_volume; }
    bool isPaused() const { return m_paused; }
    bool isMuted() const { return m_muted; }
    QString mediaTitle() const { return m_mediaTitle; }
    QString currentFile() const { return m_currentFile; }
    double speed() const { return m_speed; }
    double subtitleDelay() const { return m_subtitleDelay; }
    bool subtitleLoading() const { return m_subtitlesVisible && !m_subtitleRequests.isEmpty(); }
    QVariantList trackList() const { return m_trackList; }
    QVariantList chapterList() const { return m_chapterList; }
    bool eofReached() const { return m_eofReached; }
    QString hwdecCurrent() const { return m_hwdecCurrent; }
    bool scrubbing() const { return m_scrubbing; }
    bool seekPending() const { return m_seekPending; }
    double seekTarget() const { return m_seekTarget; }
    State state() const { return m_state; }
    int cacheBuffering() const { return m_cacheBuffering; }
    QString lastError() const { return m_lastError; }
    QString playbackError() const { return m_playbackError; }
    QString failedFile() const { return m_failedFile; }
    QStringList unavailableFiles() const { return m_unavailableFiles; }
    QVariantList playlist() const { return m_playlist; }
    QVariantList audioDevices() const { return m_audioDevices; }
    QVariantMap playbackInfo() const { return m_playbackInfo; }
    bool playbackInfoActive() const { return m_playbackInfoActive; }
    void setPlaybackInfoActive(bool active);
    int playlistPos() const { return m_playlistPos; }
    int videoWidth() const { return m_videoWidth; }
    int videoHeight() const { return m_videoHeight; }
    double containerFps() const { return m_containerFps; }
    bool isSeekable() const { return m_seekable; }
    double audioDelay() const { return m_audioDelay; }
    double abLoopA() const { return m_abLoopA; }
    double abLoopB() const { return m_abLoopB; }
    double estimatedVfFps() const { return m_estimatedVfFps; }

    // F6b: "90", "1:30", "01:30:00.250" -> seconds; negative when the text
    // is not a time. Shared with the F41 CLI --start parser so the dialog
    // and the command line accept exactly the same spellings.
    Q_INVOKABLE static double parseTimeSpec(const QString &text);

    // F45: classify arbitrary text (clipboard, drop, argv). Returns a
    // stream URL unchanged, a local media file as an absolute path, or
    // empty when it is neither — so callers can report "nothing to open"
    // instead of letting mpv fail on nonsense. Drive letters are ruled
    // out before URL parsing ("c:/x" otherwise reads as scheme "c").
    Q_INVOKABLE QString resolveOpenTarget(const QString &text) const;

    // F9: mpv has no clipboard screenshot command, so the frame comes back
    // through screenshot-raw asynchronously. True means accepted, not copied;
    // screenshotCopied with an empty path reports the eventual result.
    Q_INVOKABLE bool screenshotToClipboard();

    // Generic read of any mpv property as MPV_FORMAT_NODE, decoded through
    // the same nodeToVariant() the event path uses. This read is synchronous;
    // recurring UI work uses observed properties instead.
    Q_INVOKABLE QVariant mpvProperty(const QString &name) const;

    Q_INVOKABLE void loadSubtitleFile(const QString &filePath);
    Q_INVOKABLE void playlistShuffle();

public slots:
    void processMpvEvents();
    void openFile(const QString &filePath);    void play();
    void pause();
    void togglePause();
    void setPaused(bool paused);
    // Temporary UI pause preserves playback intent; explicit playback actions win.
    void setInteractionPause(bool held);
    void retryFailedFile();
    void seek(double seconds);
    void seekRelative(double seconds);
    void setVolume(double vol);
    void setMuted(bool mute);
    void setSpeed(double speed);
    void setAudioTrack(int trackId);
    void setSubtitleTrack(int trackId);
    void setSubtitleDelay(double seconds);
    void setScrubbing(bool scrubbing);
    void loadShader(const QString &shaderPath);
    // Nonblocking dispatch. State updates arrive through observed properties;
    // commandFinished reports the queued command's completion or failure.
    // Nonzero identifies a directly submitted commandFinished reply. Zero
    // means rejected or routed to a higher-level pause/step/navigation action.
    quint64 command(const QStringList &args);
    void stop();

    // F6a A-B loop. Pass a negative value (or call clearAbLoop) to unset;
    // mpv wants the literal string "no" there. mpv auto-swaps A and B when
    // A lands after B, so the UI never has to police the order.
    void setAbLoopA(double seconds);
    void setAbLoopB(double seconds);
    void clearAbLoop();

    // Orderly shutdown for application exit (idempotent). Stops playback,
    // briefly drains the event loop so mpv settles back to idle, then
    // suspends the wakeup callback. Call from aboutToQuit BEFORE the QML
    // engine/window teardown; ~PlayerController still does terminate_destroy.
    void prepareForShutdown();

    // Playlist: appends files; the rest delegate to mpv's playlist commands.
    // The entries mirror mpv's "playlist" property: { filename, title?, ... }.
    // startIfIdle=true uses mpv's append-play so a drop of N files onto an
    // idle player starts the first one. Pass false when the caller is about
    // to choose the entry itself (F16 folder enqueue): append-play makes
    // EVERY not-yet-playing append start a load, so a folder of N siblings
    // opened N loads that then aborted each other (A25).
    void playlistAppendFiles(const QStringList &paths, bool startIfIdle = true);
    // Append without clearing the queue, then play the selected new entry.
    // Uses the append command's returned ID; cached playlist size can lag.
    void playlistAppendAndPlay(const QStringList &paths, int selectedIndex = 0);
    quint64 playlistPlayIndex(int index);
    void playlistNext();
    void playlistPrev();
    quint64 playlistRemove(int index);
    void playlistClear();

    // Folder enqueue (F16): absolute paths of all media files that sit in the
    // same directory as <path>, naturally sorted (QCollator numeric mode,
    // case-insensitive), including <path> itself. Empty when <path> does not
    // exist or has no siblings. Extensions (case-insensitive): mkv mp4 m4v avi
    // mov webm wmv flv ts m2ts mpg mpeg ogv 3gp mp3 m4a flac wav aac ogg opus.
    Q_INVOKABLE QStringList siblingMediaFiles(const QString &path) const;
    // Folders in <paths> become their media files (one level, natural
    // order); files pass through unchanged. Used by drops, the command
    // line and the second-launch handoff.
    Q_INVOKABLE QStringList expandMediaPaths(const QStringList &paths) const;

signals:
    void subtitleLoadingChanged();
    void subtitleLoadingFailed();
    void positionChanged(double position);
    void durationChanged(double duration);
    void volumeChanged(double volume);
    void pauseChanged(bool paused);
    void playbackActionRequested(bool paused);
    void muteChanged(bool muted);
    void mediaTitleChanged(const QString &title);
    void speedChanged(double speed);
    void subtitleDelayChanged(double delay);
    void trackListChanged();
    void trackSelectionFeedback(const QString &type, const QString &label);
    void chapterListChanged();
    void eofReachedChanged(bool eof);
    void hwdecCurrentChanged(const QString &hwdec);
    void scrubbingChanged(bool scrubbing);
    void seekPendingChanged();
    void stateChanged();
    void cacheBufferingChanged(int percent);
    void errorOccurred(const QString &message);
    void endOfFile();
    void fileLoaded(const QString &path);
    void appCommand(const QString &name, const QStringList &args);
    void commandFinished(quint64 id, bool success, const QString &error);
    void playlistChanged();
    void audioDevicesChanged();
    void playbackInfoChanged();
    void playbackInfoActiveChanged();
    void playlistPosChanged(int index);
    void videoSizeChanged();
    void containerFpsChanged();
    void audioDelayChanged();
    void abLoopChanged();
    void estimatedVfFpsChanged();
    void presentationStateChanged();
    void playbackErrorChanged();
    void unavailableFilesChanged();
    // A54. mpv wrote a screenshot; path is absolute with forward slashes
    // (relative names from an unset screenshot-directory are resolved
    // against the working directory mpv used). failed carries mpv's own
    // reason ("Taking screenshot failed.", "Error writing screenshot!").
    void screenshotSaved(const QString &path);
    void screenshotFailed(const QString &message);
    // The latest screenshot request completed. An empty path identifies a
    // raw frame; saved images decode on a bounded worker. Superseded or
    // invalidated requests do not publish a clipboard result.
    void screenshotCopied(const QString &path, bool copied);

private:
    friend class TestEventDelivery;
    static bool isSubtitleRequest(const QStringList &args);
    void beginSubtitleRequest(quint64 id);
    void finishSubtitleRequest(quint64 id, bool success);
    void clearSubtitleRequests();
    void armSubtitleFontsIfNeeded();
    // The callback only schedules work. All event payloads and controller
    // state remain on the GUI thread; only these gates cross threads.
    static void mpvWakeup(void *context);
    void scheduleMpvEvents();
    void drainMpvEvents();
    void finishShutdown();
    std::atomic_bool m_eventDeliveryEnabled{true};
    std::atomic_bool m_eventWakeupQueued{false};
    bool m_processingEvents{false};
    bool m_eventDrainRequested{false};
    bool m_eventQueueDrained{true};
    bool m_finishShutdownPending{false};
    const bool m_traceEventDelivery{qEnvironmentVariableIsSet("FRAME_PERF")};
    std::atomic<quint64> m_eventWakeups{0};
    std::atomic<quint64> m_eventPosts{0};
    quint64 m_eventBatches{0}, m_eventsProcessed{0}, m_eventBudgetYields{0};
    quint64 m_eventReentries{0}, m_eventOverflows{0};
    int m_maxEventBatch{0};
    qint64 m_maxEventBatchNs{0};
    QElapsedTimer m_eventReportClock;
    QVariantMap m_fileDefaults;
    void handleScreenshotLog(const QString &level, const QString &text);
    ClipboardCapture *m_clipboardCapture{nullptr};
    bool m_captureReady{false};
    void initMpv();
    void updateState();
    void observeProperties();
    void startPlaybackInfoObservation();
    void clearPlaybackInfoObservation();
    void applyPause(bool paused);
    void submitPause();
    void reconcilePause();
    void continueLoadHook();
    void submitSeek();
    void querySeekPosition();
    void clearPendingSeek(bool settled = false);
    void releasePresentationHold();
    void clearPlaybackError();
    void reportPlaybackError(const QString &path);
    QString fileForPlaylistEntry(qint64 id) const;
    QString defaultInputConfPath() const;
    QString userInputConfPath() const;
    void applyTrackPreferences();
    void rememberChangedTracks();
    void rememberTrackCommand(const QStringList &args, quint64 replyId);
    void beginFileMetadata();
    void invalidateFileMetadata();
    bool acceptFileMetadataReply(quint64 replyId, const QVariant &value);
    void finishFileMetadata();
    enum FileMetadataField { Tracks, SubVisibility, Path, VideoTrack, Seekable, Fps, MetadataCount };
    std::array<quint64, MetadataCount> m_fileMetadataReplies{};
    std::array<QVariant, MetadataCount> m_fileMetadata{};
    quint64 m_fileMetadataGeneration{0};
    quint64 m_fileMetadataCommand{0};
    bool m_fileMetadataStop{false};
    bool m_fileMetadataPending{false};
    bool m_metadataPublished{false};
    bool m_metadataTrackObserved{false};
    bool m_seekable{false};

    mpv_handle *m_mpv{nullptr};
    QVariantList m_audioDevices;
    QVariantMap m_playbackInfo;
    QTimer m_playbackInfoTimer;
    quint64 m_playbackInfoObservation{0};
    bool m_playbackInfoActive{false};
    double m_position{0.0};
    double m_duration{0.0};
    double m_volume{100.0};
    bool m_paused{false};
    bool m_requestedPaused{false};
    bool m_interactionPause{false};
    bool m_pauseTarget{false};
    bool m_pauseSubmitted{false};
    quint64 m_pauseReply{0};
    quint64 m_pauseQueryReply{0};
    quint64 m_frameStepReply{0};
    QList<QStringList> m_frameSteps;
    bool m_submittingFrameStep{false};
    quint64 m_loadHookId{0};
    bool m_loadHookPending{false};
    bool m_continuingLoadHook{false};
    bool m_stopping{false};
    bool m_shutdownStopConfirmed{false};
    bool m_selectionPlayPending{false};
    bool m_requireVideoPresentation{false};
    bool m_waitingForPresentation{false};
    bool m_presentationFrameReady{false};
    bool m_videoPresented{false};
    bool m_presentationFileLoaded{false};
    bool m_presentationHasVideo{true};
    quint64 m_presentationGeneration{0};
    bool m_muted{false};
    bool m_eofReached{false};
    bool m_scrubbing{false};
    bool m_seekPending{false};
    bool m_seekQueued{false};
    bool m_submittingSeek{false};
    bool m_seekActive{false};
    bool m_seekStarted{false};
    bool m_seekRestarted{false};
    double m_seekTarget{0.0};
    double m_activeSeekTarget{0.0};
    QElapsedTimer m_seekRestartClock;
    quint64 m_seekReply{0};
    quint64 m_seekQueryReply{0};
    quint64 m_seekIdleQueryReply{0};
    QTimer m_seekTimer;
    QTimer m_seekQueryTimer;
    double m_speed{1.0};
    double m_subtitleDelay{0.0};
    QString m_mediaTitle;
    QString m_currentFile;
    QString m_hwdecCurrent;
    QString m_lastError;
    QString m_loadingFile;
    QString m_failedFile;
    QString m_playbackError;
    QStringList m_unavailableFiles;
    QVariantList m_trackList;
    bool m_trackFeedbackReady{false};
    bool m_subtitlesVisible{true};
    QSet<quint64> m_subtitleRequests;
    QString m_subtitleFontProvider{QStringLiteral("none")};
    quint64 m_subtitleFontReply{0};
    qint64 m_activeSubtitleId{-1};
    TrackPreference m_audioPreference;
    TrackPreference m_subtitlePreference;
    QVariantList m_initialTracks;
    bool m_initialSubtitlesVisible{true};
    bool m_haveInitialTracks{false};
    bool m_audioTrackTouched{false};
    bool m_subtitleTrackTouched{false};
    bool m_haveSubtitleVisibilityPreference{false};
    bool m_subtitleVisibilityPreference{true};
    quint64 m_audioTrackReply{0};
    quint64 m_subtitleTrackReply{0};
    QVariantList m_chapterList;
    State m_state{State::Idle};
    int m_cacheBuffering{0};
    QVariantList m_playlist;
    int m_playlistPos{-1};
    // Per-file video properties. dwidth/dheight are 0 for audio-only files
    // (meaningful — QML gates on it); reset in openFile() like the rest of
    // the per-file state.
    int m_videoWidth{0};
    int m_videoHeight{0};
    double m_containerFps{0.0};
    double m_audioDelay{0.0};
    // F6a: negative = unset, which is how mpv's "no" arrives here.
    double m_abLoopA{-1.0};
    double m_abLoopB{-1.0};
    double m_estimatedVfFps{0.0};
    bool m_coreIdle{true};
    bool m_idleActive{true};
    bool m_pausedForCache{false};
    bool m_seeking{false};
    // Last position value actually announced via positionChanged; time-pos
    // events arrive per rendered frame, so emission is throttled (~10 Hz).
    double m_lastEmittedPosition{-1e9};
    // Set while a coalesced playlistChanged() emit is scheduled (16 ms).
    bool m_playlistFlushPending{false};
    quint64 m_nextReplyId{1};
    quint64 m_playAppendedReply{0};
    quint64 m_playAppendedStartReply{0};
    quint64 m_replaySeekReply{0};
    quint64 m_stopReply{0};
    qint64 m_playAppendedEntry{-1};
    // An open requested while mpv was idle, not yet acknowledged by
    // START_FILE; reported as State::Loading. m_loadReply is the command
    // whose error reply would cancel it.
    bool m_loadPending{false};
    quint64 m_loadReply{0};
    void markLoadPending(quint64 replyId);
    void clearLoadPending();
};
