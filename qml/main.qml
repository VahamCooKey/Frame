import QtQuick
import QtQuick.Window
import Frame.Media 1.0

Window {
    id: root

    width: 1280
    height: 720
    // Resizing floors: below these the bottom bar (play / volume / tracks /
    // fullscreen) no longer fits. Enforced natively too — WM_GETMINMAXINFO
    // flows through the subclassed WndProc to Qt's handler during the
    // system resize loop.
    // The mini player is smaller than any normal window may be.
    minimumWidth: miniPlayer ? 240 : 480
    minimumHeight: miniPlayer ? 135 : 360
    // main.cpp shows the native window on the first event-loop turn.  This
    // avoids blocking QML component construction on a cold graphics driver.
    visible: false
    color: root.cBg
    title: Player.failedFile !== "" ? MediaNames.display(Player.failedFile, MediaNames.cleanupEnabled)
          : Player.currentFile === "" ? "Frame"
                                    : MediaNames.display(Player.currentFile, MediaNames.cleanupEnabled)
    // Taskbar/Alt-Tab icon is set in main.cpp (QQuickWindow::setIcon) —
    // QML's Window type has no icon property in Qt 6. Same SVG as the .rc.

    // The Colors singleton can be briefly unresolved during engine startup;
    // keep local fallbacks so first-frame bindings never assign undefined.
    readonly property bool hasColors: typeof Colors !== "undefined" && Colors !== null
    readonly property color cBg: hasColors ? Colors.bg : "#080808"
    readonly property color cBar: hasColors ? Colors.surface : "#F5181818"
    readonly property color cText: hasColors ? Colors.text : "#F3F3F3"
    readonly property color cTextDim: hasColors ? Colors.textDim : "#A3A3A3"
    readonly property color cDanger: hasColors ? Colors.danger : "#F0A8A8"
    readonly property color cAccent: hasColors ? Colors.accent : "#E0E0E0"

    property string errorMessage: ""
    property int dropCount: 0               // >0 while a file drag hovers the window
    property bool dropIsSubtitle: false
    property int dropChoice: 0

    // F36 shuttle: J/L trim the speed in 0.25x steps (J down to a 0.25x
    // floor, L up to an 8x ceiling); K toggles pause. Holding L bursts to
    // the hold target (taps 1/2/3 → 2.5x/5x/8x) and reverts on release.
    property bool holdActive: false
    property bool holdWasPaused: false
    property real holdTarget: 1.0
    property real holdPreSpeed: 1.0
    Timer {
        id: holdRampTimer
        interval: 100
        repeat: true
        onTriggered: {
            const next = Math.round((Player.speed + 0.25) * 100) / 100
            if (next >= root.holdTarget) {
                Player.setSpeed(root.holdTarget)
                holdRampTimer.stop()
            } else {
                Player.setSpeed(next)
            }
        }
    }
    function endShuttleHold() {
        if (!root.holdActive)
            return
        root.holdActive = false
        holdRampTimer.stop()
        Player.setSpeed(root.holdPreSpeed)
        if (root.holdWasPaused)
            Player.setPaused(true)
        root.holdWasPaused = false
    }
    function stepShuttle(delta) {
        Player.setSpeed(Math.min(8.0, Math.max(0.25,
            Math.round((Player.speed + delta) * 100) / 100)))
    }
    // Loop-file is sticky; temporary aspect changes reset to the saved default.
    property bool loopFile: false
    readonly property var aspectValues: ["no", "16:9", "4:3", "2.35"]
    property int aspectIdx: Preferences.defaultAspectIndex
    function setAspect(index) {
        root.aspectIdx = ((index % 4) + 4) % 4
        Player.command(["set", "video-aspect-override", root.aspectValues[root.aspectIdx]])
        root.showHud(root.aspectIdx === 0 ? "Aspect: original"
                                          : "Aspect: " + root.aspectValues[root.aspectIdx])
    }
    // Mirrors general/playlistRepeat; the quick menu, playlist options and
    // the settings pane all write that one setting.
    property bool playlistRepeat: Settings.flag("general/playlistRepeat", false)
    function setPlaylistRepeat(on) {
        Settings.setValue("general/playlistRepeat", on)
        Player.command(["set", "loop-playlist", on ? "inf" : "no"])
        root.showHud(on ? "Repeat playlist: on" : "Repeat playlist: off")
    }
    // F2 auto-fit runs at most once per file; videoSizeChanged can fire more
    // than once (video reconfiguration) and a skipped attempt (audio-only,
    // maximized, theater, fullscreen) must not consume the one chance.
    property bool perFileFitted: false

    // F6d zoom + pan. mpv's video-zoom is log2 (0 = 1x, 1 = 2x), so the
    // clamp is in that space: 0..3 is 1x..8x. Pan is in fractions of the
    // scaled video, so it only means anything once zoomed in. All three
    // reset per file — carrying a 6x zoom into the next video is never what
    // anyone wants.
    readonly property real zoomStep: 0.25
    readonly property real zoomMax: 3.0
    property real videoZoom: 0.0
    property real videoPanX: 0.0
    property real videoPanY: 0.0

    function setZoom(z) {
        const clamped = Math.max(0, Math.min(root.zoomMax, z))
        root.videoZoom = clamped
        Player.command(["set", "video-zoom", clamped.toFixed(3)])
        if (clamped === 0)
            root.applyPan(0, 0)
        root.showHud("Zoom " + Math.pow(2, clamped).toFixed(2) + "x")
    }

    function applyPan(x, y) {
        // Beyond ~1 the video has left the frame entirely; clamp so panning
        // cannot lose the picture off-screen.
        root.videoPanX = Math.max(-1, Math.min(1, x))
        root.videoPanY = Math.max(-1, Math.min(1, y))
        Player.command(["set", "video-pan-x", root.videoPanX.toFixed(4)])
        Player.command(["set", "video-pan-y", root.videoPanY.toFixed(4)])
    }

    function resetZoom() {
        root.videoZoom = 0
        Player.command(["set", "video-zoom", "0"])
        root.applyPan(0, 0)
    }

    // F6e bookmarks. SettingsStore is stateless, so the live list is held
    // here and refreshed explicitly — on file load and after each toggle.
    property var bookmarkList: []

    function refreshBookmarks() {
        root.bookmarkList = Player.currentFile !== ""
            ? Settings.bookmarks(Player.currentFile) : []
    }

    function toggleBookmarkHere() {
        if (Player.currentFile === "")
            return
        const added = Settings.toggleBookmark(Player.currentFile, Player.position)
        root.refreshBookmarks()
        root.showHud((added ? "Bookmark added at " : "Bookmark removed at ")
                     + KeyNames.clock(Player.position))
    }

    // F6c time display: 0 elapsed, 1 remaining, 2 SMPTE-style timecode.
    property int timeDisplayMode: 0

    function timeDisplayName() {
        return ["elapsed", "remaining", "timecode (HH:MM:SS:FF)"][root.timeDisplayMode]
    }

    // container-fps can "easily contain bogus values" (mpv manual), so fall
    // back to mpv's own estimate and finally to 24 rather than showing a
    // frame number derived from nonsense.
    function timecodeFps() {
        if (Player.containerFps > 0.1 && Player.containerFps < 1000)
            return Player.containerFps
        if (Player.estimatedVfFps > 0.1 && Player.estimatedVfFps < 1000)
            return Player.estimatedVfFps
        return 24
    }

    function pad2(n) { return n < 10 ? "0" + n : "" + n }

    // HH:MM:SS always, unlike formatTime which drops a zero hour — used
    // where a fixed-width clock reads better (HUD, go-to-time).
    function formatClock(t) {
        var total = Math.max(0, Math.floor(t))
        return root.pad2(Math.floor(total / 3600)) + ":"
             + root.pad2(Math.floor(total / 60) % 60) + ":"
             + root.pad2(total % 60)
    }

    function formatTimecode(t) {
        const fps = root.timecodeFps()
        const safe = Math.max(0, t)
        var frames = Math.floor((safe - Math.floor(safe)) * fps)
        if (frames >= fps)
            frames = Math.floor(fps) - 1
        return root.formatClock(safe) + ":" + root.pad2(frames)
    }

    // The OSD's left-hand playhead label, which follows timeDisplayMode.
    // Kept separate from formatTime: the duration label and the scrub
    // tooltip are always plain elapsed, and folding the mode into
    // formatTime made the total read "-0:00" in remaining mode.
    function formatPosition(t) {
        if (root.timeDisplayMode === 1 && Player.duration > 0)
            return "-" + root.formatTime(Player.duration - t)
        if (root.timeDisplayMode === 2)
            return root.formatTimecode(t)
        return root.formatTime(t)
    }

    function formatTime(t) {
        var total = Math.max(0, Math.floor(t))
        var s = total % 60
        var m = Math.floor(total / 60) % 60
        var h = Math.floor(total / 3600)
        var ss = s < 10 ? "0" + s : "" + s
        var mm = m < 10 ? "0" + m : "" + m
        return h > 0 ? h + ":" + mm + ":" + ss : mm + ":" + ss
    }

    // Transient value feedback (volume/speed/sub-delay/resume). The OSD is
    // hidden most of the time these fire, so this lives at window level.
    property string hudText: ""
    property string trackFeedbackText: ""

    function showHud(text) {
        hudText = text
        hudTimer.restart()
    }

    // main.cpp advances these after the first empty frame and a short idle
    // turn. Keeping the controls tree out of the initial component graph
    // ensures native-window creation is never delayed by secondary UI.
    property bool startupVideoReady: false
    property bool startupChromeReady: false

    // Theater reserves space below the video for the control bar and shelf;
    // ordinary/fullscreen playback overlays both. Restore the saved video
    // rectangle on exit, and cap the expanded window to the work area.
    property bool theaterMode: false
    // PlayerController::State ordinals (QML cannot reach Q_ENUMs through a
    // context-property instance, so keep the numeric contract here).
    readonly property int stateIdle: 0
    readonly property int stateLoading: 1
    readonly property int statePlaying: 3
    readonly property bool theaterLayout: theaterMode && !isFullscreen
    property real theaterBaseX: 0
    property real theaterBaseY: 0
    property real theaterBaseW: 0
    property real theaterBaseH: 0
    // Suppress the drawer's geometry callback while the window and anchors
    // are being changed together.  Letting each individual property flush
    // produces a visible one-frame vertical bounce on a theater toggle.
    property bool theaterTransitioning: false
    // titleBar + the 72 px theater control band (OnScreenController.band).
    readonly property real theaterChromeH: 40 + 72
    // The right-edge strip can summon the playlist. Its shared playback
    // idle clock handles dismissal. lastDrawerCloseMs lets
    // the summon strip tell a genuine hover apart from the hover flip a
    // close produces when the drawer (or chrome) closes under a cursor
    // parked on the strip.
    readonly property bool peekMode: theaterMode || isFullscreen
    property real lastDrawerCloseMs: 0
    // F34: the top bar (caption) and the bottom bar have INDEPENDENT
    // hover-peek clocks. topBarShown drives the caption in fullscreen and
    // windowed playback (theater pins it visible via theaterLayout); the
    // top-edge summon strip and any caption interaction re-arm it. The OSD
    // keeps its own osd.shown clock fed by the bottom-edge strip.
    property bool topBarShown: true
    // Interface pane "OSD auto-hide timeout"; shared by this timer and
    // cursorIdleTimer below so both chrome layers disappear together.
    property int osdHideMs: Settings.value("interface/osdHideMs", 1400)
    Timer {
        id: topBarTimer
        interval: root.osdHideMs
        onTriggered: {
            if (titleBar.hovered) { restart(); return }
            root.topBarShown = false
            root.lastTopHideMs = Date.now()
        }
    }
    // The caption is born visible (topBarShown: true) but its clock above was
    // only ever armed by mouse paths (summon strip, caption interaction), so
    // a CLI open — which involves no mouse travel — left it up for the whole
    // playback. Arm the same clock when playback actually starts; the caption
    // then fades with the OSD bar instead of outliving it. Paused/idle opens
    // never reach Playing, so the caption stays (consistent with the OSD's
    // paused rule and mandatory for dragging a frameless idle window).
    Connections {
        target: Player
        function onStateChanged() {
            if (Player.state === root.statePlaying)
                topBarTimer.restart()
        }
    }
    // The shelf fits above the controls even at the 480 x 360 minimum.
    readonly property real playlistHeight: Math.max(172, Math.min(224,
        (root.theaterLayout ? root.theaterBaseH : root.height) - 188))
    // F18 theater growth: drive the window grow/shrink from this single
    // docked-state flag (theater only — fullscreen is monitor-sized and
    // never resizes). Hooking it here instead of the drawer's onShownChanged
    // keeps every path consistent, including theater enter/leave, where
    // theaterMode flips after shown has already been set.
    readonly property bool theaterDocked: root.theaterLayout && playlistDrawer.shown
    readonly property int theaterPlaylistWidth: 300
    onTheaterDockedChanged: root.applyTheaterPlaylistSize()
    // F19 windowed hover-peek: "the playlist is toggled on". Armed by a
    // manual open (p / OSD ☰) in windowed mode; disarmed by a manual close
    // (p / ☰ / drawer ✕ / ESC in windowed mode). While armed, the summon
    // strip is also active in windowed mode and a hover-open is unpinned
    // with the 1400 ms collapse.
    property bool playlistAutoPeek: false
    // True only while main.qml itself closes the drawer implicitly (OSD
    // auto-hide, the peek clock, a video click): onShownChanged uses it to
    // spare those closes from disarming playlistAutoPeek.
    property bool drawerAutoClosing: false

    function enterTheater() {
        if (root.theaterMode || root.isFullscreen)
            return
        if (root.miniPlayer)
            root.leaveMiniPlayer()

        root.theaterTransitioning = true
        root.theaterBaseX = root.x
        root.theaterBaseY = root.y
        root.theaterBaseW = root.width
        root.theaterBaseH = root.height
        playlistDrawer.shown = true
        // Theater entry is a manual open: pin the drawer so the peek clock
        // never collapses it (ESC / p / its close button still do).
        // Set the outer rectangle first, then switch the video anchors in
        // this same event turn.  Qt presents only the final arrangement, so
        // the video stays anchored rather than visibly jumping up and down.
        root.x = root.theaterBaseX
        root.y = root.theaterBaseY - titleBar.height
        root.width = root.theaterBaseW
        root.height = root.theaterWindowHeight()
        root.theaterMode = true
        root.applyTheaterPlaylistSize()
        osd.pinned = true
        osd.showControls()
        theaterSettleTimer.restart()
    }

    function leaveTheater() {
        if (!root.theaterMode || root.isFullscreen)
            return

        root.theaterTransitioning = true
        playlistDrawer.shown = false
        root.x = root.theaterBaseX
        root.y = root.theaterBaseY
        root.width = root.theaterBaseW
        root.height = root.theaterBaseH
        root.theaterMode = false
        theaterSettleTimer.restart()
        osd.pinned = false
        // Chrome must not linger after leaving theater: hide the playbar so
        // the video is fully clean until the user moves the mouse or clicks.
        osd.hideNow()
    }

    // F28: programmatic x/y assignments echo back through the native window
    // (WM_WINDOWPOSCHANGED) one or two event turns LATER — after
    // theaterTransitioning was already cleared synchronously. The base
    // trackers then store the THEATER rectangle as the new base and every
    // toggle creeps the window up by the titlebar height. Keep the guards up
    // until the echo has settled; a user drag within those 60 ms is simply
    // not tracked (immeasurable).
    Timer {
        id: theaterSettleTimer
        interval: 60
        onTriggered: root.theaterTransitioning = false
    }

    // Mini player: a small always-on-top window pinned to a corner of the
    // screen the window is on. Leaving it restores the previous rectangle,
    // maximized state and always-on-top setting. Dragging it away from its
    // corner is allowed; the corner check marks then clear.
    property bool miniPlayer: false
    property string miniCorner: ""
    property var miniRestore: null
    property bool miniWasMaximized: false
    // Read by main.cpp on exit: the size to remember is the normal window,
    // never the mini one.
    readonly property var windowRestoreGeometry: miniPlayer && miniRestore ? miniRestore : null
    property bool miniPlacing: false
    Timer { id: miniSettleTimer; interval: 150; onTriggered: root.miniPlacing = false }
    // Leaving fullscreen or maximized settles asynchronously; the follow-up
    // (enter the mini player, apply a size preset) runs once it has.
    Timer {
        id: windowDeferTimer
        interval: 280
        property var pending: null
        onTriggered: { const f = pending; pending = null; if (f) f() }
    }
    function deferWindowAction(f) {
        windowDeferTimer.pending = f
        windowDeferTimer.restart()
    }
    function windowWorkArea() {
        return Native.screenWorkArea(Math.round(root.x + root.width / 2), Math.round(root.y + root.height / 2))
    }
    function nearestCorner() {
        const a = root.windowWorkArea()
        const cx = root.x + root.width / 2, cy = root.y + root.height / 2
        return (cy < a.y + a.height / 2 ? "t" : "b") + (cx < a.x + a.width / 2 ? "l" : "r")
    }
    function miniSize(area) {
        const aspect = Player.videoWidth > 0 && Player.videoHeight > 0
            ? Player.videoWidth / Player.videoHeight : 16 / 9
        let w = Math.round(Math.max(320, Math.min(560, area.width * 0.24)))
        let h = Math.round(w / aspect)
        if (h > area.height * 0.42) {
            h = Math.round(area.height * 0.42)
            w = Math.round(h * aspect)
        }
        return Qt.size(Math.max(240, w), Math.max(135, h))
    }
    function placeMiniPlayer(corner) {
        if (!corner || corner.length !== 2) corner = root.nearestCorner()
        const area = root.windowWorkArea()
        const size = root.miniSize(area)
        const margin = 16
        root.miniPlacing = true
        root.width = size.width
        root.height = size.height
        root.x = corner[1] === "l" ? area.x + margin : area.x + area.width - size.width - margin
        root.y = corner[0] === "t" ? area.y + margin : area.y + area.height - size.height - margin
        root.miniCorner = corner
        miniSettleTimer.restart()
    }
    function enterMiniPlayer(corner) {
        if (root.isFullscreen) {
            root.toggleFullscreen()
            root.deferWindowAction(() => root.enterMiniPlayer(corner))
            return
        }
        if (root.theaterMode)
            root.leaveTheater()
        if (root.miniPlayer) {
            root.placeMiniPlayer(corner)
            return
        }
        if (Native.isMaximized()) {
            root.miniWasMaximized = true
            Native.toggleMaximize()
            root.deferWindowAction(() => root.enterMiniPlayer(corner))
            return
        }
        root.miniRestore = { x: root.x, y: root.y, width: root.width, height: root.height,
                             onTop: Native.alwaysOnTop, maximized: root.miniWasMaximized }
        root.miniWasMaximized = false
        playlistDrawer.shown = false
        root.miniPlayer = true
        if (!Native.alwaysOnTop)
            Native.setAlwaysOnTop(true)
        root.placeMiniPlayer(corner)
        osd.hideNow()
    }
    function leaveMiniPlayer() {
        if (!root.miniPlayer)
            return
        const r = root.miniRestore
        root.miniPlayer = false
        root.miniCorner = ""
        root.miniRestore = null
        root.miniPlacing = true
        if (r) {
            root.width = r.width
            root.height = r.height
            root.x = r.x
            root.y = r.y
            if (!r.onTop && Native.alwaysOnTop)
                Native.setAlwaysOnTop(false)
            if (r.maximized)
                Native.toggleMaximize()
        }
        miniSettleTimer.restart()
    }

    // Window size presets (Alt+0/1/2): the video size times scale, shrunk
    // to fit the screen, centred where the window is now.
    function setWindowScale(scale) {
        if (Player.videoWidth <= 0 || Player.videoHeight <= 0) {
            root.showHud("No video to size the window to")
            return
        }
        if (root.isFullscreen) {
            root.toggleFullscreen()
            root.deferWindowAction(() => root.setWindowScale(scale))
            return
        }
        if (root.theaterMode)
            root.leaveTheater()
        if (root.miniPlayer) {
            root.miniRestore = null
            root.miniPlayer = false
            root.miniCorner = ""
        }
        if (Native.isMaximized()) {
            Native.toggleMaximize()
            root.deferWindowAction(() => root.setWindowScale(scale))
            return
        }
        const area = root.windowWorkArea()
        const cx = root.x + root.width / 2, cy = root.y + root.height / 2
        let w = Player.videoWidth * scale, h = Player.videoHeight * scale
        const fit = Math.min(1, area.width / w, area.height / h)
        w = Math.max(root.minimumWidth, Math.round(w * fit))
        h = Math.max(root.minimumHeight, Math.round(h * fit))
        root.width = w
        root.height = h
        root.x = Math.max(area.x, Math.min(Math.round(cx - w / 2), area.x + area.width - w))
        root.y = Math.max(area.y, Math.min(Math.round(cy - h / 2), area.y + area.height - h))
        root.showHud(fit < 1 ? "Window: fit to screen" : "Window: " + Math.round(scale * 100) + "%")
    }

    // Theater window height: the video column at the clip's own aspect
    // ratio for the column width, plus the caption and the 72 px band, so
    // a 1.9:1 or 2.39:1 clip gets no black bars above and below. Before the
    // video size is known it keeps the windowed height.
    function theaterWindowHeight() {
        const vw = Player.videoWidth, vh = Player.videoHeight
        const columnH = vw > 0 && vh > 0 ? Math.round(root.theaterBaseW * vh / vw)
                                         : root.theaterBaseH + root.theaterChromeH - titleBar.height - 72
        return Math.min(root.Screen.desktopAvailableHeight, columnH + titleBar.height + 72)
    }

    function updateTheaterWindow() {
        if (!root.theaterMode || root.isFullscreen)
            return
        // Keep the video origin while reserving the chrome below it.
        root.x = root.theaterBaseX
        root.y = root.theaterBaseY - titleBar.height
        root.height = root.theaterWindowHeight()
        root.applyTheaterPlaylistSize()
    }

    // All shelf open/close paths share the theater geometry calculation.
    function applyTheaterPlaylistSize() {
        if (!root.theaterLayout) return
        root.width = Math.min(root.Screen.desktopAvailableWidth,
            root.theaterBaseW + (playlistDrawer.shown ? root.theaterPlaylistWidth : 0))
        root.height = Math.min(root.Screen.desktopAvailableHeight,
            root.theaterWindowHeight())
        const maxX = root.Screen.desktopAvailableWidth - root.width
        if (maxX >= 0 && root.x > maxX) root.x = maxX
        const maxY = root.Screen.desktopAvailableHeight - root.height
        if (maxY >= 0 && root.y > maxY) root.y = maxY
    }

    // The base rectangle is captured once on entering theater mode; keep it
    // current as the user drags the enlarged window, or any later re-apply
    // of the theater rectangle (updateTheaterWindow) would teleport the
    // window back to where theater was entered.
    onXChanged: {
        if (root.theaterMode && !root.theaterTransitioning && !root.isFullscreen)
            root.theaterBaseX = x
        if (root.miniPlayer && !root.miniPlacing)
            root.miniCorner = ""
    }
    onYChanged: {
        if (root.theaterMode && !root.theaterTransitioning && !root.isFullscreen)
            root.theaterBaseY = y
        if (root.miniPlayer && !root.miniPlacing)
            root.miniCorner = ""
    }

    readonly property var baseFlags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinMaxButtonsHint
    readonly property bool isFullscreen: savedGeo !== null
    property var savedGeo: null
    // When fullscreen last ended; the caption ignores a double-click that
    // straddles the exit (TitleBar.qml).
    property real lastFullscreenExitMs: 0

    // Both F and the OSD button arrive here through the same mpv
    // app-toggle-fullscreen command. Native code applies monitor geometry,
    // topmost z-order and square corners atomically so no taskbar, desktop or
    // application pixels remain visible.
    function toggleFullscreen() {
        const t0 = Date.now()
        if (!root.isFullscreen && root.miniPlayer)
            root.leaveMiniPlayer()
        console.log("fullscreen: toggle requested, leaving =", root.isFullscreen)
        if (root.isFullscreen) {
            const restore = savedGeo
            savedGeo = null
            root.lastFullscreenExitMs = Date.now()
            playlistDrawer.shown = restore.drawerShown
            // Returning to theater re-arms its pinned drawer; in plain
            // windowed mode the pin is meaningless. Disarm a stale peek
            // clock so a restored drawer is not instantly auto-collapsed.
            Native.setFullscreenWindow(false, restore.x, restore.y,
                                       restore.width, restore.height)
            // Fullscreen sets the native rectangle directly, which leaves
            // Qt's maximized state out of step with the real size: Restore
            // then re-maximized instead of restoring. Come back to the
            // normal rectangle and maximize again through Qt.
            if (restore.maximized)
                Qt.callLater(() => { if (!Native.isMaximized()) root.toggleMaximized() })
        } else {
            // From a maximized window, remember the normal rectangle and
            // the maximized flag rather than the maximized rectangle.
            const wasMaximized = Native.isMaximized()
            const normal = wasMaximized && root.preMaxGeo ? root.preMaxGeo
                : { x: x, y: y, width: width, height: height }
            savedGeo = {
                x: normal.x, y: normal.y, width: normal.width, height: normal.height,
                maximized: wasMaximized,
                drawerShown: playlistDrawer.shown
            }
            // The drawer overlays the whole video area; do not carry it
            // into fullscreen (user preference: auto-collapse).
            playlistDrawer.shown = false
            Native.setFullscreenWindow(true, 0, 0, 0, 0)
        }
        osd.showControlsNow()
        console.log("fullscreen: applied, delta ms =", Date.now() - t0)
    }

    // NativeWindowWin32 changes z-order without recreating the QML window.
    // Rebinding WindowStaysOnTopHint here caused a slow hide/show cycle.
    flags: root.baseFlags

    // Titlebar drag while fullscreen or maximized: restore the windowed
    // geometry first — anchored so the grab point stays under the cursor on
    // the restored caption (Windows' own maximized-drag behavior) — then
    // hand over to the system move loop. Called from TitleBar.qml with the
    // grab point in window coordinates.
    function captionDrag(winX, winY) {
        if (root.isFullscreen) {
            if (!root.savedGeo) {
                Native.startMove()
                return
            }
            const restore = root.savedGeo
            const screenX = root.x + winX
            const screenY = root.y + winY
            // Keep the cursor at the same fractional position along the
            // caption and the same pixel offset below the window's top edge.
            let nx = Math.round(screenX - winX * restore.width / Math.max(1, root.width))
            let ny = Math.round(screenY - winY)
            // Keep the titlebar reachable: caption visible, most of the
            // window allowed off any one edge (like native windows).
            const margin = 40
            nx = Math.max(-(restore.width - 2 * margin), Math.min(nx, root.Screen.desktopAvailableWidth - margin))
            ny = Math.max(0, Math.min(ny, root.Screen.desktopAvailableHeight - margin))
            root.savedGeo = null
            root.lastFullscreenExitMs = Date.now()
            playlistDrawer.shown = restore.drawerShown
            // Same pin/clock hygiene as the plain fullscreen-exit path:
            // theater keeps its drawer pinned, and no stale peek clock may
            // close a drawer just restored under the dragged-out window.
            Native.setFullscreenWindow(false, nx, ny, restore.width, restore.height)
            Native.startMove()
            return
        }
        if (Native.isMaximized()) {
            // Windows caption-drag behavior: drop out of maximized, then
            // re-anchor so the grab point stays under the cursor.
            Native.toggleMaximize()
            if (root.preMaxGeo) {
                const g = root.preMaxGeo
                root.preMaxGeo = null
                const screenX = root.x + winX
                const screenY = root.y + winY
                let nx = Math.round(screenX - winX * g.width / Math.max(1, root.width))
                let ny = Math.round(screenY - winY)
                const margin = 40
                nx = Math.max(-(g.width - 2 * margin), Math.min(nx, root.Screen.desktopAvailableWidth - margin))
                ny = Math.max(0, Math.min(ny, root.Screen.desktopAvailableHeight - margin))
                root.x = nx
                root.y = ny
                root.width = g.width
                root.height = g.height
            }
            Native.startMove()
            return
        }
        Native.startMove()
    }

    // Maximize toggle from the caption (button / double-click). The saved
    // rectangle lets captionDrag re-anchor a drag-out of maximized even when
    // Qt's own restored geometry is not yet propagated in the same turn.
    property var preMaxGeo: null
    function toggleMaximized() {
        if (Native.isMaximized()) {
            root.preMaxGeo = null
        } else {
            root.preMaxGeo = { x: root.x, y: root.y, width: root.width, height: root.height }
        }
        Native.toggleMaximize()
    }

    // F2 window auto-fit: on a fresh file, resize the windowed player to the
    // video's display size (mpv dwidth/dheight), capped at 0.9x of the
    // desktop's available area, upscaled only to meet the
    // window minimums, and re-clamped fully on-screen. Skipped for
    // audio-only files (0x0), fullscreen, theater and maximized windows.
    // Display dimensions arrive asynchronously, so the observed
    // property change is the trigger (Connections below); perFileFitted
    // keeps it to one attempt per file and is reset in onFileLoaded.
    function fittedVideoSize(videoWidth, videoHeight, availableWidth, availableHeight) {
        // Apply one scale to both axes. Independent minimum clamps add
        // letterboxing to small, portrait and ultrawide videos.
        const minimumScale = Math.max(root.minimumWidth / videoWidth,
                                      root.minimumHeight / videoHeight)
        const scale = Math.min(Math.max(1.0, minimumScale),
                               0.9 * availableWidth / videoWidth,
                               0.9 * availableHeight / videoHeight)
        return Qt.size(Math.max(root.minimumWidth, Math.round(videoWidth * scale)),
                       Math.max(root.minimumHeight, Math.round(videoHeight * scale)))
    }

    function fitWindowToVideo() {
        if (root.perFileFitted)
            return
        if (Player.videoWidth <= 0 || Player.videoHeight <= 0)
            return
        if (root.miniPlayer) {
            // A new file keeps the mini player; only its aspect follows.
            root.placeMiniPlayer(root.miniCorner)
            root.perFileFitted = true
            return
        }
        if (root.isFullscreen || root.theaterMode || Native.isMaximized())
            return
        const size = root.fittedVideoSize(Player.videoWidth, Player.videoHeight,
                                         root.Screen.desktopAvailableWidth,
                                         root.Screen.desktopAvailableHeight)
        root.width = size.width
        root.height = size.height
        // Same origin convention as dragFromFullscreen: keep the fitted
        // rectangle inside the desktop's available area.
        root.x = Math.max(0, Math.min(root.x, root.Screen.desktopAvailableWidth - root.width))
        root.y = Math.max(0, Math.min(root.y, root.Screen.desktopAvailableHeight - root.height))
        root.perFileFitted = true
    }

    // A manual open refreshes the shared playback idle clock. Windowed
    // opens also arm hover-peek until explicitly closed by the user.
    function togglePlaylist() {
        if (playlistDrawer.shown) {
            playlistDrawer.shown = false
        } else {
            if (!root.peekMode)
                root.playlistAutoPeek = true
            // Manual opens share the playback controls' idle deadline.
            playlistDrawer.shown = true
        }
    }

    function dismissPlaybackChrome() {
        root.drawerAutoClosing = true
        if (!root.theaterMode) playlistDrawer.shown = false
        root.drawerAutoClosing = false
        osd.hideNow()
    }

    function requestFullscreenToggle() {
        // This is exactly the command bound to F in assets/input.conf.
        Player.command(["script-message", "app-toggle-fullscreen"])
    }

    function urlToPath(u) {
        var s = u.toString()
        if (s.indexOf("file:///") === 0)
            return decodeURIComponent(s.substring(Qt.platform.os === "windows" ? 8 : 7))
        return s
    }

    // F16 folder auto-enqueue: opening a video also queues every other media
    // file in its folder, in natural (sequential) order, and plays the
    // opened one at its place in that order. Single-file folders and any
    // mismatch fall back to a plain open.
    function sameMediaPath(a, b) {
        if (Qt.platform.os === "windows")
            return a.replace(/\//g, "\\").toLowerCase() === b.replace(/\//g, "\\").toLowerCase()
        return a === b
    }

    function openWithFolderEnqueue(path) {
        // A folder plays its media files in natural order.
        const expanded = Player.expandMediaPaths([path])
        if (expanded.length !== 1 || expanded[0] !== path) {
            if (expanded.length === 0)
                root.showHud("No videos in that folder")
            else
                Player.playlistAppendAndPlay(expanded, 0)
            return
        }
        // General pane toggle: off means "just play this one file", same
        // path a single-file folder already takes below.
        if (!Settings.flag("general/folderAutoEnqueue", true)) {
            Player.openFile(path)
            return
        }
        const siblings = Player.siblingMediaFiles(path)
        let idx = -1
        for (var i = 0; i < siblings.length; i++) {
            if (root.sameMediaPath(siblings[i], path)) {
                idx = i
                break
            }
        }
        if (idx < 0) {
            Player.openFile(path)
            return
        }
        // Append with NO autostart, then choose the entry ourselves. The old
        // shape append-played the batch and pause-guarded it, but pause only
        // stops playback, never the load: every sibling still opened and got
        // aborted by the next one, so a CLI open flashed the wrong files and
        // logged a "Loading failed." per sibling (A25). Passive appends make
        // selected entry ID the only thing that ever starts a load, even
        // when this folder is appended to an existing queue.
        Player.playlistAppendAndPlay(siblings, idx)
    }

    // F40 CLI file open: runs when the video surface's mpv render context
    // exists (MpvItem.onRenderContextReady). The guard keeps a Renderer
    // re-create (which re-emits the signal) from opening the file twice.
    property bool startupFileOpened: false
    function openStartupFile() {
        if (root.startupFileOpened)
            return
        if (typeof startupFile === "undefined" || startupFile === "")
            return
        root.startupFileOpened = true
        console.info("startup: opening CLI file " + startupFile)
        // --no-enqueue opens the single file; the default runs F16 folder
        // auto-enqueue exactly like the Open dialog.
        if (typeof startupEnqueue !== "undefined" && !startupEnqueue)
            Player.openFile(startupFile)
        else
            root.openWithFolderEnqueue(startupFile)
    }

    // The Open dialog retains folder enqueue. Explicit drop targets below
    // append only the selected files and choose whether to start them.
    // One file behaves like it always has (F16 folder auto-enqueue); several
    // are an explicit choice, so they become the queue verbatim rather than
    // being buried among their folder siblings. Recents are not touched
    // here: main.cpp adds whatever actually loads on the fileLoaded signal.
    function openPaths(paths) {
        if (!paths || paths.length === 0)
            return
        if (paths.length === 1)
            root.openWithFolderEnqueue(paths[0])
        else
            Player.playlistAppendFiles(Player.expandMediaPaths(paths))
    }

    // A later launch handed its files to this window (main.cpp
    // SingleInstance). Same policy as the command line: one file gets the
    // folder enqueue unless the sender passed --no-enqueue.
    function openExternal(paths, enqueue) {
        if (!paths || paths.length === 0)
            return
        if (paths.length === 1 && !enqueue)
            Player.openFile(paths[0])
        else
            root.openPaths(paths)
    }

    // Windows uses the native COM picker (NativeWindowWin32) so
    // QtQuick.Dialogs stays out of startup and the first Open command.
    // Everywhere else (A29, plan §4.4) the deferred QML picker does the job;
    // it loads the module only on its first open.
    function openMediaDialog() {
        if (Qt.platform.os === "windows") {
            root.openPaths(Native.openMediaFilesDialog())
            return
        }
        filePickers.openMediaFiles()
    }

    function applyShaderPath(path) {
        if (path === "")
            return
        const shaders = Settings.appliedShaders()
        if (shaders.indexOf(path) < 0)
            shaders.push(path)
        Settings.setAppliedShaders(shaders)
        Preferences.setShadersEnabled(true)
    }

    function openShaderDialog() {
        if (Qt.platform.os === "windows")
            return root.applyShaderPath(Native.openShaderFileDialog())
        filePickers.openShader()
    }

    function baseName(p) {
        if (!p)
            return ""
        var k = Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\"))
        return k >= 0 ? p.substring(k + 1) : p
    }

    function isSubtitlePath(p) {
        if (!p)
            return false
        const s = p.toLowerCase()
        const exts = [".srt", ".ass", ".ssa", ".vtt", ".sub", ".idx", ".sup", ".smi", ".txt"]
        for (var i = 0; i < exts.length; i++) {
            if (s.endsWith(exts[i]))
                return true
        }
        return false
    }

    function loadSubtitlePath(path) {
        if (!path || path === "")
            return
        Player.loadSubtitleFile(path)
        root.showHud("Subtitles loaded: " + root.baseName(path))
    }

    function openSubtitleDialog() {
        if (Qt.platform.os === "windows") {
            const path = Native.openSubtitleFileDialog()
            if (path && path !== "")
                root.loadSubtitlePath(path)
            return
        }
        filePickers.openSubtitle()
    }

    // Creating mpv's OpenGL render context is the expensive part of cold
    // startup.  Defer it one event turn so the application window and its
    // controls can present a first frame immediately instead of appearing
    // frozen while the driver initializes.
    Loader {
        id: videoLoader
        anchors.top: root.theaterLayout ? titleBar.bottom : parent.top
        anchors.bottom: root.theaterLayout ? osd.top : parent.bottom
        anchors.left: parent.left
        anchors.right: root.theaterDocked ? playlistDrawer.left : parent.right
        active: root.startupVideoReady
        sourceComponent: Component {
            MpvItem {
                anchors.fill: parent
                player: Player
                // F40: the CLI file starts only once a mpv render context
                // actually exists. Issuing loadfile earlier loses the race:
                // mpv builds its vo chain while the context is still missing,
                // logs "No render context set", and plays the file
                // audio-only forever ("Video: no video"). item creation
                // (onLoaded) is NOT enough — the context appears on the
                // item's first render pass, one or more frames later.
                onRenderContextReady: root.openStartupFile()
            }
        }
    }

    SubtitleSafeArea {
        player: Player
        viewportHeight: videoLoader.height
        // Theater already places controls below the video. Keep the margin
        // through fade-out so subtitles never drop behind a fading bar.
        coveredHeight: !root.theaterLayout && osd.painting ? osd.height + 8 : 0
    }

    // Cursor auto-hide during playback: the pointer goes blank once the OSD
    // is gone and wakes on movement. F25: mid-video movement no longer
    // summons the chrome — the top/bottom edge strips do that (hover-peek,
    // like the playlist drawer).
    MouseArea {
        id: surfaceArea
        z: 1
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property bool cursorAwake: false

        // F6d drag-pan state. didPan suppresses the pause-click that would
        // otherwise fire when the drag ends.
        property real dragStartX: 0
        property real dragStartY: 0
        property real panBaseX: 0
        property real panBaseY: 0
        property bool didPan: false

        cursorShape: {
            if (root.videoZoom > 0)
                return pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            return (Player.state === root.statePlaying && !osd.shown && !cursorAwake)
                   ? Qt.BlankCursor : Qt.ArrowCursor
        }

        onPressed: (mouse) => {
            surfaceArea.dragStartX = mouse.x
            surfaceArea.dragStartY = mouse.y
            surfaceArea.panBaseX = root.videoPanX
            surfaceArea.panBaseY = root.videoPanY
            surfaceArea.didPan = false
        }

        onPositionChanged: (mouse) => {
            cursorAwake = true
            cursorIdleTimer.restart()
            if (!pressed || root.videoZoom <= 0)
                return
            const dx = mouse.x - surfaceArea.dragStartX
            const dy = mouse.y - surfaceArea.dragStartY
            if (!surfaceArea.didPan && Math.abs(dx) + Math.abs(dy) < 4)
                return
            surfaceArea.didPan = true
            // video-pan-* is a fraction of the SCALED video, so the pixel
            // delta is divided by the on-screen size at the current zoom
            // (video-zoom is log2, hence the power of two).
            const scale = Math.pow(2, root.videoZoom)
            root.applyPan(surfaceArea.panBaseX + dx / (width * scale),
                          surfaceArea.panBaseY + dy / (height * scale))
        }
        Timer {
            id: cursorIdleTimer
            interval: root.osdHideMs
            onTriggered: surfaceArea.cursorAwake = false
        }
        // Single and double click need to be told apart: pause on single,
        // fullscreen on double. A short timer defers the pause decision
        // until the double-click window has passed.
        Timer {
            id: clickTimer
            interval: 260
            onTriggered: {
                if (osd.hasOpenPopup()) {
                    osd.closePopups()
                    if (playlistDrawer.shown && !root.theaterMode)
                        root.dismissPlaybackChrome()
                } else if (playlistDrawer.shown && !root.theaterMode) {
                    // Clicking the video is a pause click that happens to
                    // close the drawer, not a playlist close (F19): mark it
                    // implicit so the armed hover-peek state survives. It
                    // used to only close the drawer, so a pointer returning
                    // over the right edge (which re-peeks the drawer) lost
                    // its first click.
                    root.dismissPlaybackChrome()
                    if (Player.currentFile !== "") Player.togglePause()
                } else if (Player.currentFile !== "") {
                    Player.togglePause()
                }
            }
        }
        onClicked: (mouse) => {
            surfaceArea.forceActiveFocus()
            if (mouse.button === Qt.RightButton) {
                contextMenu.openAt(mouse.x, mouse.y)
            } else if (!surfaceArea.didPan) {
                // A drag that ends over the video is a pan, not a pause.
                clickTimer.restart()
            }
        }
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                clickTimer.stop()
                root.requestFullscreenToggle()
            }
        }
    }

    // A29 (plan §4.1): the F1 border-resize strips, restored for
    // non-Windows. Windows gets the same 8 px zone from the WM_NCHITTEST
    // subclass in NativeWindowWin32; everywhere else these eight areas hand
    // each edge to QWindow::startSystemResize() (INativeWindow Rule 1 —
    // never a computed move). hoverEnabled stays false so hover falls
    // through to the F25/F34 summon strips underneath; cursorShape applies
    // while pressed, matching the pre-F1 strips.
    Item {
        z: 2
        anchors.fill: parent
        visible: Qt.platform.os !== "windows"
        enabled: visible && !root.isFullscreen

        MouseArea {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 8
            cursorShape: Qt.SizeVerCursor
            onPressed: Native.startResize(Qt.TopEdge)
        }
        MouseArea {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 8
            cursorShape: Qt.SizeVerCursor
            onPressed: Native.startResize(Qt.BottomEdge)
        }
        MouseArea {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 8
            cursorShape: Qt.SizeHorCursor
            onPressed: Native.startResize(Qt.LeftEdge)
        }
        MouseArea {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 8
            cursorShape: Qt.SizeHorCursor
            onPressed: Native.startResize(Qt.RightEdge)
        }
        MouseArea {
            anchors.top: parent.top
            anchors.left: parent.left
            width: 8
            height: 8
            cursorShape: Qt.SizeFDiagCursor
            onPressed: Native.startResize(Qt.TopEdge | Qt.LeftEdge)
        }
        MouseArea {
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: 8
            height: 8
            cursorShape: Qt.SizeFDiagCursor
            onPressed: Native.startResize(Qt.BottomEdge | Qt.RightEdge)
        }
        MouseArea {
            anchors.top: parent.top
            anchors.right: parent.right
            width: 8
            height: 8
            cursorShape: Qt.SizeBDiagCursor
            onPressed: Native.startResize(Qt.TopEdge | Qt.RightEdge)
        }
        MouseArea {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            width: 8
            height: 8
            cursorShape: Qt.SizeBDiagCursor
            onPressed: Native.startResize(Qt.BottomEdge | Qt.LeftEdge)
        }
    }

    // F25 chrome summon strips — hover-peek for the bars, the same model as
    // the playlist drawer: hovering near the top edge brings the caption out
    // (fullscreen; in windowed/theater the caption never hides), hovering
    // near the bottom edge brings the control bar out in every mode. The
    // right 28 px stay reserved for the drawer summon strip. Clicks pass
    // through (NoButton). The OSD's own 400 ms post-hide cooldown plus the
    // "no event while stationary" hover semantics keep a parked cursor from
    // re-show/hide flicker.
    // F34/F39 chrome summon strips — hover-peek for the bars, the same model
    // as the playlist drawer: hovering near the top edge brings the caption
    // out (in every mode; theater pins it anyway), hovering near the bottom
    // edge brings the control bar out. The right 28 px stay reserved for the
    // drawer summon strip. Clicks pass through (NoButton).
    //
    // Anti-flicker: when a bar auto-hides with the cursor parked inside its
    // strip, the hover re-evaluation would re-summon it in a loop. Each hide
    // stamps a *_HideMs time; a summon inside the 300 ms cool-off is ignored,
    // and moving the mouse afterwards (positionChanged) retries.
    property real lastChromeHideMs: 0
    // When the last keyboard-owning popup (menu, picker) closed.
    property real lastPopupCloseMs: 0

    Connections {
        target: Shortcuts
        function onMenuOpenChanged() {
            if (!Shortcuts.menuOpen)
                root.lastPopupCloseMs = Date.now()
        }
    }
    property real lastTopHideMs: 0

    function summonChromeBars() {
        if (Date.now() - root.lastChromeHideMs > 300)
            osd.showControlsNow()
    }

    function summonTopBar() {
        if (Date.now() - root.lastTopHideMs > 300) {
            root.topBarShown = true
            topBarTimer.restart()
        }
    }

    MouseArea {
        z: 3
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        // Full width, including the rightmost 28 px under the drawer summon
        // strip: that strip is z-stacked above this one but is disabled
        // outside peekMode/playlistAutoPeek, and even when active it now
        // relays into summonTopBar() itself (below). Without this the top-
        // right corner — right under the window close button — was a dead
        // zone that revealed neither bar.
        height: 28
        // F34: the top edge summons ONLY the caption (its own clock); the
        // bottom edge summons ONLY the control bar.
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onContainsMouseChanged: if (containsMouse) root.summonTopBar()
        onPositionChanged: root.summonTopBar()
    }

    MouseArea {
        z: 3
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.rightMargin: 28
        height: 64
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onContainsMouseChanged: if (containsMouse) root.summonChromeBars()
        onPositionChanged: root.summonChromeBars()
    }

    // F3/F19 hover-peek summon strip: hovering the right 28 px of the window
    // opens the playlist drawer. Active in theater/fullscreen (peekMode) and
    // — per F19 — in windowed mode while the playlist is toggled on
    // (playlistAutoPeek; the extra !isFullscreen/!theaterMode term is the
    // windowed-only complement of peekMode). It sits above the video
    // surface but below every chrome layer (HUD, OSD, titlebar, drawer) and
    // accepts no buttons, so clicks and the wheel fall through to the video
    // surface (pause, context menu, double-click fullscreen, volume).
    MouseArea {
        id: drawerPeekZone
        z: 3
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 28
        visible: root.peekMode || (root.playlistAutoPeek && !root.isFullscreen && !root.theaterMode)
        enabled: root.peekMode || (root.playlistAutoPeek && !root.isFullscreen && !root.theaterMode)
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onContainsMouseChanged: {
            if (!containsMouse)
                return
            osd.showControls()
            // This strip sits above the top-bar summon strip and is the
            // same width as the top-right corner (close button); without
            // this it stole hover there without ever summoning the top bar.
            root.summonTopBar()
            // A close that happened under a cursor parked on this strip
            // (ESC, the drawer's close button, p) replays this same enter
            // transition a moment later; require a real leave first.
            if (Date.now() - root.lastDrawerCloseMs < 350)
                return
            if (!playlistDrawer.shown) {
                playlistDrawer.shown = true
            }
            osd.showControlsNow()
        }
        onPositionChanged: root.summonTopBar()
    }

    DropArea {
        id: dropArea
        anchors.fill: parent
        z: 30
        onEntered: (drop) => {
            root.dropCount = (drop.hasUrls && drop.urls.length > 0) ? drop.urls.length : 0
            drop.accepted = root.dropCount > 0
            root.dropIsSubtitle = (root.dropCount === 1 && Player.currentFile !== ""
                                   && root.isSubtitlePath(root.urlToPath(drop.urls[0])))
            root.dropChoice = mediaDrop.choiceAt(drop.x, drop.y)
        }
        onPositionChanged: (drop) => {
            root.dropChoice = mediaDrop.choiceAt(drop.x, drop.y)
        }
        onExited: {
            root.dropCount = 0
            root.dropIsSubtitle = false
            root.dropChoice = 0
        }
        onDropped: (drop) => {
            const choice = mediaDrop.choiceAt(drop.x, drop.y)
            root.dropCount = 0
            root.dropIsSubtitle = false
            root.dropChoice = 0
            if (drop.hasUrls && drop.urls.length > 0) {
                var paths = []
                for (var i = 0; i < drop.urls.length; i++) {
                    var p = root.urlToPath(drop.urls[i])
                    if (p !== "")
                        paths.push(p)
                }
                if (paths.length === 1 && Player.currentFile !== "" && root.isSubtitlePath(paths[0])) {
                    root.loadSubtitlePath(paths[0])
                    drop.accept(Qt.CopyAction)
                    return
                }
                paths = Player.expandMediaPaths(paths)
                if (paths.length === 0) {
                    root.showHud("No videos in that folder")
                    drop.accept(Qt.CopyAction)
                    return
                }
                if (choice === 1) Player.playlistAppendAndPlay(paths)
                else Player.playlistAppendFiles(paths, false)
                root.showHud(choice === 1 ? "Added to playlist. Playing now."
                    : "Added " + paths.length + (paths.length === 1 ? " file" : " files") + " to playlist")
                drop.accept(Qt.CopyAction)
            }
        }
    }

    MediaDropOverlay {
        id: mediaDrop
        z: 31
        anchors.fill: parent
        fileCount: root.dropCount
        subtitle: root.dropIsSubtitle
        activeChoice: root.dropChoice
    }

    // A CLI/Explorer-opened file is not handed to mpv until the render
    // context exists; until then the player is Idle with no file, which
    // must read as "opening", not as an empty player.
    readonly property bool startupFilePending: typeof startupFile !== "undefined"
                                               && startupFile !== "" && !root.startupFileOpened

    // Steady-state empty player: a real Open affordance instead of a black
    // rectangle with no hint. Also covers the pre-chrome startup frame.
    // Idle-only: a CLI-opened file is in Loading long before currentFile is
    // set, so the button must not ghost over the incoming video.
    Rectangle {
        anchors.fill: videoLoader
        z: 1
        color: root.cBg
        visible: Player.state === root.stateIdle && !root.startupFilePending
    }
    Item {
        objectName: "openPlaceholder"
        z: 2
        anchors.centerIn: videoLoader
        visible: Player.state === root.stateIdle
                 && !root.startupFilePending
        width: openButton.width
        height: openButton.height + 28

        Rectangle {
            id: openButton
            activeFocusOnTab: visible
            Accessible.role: Accessible.Button
            Accessible.name: "Open file"
            Accessible.onPressAction: root.openMediaDialog()
            Keys.onReturnPressed: root.openMediaDialog()
            Keys.onSpacePressed: root.openMediaDialog()
            width: Math.max(160, openLabel.implicitWidth + 36)
            height: 40
            radius: 8
            color: openArea.containsMouse ? root.cBar : "#33181818"
            border.color: openArea.containsMouse || activeFocus ? root.cAccent : "#33FFFFFF"
            border.width: 1

            Text {
                id: openLabel
                anchors.centerIn: parent
                text: "Open file…"
                color: root.cText
                font.pixelSize: 14
                font.family: Fonts.ui
            }

            MouseArea {
                id: openArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.openMediaDialog()
            }
        }

        Text {
            anchors.top: openButton.bottom
            anchors.topMargin: 8
            anchors.horizontalCenter: openButton.horizontalCenter
            text: "or drop media here  ·  press O"
            color: root.cText
            opacity: 0.55
            font.pixelSize: 12
            font.family: Fonts.ui
        }
    }

    // Something is opening: a turning ring where the Open button was, or
    // over the last frame on a slow switch, so a long demuxer open never
    // looks like the click was ignored.
    Item {
        id: loadingIndicator
        objectName: "loadingIndicator"
        z: 2
        anchors.centerIn: videoLoader
        width: spinner.width
        height: spinner.height
        readonly property bool loading: root.startupFilePending || Player.state === root.stateLoading
        visible: loading
        LoadingSpinner { id: spinner; running: loadingIndicator.visible }
    }

    // Audio files (no video track, or only a cover picture) get the audio
    // view: cover, tags and a whole-track waveform. It is opaque, so mpv's
    // full-window cover underneath never shows.
    readonly property bool audioView: Player.currentFile !== "" && Player.state !== root.stateIdle
                                      && Player.state !== 1 && Player.state !== 6
                                      && !root.hasMotionVideo
                                      && Player.trackList.some(track => track.type === "audio")
    readonly property bool hasMotionVideo: Player.trackList.some(track => track.type === "video" && !track.albumart)
    onAudioViewChanged: audioView ? AudioInfo.load(Player.currentFile) : AudioInfo.clear()
    Connections {
        target: Player
        function onFileLoaded() { if (root.audioView) AudioInfo.load(Player.currentFile) }
    }
    Loader {
        id: audioViewLoader
        objectName: "audioPlaceholder"
        z: 2
        anchors.fill: videoLoader
        active: root.audioView
        sourceComponent: AudioView {
            fallbackTitle: MediaNames.display(Player.currentFile, MediaNames.cleanupEnabled)
            bottomReserve: root.theaterLayout ? 16 : 96
        }
    }

    readonly property bool hasVideoTrack: {
        var tl = Player.trackList
        for (var i = 0; i < tl.length; i++)
            if (tl[i].type === "video")
                return true
        return false
    }
    PlaybackFailure {
        anchors.fill: videoLoader
        z: 3
        visible: Player.state === 6 && Player.playbackError !== ""
        fileName: root.baseName(Player.failedFile)
        message: Player.playbackError
        onRetryRequested: Player.retryFailedFile()
        onOpenRequested: root.openMediaDialog()
    }

    DeferredTitleBar {
        id: titleBar
        active: root.startupChromeReady
        z: 10
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        // F34: the caption has its own hover-peek clock (topBarShown),
        // independent of the OSD's. Theater pins it visible.
        readonly property bool wanted: root.theaterLayout || root.topBarShown
        // visible outlives `wanted` until the fade below has actually run:
        // binding it straight to `wanted` flipped the item off in the same
        // instant opacity went to 0, so the fade never rendered and
        // every caption hide was an instant vanish. Same fade-then-hide the
        // OSD does (OnScreenController's onOpacityChanged), expressed as a
        // binding because opacity is the only input.
        visible: wanted || opacity > 0.01
        opacity: wanted ? 1 : 0
        // enabled tracks intent, not the fade: a caption on its way out must
        // stop taking clicks immediately.
        enabled: wanted

        Behavior on opacity {
            NumberAnimation { duration: Native.animationsEnabled() ? (titleBar.wanted ? 140 : 180) : 0; easing.type: Easing.OutQuad }
        }
        onInteraction: {
            root.topBarShown = true
            topBarTimer.restart()
        }
    }

    DeferredOnScreenController {
        id: osd
        active: root.startupChromeReady
        fullscreenActive: root.isFullscreen
        playlistActive: playlistDrawer.shown
        playlistInteracting: playlistDrawer.interacting || root.theaterMode
        enabled: !settingsDialog.active
        timeDisplayMode: root.timeDisplayMode
        bookmarks: root.bookmarkList
        loopFile: root.loopFile
        playlistRepeat: root.playlistRepeat
        aspectIndex: root.aspectIdx
        // Bottom letterbox under a wider-than-window picture (no zoom/pan).
        letterbox: root.theaterLayout || Player.videoWidth <= 0 || Player.videoHeight <= 0 ? 0
            : Math.max(0, (videoLoader.height - videoLoader.width * Player.videoHeight / Player.videoWidth) / 2)
        z: 12
        anchors.left: parent.left
        // F35: the bar spans the full window width and paints OVER the
        // floating drawer (drawer z:9). Theater with the playlist docked is
        // different: the controls belong to the video column and the dock
        // runs the full height beside them.
        anchors.right: root.theaterDocked ? playlistDrawer.left : parent.right
        anchors.bottom: parent.bottom
        height: root.theaterLayout ? 72 : 96
        onFullscreenRequested: root.requestFullscreenToggle()
        onPlaylistRequested: root.togglePlaylist()
        onOpenSubtitleRequested: root.openSubtitleDialog()
        onSettingsRequested: { playlistDrawer.shown = false; settingsDialog.openDialog() }
        // Both overlays dismiss in the same event turn and share a fade.
        onHidden: {
            root.lastChromeHideMs = Date.now()
            if (playlistDrawer.shown && !root.theaterMode) {
                root.drawerAutoClosing = true
                playlistDrawer.shown = false
                root.drawerAutoClosing = false
            }
        }
    }

    DeferredPlaylistDrawer {
        id: playlistDrawer
        sidebar: root.theaterLayout
        enabled: !settingsDialog.active
        active: root.startupChromeReady
        onShowHudRequested: (text) => root.showHud(text)
        z: 9
        anchors.right: parent.right
        // Theater docks the shelf from under the caption to the window
        // bottom; the control band stays in the video column beside it.
        anchors.rightMargin: root.theaterLayout ? 0 : (root.width < 700 ? 16 : 28)
        anchors.bottom: root.theaterLayout ? parent.bottom : osd.top
        anchors.bottomMargin: root.theaterLayout ? 0 : 12
        height: root.theaterLayout ? Math.max(172, root.height - titleBar.height) : root.playlistHeight
        width: root.theaterLayout ? root.theaterPlaylistWidth : root.width - 2 * (root.width < 700 ? 16 : 28)
        onInteraction: {
            if (playlistDrawer.shown) osd.showControlsNow()
            // All shelf interaction refreshes the single playback idle clock.
        }
        onShownChanged: {
            if (playlistDrawer.shown) osd.showControlsNow()
            if (!playlistDrawer.shown) {
                // Mark every close so the summon strip ignores the hover
                // event generated by removing an overlay under the pointer.
                root.lastDrawerCloseMs = Date.now()
                // F19: a manual close in windowed mode (p, OSD ☰, the
                // drawer's ✕, ESC) disarms the hover-peek state. Implicit
                // closes (shared OSD auto-hide or a video click)
                // arrive with drawerAutoClosing set and keep it armed so
                // the next hover re-opens. Closes in peek modes never
                // touch the windowed-only state.
                if (!root.peekMode && !root.drawerAutoClosing)
                    root.playlistAutoPeek = false
            }
        }
    }

    Connections {
        target: Settings
        function onSettingChanged(key, value) {
            if (key === "interface/osdHideMs") root.osdHideMs = Number(value)
            else if (key === "general/playlistRepeat") root.playlistRepeat = value === true || value === "true"
        }
    }

    Connections {
        target: Preferences
        function onFileDefaultsChanged() { root.aspectIdx = Preferences.defaultAspectIndex }
    }

    Connections {
        target: Playlist
        function onErrorOccurred(message) {
            root.errorMessage = message
            errorTimer.restart()
        }
    }

    Timer {
        id: toolWarmup
        interval: 500
        repeat: true
        property int stage: 0
        running: root.startupChromeReady && stage < 3 && !Player.scrubbing
                 && Player.state !== 1 && Player.state !== 2
                 && (startupFile === "" || root.startupFileOpened)
                 && (Player.currentFile === "" || Player.videoWidth === 0 || Player.videoPresented)
        onTriggered: {
            if (stage === 0) {
                settingsDialog.preload()
                if (settingsDialog.ready) stage++
            } else if (stage === 1) {
                playlistDrawer.preload()
                if (playlistDrawer.ready) stage++
            } else {
                osd.preloadMenus()
                stage++
            }
        }
    }

    // Keyboard shortcut guide: shown once on first launch, then on F1 or
    // from the right-click menu. Built on demand like the other dialogs.
    property bool firstRunGuidePending: !(typeof selftestActive !== "undefined" && selftestActive)
                                       && !Settings.flag("interface/shortcutGuideSeen", false)
    DeferredShortcutGuide {
        id: shortcutGuide
        z: 15
        onDismissed: {
            root.firstRunGuidePending = false
            firstRunGuideTimer.stop()
            Settings.setValue("interface/shortcutGuideSeen", true)
            Player.setInteractionPause(false)
        }
    }
    onStartupChromeReadyChanged: {
        if (startupChromeReady && root.firstRunGuidePending)
            firstRunGuideTimer.start()
    }
    // Let the first frame and controls settle before the guide appears.
    Timer {
        id: firstRunGuideTimer
        interval: 600
        onTriggered: {
            if (!root.firstRunGuidePending) return
            if (settingsDialog.active) restart()
            else shortcutGuide.openGuide()
        }
    }

    // F6b. Above the chrome (OSD is z:12) so the panel is never half-hidden
    // behind the control bar, and it owns the keyboard while open.
    DeferredGoToDialog {
        id: goToDialog
        z: 14
    }

    DeferredPlaybackInfo {
        id: playbackInfo
        z: 14
    }

    // F9. Opening goes through openPaths() like every other entry point, so
    // a pasted local path still gets the F16 folder enqueue and a URL falls
    // through to openFile.
    DeferredOpenUrlDialog {
        id: openUrlDialog
        z: 14
        onAccepted: function(target) {
            root.openPaths([target])
            root.showHud(target.indexOf("://") > 0 ? "Opening stream…"
                                                   : "Opening…")
        }
    }

    // F11/F47. The biggest QML object graph in the app; deferred so it does
    // not exist until someone opens it.
    DeferredSettingsDialog {
        id: settingsDialog
        z: 14
        onActiveChanged: if (!active) { surfaceArea.forceActiveFocus(); osd.showControlsNow() }
        onOpenShaderRequested: root.openShaderDialog()
    }

    // A29 (plan §4.4): the non-Windows file pickers. Not a visual child;
    // no z needed. Windows never activates the Loader.
    DeferredFileDialog {
        id: filePickers
        onAccepted: function(files) {
            // Raw URLs, converted here like a DropArea drop.
            var paths = []
            for (var i = 0; i < files.length; i++) {
                var p = root.urlToPath(files[i])
                if (p !== "")
                    paths.push(p)
            }
            if (paths.length === 1 && filePickers.mode === "shader")
                root.applyShaderPath(paths[0])
            else if (paths.length === 1 && filePickers.mode === "subtitle")
                root.loadSubtitlePath(paths[0])
            else
                root.openPaths(paths)
        }
    }

    DeferredContextMenu {
        id: contextMenu
        objectName: "contextMenu"
        model: Menus.buildMenu(MenuFiles)
        toggleStates: ({
            "script-message app-loop-file": root.loopFile,
            "script-message app-toggle-theater": root.theaterMode,
            "script-message app-toggle-fullscreen": root.isFullscreen,
            "script-message app-always-on-top": Native.alwaysOnTop,
            "script-message app-mini-player": root.miniPlayer,
            "script-message app-toggle-playlist": playlistDrawer.shown,
            "cycle mute": Player.isMuted
        })
        // Inline values on the root rows that open a submenu.
        valueHints: ({
            "Speed": osd.speedLabel(Player.speed),
            "Audio": osd.currentAudioLabel,
            "Subtitles": osd.currentSubtitleLabel,
            "Aspect ratio": osd.aspectLabels[root.aspectIdx] || "",
            "Window": root.miniPlayer ? "Mini" : Native.alwaysOnTop ? "On top" : ""
        })
        aspectLabels: osd.aspectLabels
        miniCorner: root.miniCorner
        onOpened: {
            osd.menuOpen = true
            // Suppress mpv key forwarding while the popup owns the keyboard.
            Shortcuts.menuOpen = true
        }
        onClosed: {
            osd.menuOpen = false
            Shortcuts.menuOpen = false
        }
    }

    Connections {
        target: Menus
        function onMenuChanged() { contextMenu.model = Menus.buildMenu(MenuFiles) }
    }

    Rectangle {
        id: errorBox
        z: 12
        anchors.centerIn: videoLoader
        visible: root.errorMessage !== ""
        radius: 8
        color: root.cBar
        border.color: root.cDanger
        border.width: 1
        width: Math.min(Math.max(240, errorLabel.implicitWidth) + 32, videoLoader.width - 48)
        height: errorLabel.implicitHeight + 20

        Text {
            id: errorLabel
            anchors.centerIn: parent
            width: parent.width - 32
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            text: root.errorMessage
            color: root.cText
            font.pixelSize: 13
            font.family: Fonts.ui
        }

        // Click to dismiss: long mpv errors used to sit out their full 4 s
        // while swallowing the pause-toggle click underneath.
        MouseArea {
            anchors.fill: parent
            onClicked: {
                root.errorMessage = ""
                errorTimer.stop()
            }
        }
    }

    Timer {
        id: errorTimer
        interval: 4000
        onTriggered: root.errorMessage = ""
    }

    // Product and licensing details with an on-demand GitHub release check.
    Rectangle {
        id: aboutBox
        objectName: "aboutPanel"
        z: 14
        anchors.centerIn: parent
        visible: false
        width: Math.min(440, root.width - 48)
        height: Math.min(aboutColumn.implicitHeight + 40, root.height - 32)
        radius: 14
        color: "#171717"
        border.color: "#2EFFFFFF"
        border.width: 1
        Accessible.role: Accessible.Dialog
        Accessible.name: "About Frame"
        onVisibleChanged: {
            if (visible) {
                aboutScroll.contentY = 0
                aboutClose.forceActiveFocus(Qt.TabFocusReason)
            }
            else root.contentItem.forceActiveFocus()
        }
        Keys.onEscapePressed: visible = false

        MouseArea { anchors.fill: parent; z: -1; acceptedButtons: Qt.AllButtons }

        IconButton {
            id: aboutClose
            objectName: "aboutCloseButton"
            x: parent.width - 42; y: 10; z: 2
            iconSource: "qrc:/assets/icons/close.svg"
            accessibleName: "Close information"
            tooltipEnabled: true
            tooltipHints: [{ label: "Close information", keys: "ESC" }]
            onActivated: aboutBox.visible = false
        }
        Flickable {
            id: aboutScroll
            anchors.fill: parent; anchors.margins: 20
            contentWidth: width
            contentHeight: aboutColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            function reveal(control) {
                const y = control.mapToItem(aboutColumn, 0, 0).y
                contentY = Math.max(0, Math.min(contentHeight - height,
                    y < contentY ? y : y + control.height > contentY + height ? y + control.height - height : contentY))
            }
        Column {
            id: aboutColumn
            width: parent.width
            spacing: 15

            Row {
                width: parent.width; spacing: 12
                Image { width: 56; height: 44; source: "qrc:/assets/branding/concepts/frame-player-icon-v1.svg"; sourceSize: Qt.size(112,88); fillMode: Image.PreserveAspectFit }
                Column {
                    anchors.verticalCenter: parent.verticalCenter; spacing: 3
                    Text { text: "Frame"; color: "#F4F4F4"; font.family: Fonts.ui; font.pixelSize: 21; font.weight: Font.DemiBold }
                    Text { text: "Version " + Qt.application.version; color: "#A5A5A5"; font.family: Fonts.ui; font.pixelSize: 12 }
                }
            }
            Rectangle { width: parent.width; height: 1; color: "#22FFFFFF" }
            Column {
                width: parent.width; spacing: 5
                Text { text: "A calm place to watch your videos"; color: "#EEEEEE"; font.family: Fonts.ui; font.pixelSize: 14; font.weight: Font.Medium }
                Text { width: parent.width; text: "Frame plays your video files and streams with subtitles, playlists, and controls that stay out of the way."; wrapMode: Text.WordWrap; color: "#AAAAAA"; font.family: Fonts.ui; font.pixelSize: 12 }
                Text { text: "Developed by VahamCooKey"; color: "#BFBFBF"; font.family: Fonts.ui; font.pixelSize: 12; topPadding: 4 }
            }
            Rectangle {
                width: parent.width; height: licenseColumn.implicitHeight + 22; radius: 9; color: "#202020"; border.color: "#1FFFFFFF"
                Column {
                    id: licenseColumn
                    x: 12; y: 11; width: parent.width - 24; spacing: 4
                    Text { text: "Open-source licenses"; color: "#EAEAEA"; font.family: Fonts.ui; font.pixelSize: 12; font.weight: Font.Medium }
                    Text { width: parent.width; text: "Frame is licensed under GNU GPL v3. It uses Qt (LGPL v3) and mpv. See the repository’s license and notices for details."; wrapMode: Text.WordWrap; color: "#A5A5A5"; font.family: Fonts.ui; font.pixelSize: 12 }
                }
            }
            Row {
                width: parent.width; height: 38; spacing: 8
                Rectangle {
                    width: 38; height: 38; radius: 8; color: "#252525"
                    Image { anchors.centerIn: parent; width: 19; height: 19; source: "qrc:/assets/icons/github.svg"; sourceSize: Qt.size(38,38); opacity: 0.9 }
                }
                FlatButton {
                    objectName: "aboutRepositoryButton"
                    width: parent.width - 46; height: 38
                    label: "View Frame on GitHub"
                    onActiveFocusChanged: if (activeFocus) aboutScroll.reveal(this)
                    onActivated: ReleaseChecker.openRepository()
                }
            }
            Column {
                width: parent.width; spacing: 7
                Row {
                    width: parent.width; height: 34; spacing: 8
                    FlatButton {
                        objectName: "aboutUpdateButton"
                        width: Math.max(implicitWidth, (parent.width - 8) / 2)
                        label: "Check for updates"
                        primary: true
                        onActiveFocusChanged: if (activeFocus) aboutScroll.reveal(this)
                        onActivated: ReleaseChecker.check()
                    }
                    FlatButton {
                        objectName: "aboutReleasesButton"
                        width: (parent.width - 8) / 2
                        label: "Open releases"
                        onActiveFocusChanged: if (activeFocus) aboutScroll.reveal(this)
                        onActivated: ReleaseChecker.openReleases()
                    }
                }
                Text { width: parent.width; visible: ReleaseChecker.status !== ""; text: ReleaseChecker.status; wrapMode: Text.WordWrap; color: ReleaseChecker.updateAvailable ? "#BFE4CD" : "#A5A5A5"; font.family: Fonts.ui; font.pixelSize: 12 }
            }
        }
        }
    }

    // Backdrop: any click outside the panel dismisses it, same model as the
    // track menus. Blocks the surface click from firing underneath.
    MouseArea {
        z: 13
        anchors.fill: parent
        visible: aboutBox.visible
        enabled: aboutBox.visible
        onClicked: aboutBox.visible = false
    }

    Rectangle {
        id: playbackFeedback
        objectName: "playbackFeedback"
        property bool shown: false
        property bool paused: false
        z: 8
        anchors.centerIn: videoLoader
        width: 72; height: 72; radius: 36
        color: "#CC181818"
        border.color: "#25FFFFFF"
        visible: opacity > 0
        opacity: shown ? 1 : 0
        scale: shown ? 1 : 0.9
        Behavior on opacity { NumberAnimation { duration: Native.animationsEnabled() ? 160 : 0 } }
        Behavior on scale { NumberAnimation { duration: Native.animationsEnabled() ? 160 : 0; easing.type: Easing.OutCubic } }
        Image {
            anchors.centerIn: parent
            width: 30; height: 30
            source: playbackFeedback.paused ? "qrc:/assets/icons/pause.svg" : "qrc:/assets/icons/play.svg"
            sourceSize: Qt.size(30 * root.Screen.devicePixelRatio, 30 * root.Screen.devicePixelRatio)
        }
        Timer { id: playbackFeedbackTimer; interval: 650; onTriggered: playbackFeedback.shown = false }
        // No pointer handler: the feedback never blocks another click.
    }

    // Value feedback for keyboard-driven changes (UP/DOWN, =/-, m, [ ], z/x).
    Rectangle {
        objectName: "trackFeedback"
        z: 9
        anchors.left: videoLoader.left
        anchors.top: videoLoader.top
        anchors.leftMargin: 20
        anchors.topMargin: root.theaterLayout ? 20 : titleBar.height + 16
        width: Math.min(trackFeedbackLabel.implicitWidth + 28, Math.max(0, videoLoader.width - 40))
        height: 38
        radius: 8
        color: Colors.surface
        visible: root.trackFeedbackText !== "" && !subtitleLoadingNotice.shown
        Text {
            id: trackFeedbackLabel
            anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
            verticalAlignment: Text.AlignVCenter
            text: root.trackFeedbackText
            elide: Text.ElideRight
            color: root.cText; font.family: Fonts.ui; font.pixelSize: 13
        }
    }
    Timer {
        id: trackFeedbackTimer
        interval: 1800
        onTriggered: root.trackFeedbackText = ""
    }

    // A54. Screenshot notice, top-right, mirroring the track feedback's
    // top-left placement so neither overlaps the other or the caption.
    SubtitleLoadingNotice {
        id: subtitleLoadingNotice
        objectName: "subtitleLoadingNotice"
        z: 10
        anchors.left: videoLoader.left
        anchors.top: videoLoader.top
        anchors.leftMargin: 20
        anchors.topMargin: root.theaterLayout ? 20 : titleBar.height + 16
        width: Math.min(implicitWidth, Math.max(0, videoLoader.width - 40))
        height: implicitHeight
        loading: Player.subtitleLoading
        fontFamily: Fonts.ui
    }
    Connections {
        target: Player
        function onSubtitleLoadingFailed() {
            root.trackFeedbackText = qsTr("Could not load subtitles")
            trackFeedbackTimer.restart()
        }
    }
    ScreenshotToast {
        id: screenshotToast
        z: 13
        anchors.right: videoLoader.right
        anchors.top: videoLoader.top
        anchors.topMargin: root.theaterLayout ? 20 : titleBar.height + 16
        onRevealRequested: path => Native.revealInFileManager(path)
    }

    // These otherwise happen invisibly while the OSD is hidden.
    Rectangle {
        z: 9
        anchors.bottom: root.theaterLayout ? osd.top : parent.bottom
        anchors.bottomMargin: root.theaterLayout ? 8 : (osd.shown ? 118 : 24)
        anchors.horizontalCenter: parent.horizontalCenter
        width: hudLabel.implicitWidth + 28
        height: hudLabel.implicitHeight + 14
        radius: 8
        color: root.cBar
        visible: root.hudText !== ""
        opacity: visible ? 1 : 0

        // Frequent keyboard feedback should appear immediately.

        Text {
            id: hudLabel
            anchors.centerIn: parent
            text: root.hudText
            color: root.cText
            font.pixelSize: 13
            font.family: Fonts.ui
        }
    }

    Timer {
        id: hudTimer
        interval: 1100
        onTriggered: root.hudText = ""
    }

    // Lightweight feedback from confirmed playback state changes.
    Connections {
        target: Player
        ignoreUnknownSignals: true
        function onVolumeChanged(volume) {
            root.showHud("Volume " + Math.round(Math.min(130, Math.max(0, volume))) + "%")
        }
        function onMuteChanged(muted) {
            root.showHud(muted ? "Muted" : "Unmuted")
        }
        function onSpeedChanged(speed) {
            root.showHud("Speed " + (Math.abs(speed - 1.0) < 0.005 ? "1" : speed.toFixed(2)) + "x")
        }
        function onSubtitleDelayChanged(delay) {
            if (Math.abs(delay) < 0.05)
                root.showHud("Subtitles in sync")
            else
                root.showHud("Sub delay " + (delay < 0 ? "−" : "+") + Math.abs(delay).toFixed(1) + "s")
        }
        function onAudioDelayChanged() {
            const d = Player.audioDelay
            if (Player.currentFile === "" || Player.state === 1) return
            root.showHud(d === 0 ? "Audio delay: 0 ms"
                                 : "Audio delay: " + (d > 0 ? "+" : "") + Math.round(d * 1000) + " ms")
        }
    }

    Connections {
        target: Player
        function onPlaybackActionRequested(paused) {
            if (Player.currentFile === "" || Player.state === 1) return
            playbackFeedback.paused = paused
            playbackFeedback.shown = true
            playbackFeedbackTimer.restart()
        }
        function onTrackSelectionFeedback(type, label) {
            root.trackFeedbackText = (type === "audio" ? "Audio: " : "Subtitles: ") + label
            trackFeedbackTimer.restart()
        }
        function onScreenshotSaved(path) { screenshotToast.showSaved(path) }
        function onScreenshotFailed(message) { screenshotToast.showFailed(message) }
        function onScreenshotCopied(path, copied) {
            if (path === "")
                root.showHud(copied ? "Frame copied to clipboard" : "Could not copy this frame")
            else
                screenshotToast.markCopied(path, copied)
        }
        function onErrorOccurred(message) {
            if (Player.playbackError !== "") return
            root.errorMessage = message
            errorTimer.restart()
        }
        function onPlaybackErrorChanged() {
            root.errorMessage = ""
            errorTimer.stop()
            if (Player.playbackError !== "") {
                root.trackFeedbackText = ""
                trackFeedbackTimer.stop()
            }
        }
        function onFileLoaded(path) {
            root.trackFeedbackText = ""
            trackFeedbackTimer.stop()
            root.errorMessage = ""
            errorTimer.stop()
            // End the shuttle hold, reflect the saved aspect applied by the
            // loading hook, and re-arm the window fit.
            root.endShuttleHold()
            Player.setSpeed(1.0)
            root.aspectIdx = Preferences.defaultAspectIndex
            root.perFileFitted = false
            Qt.callLater(root.fitWindowToVideo)
            // F6d: a zoom/pan belongs to the video it was framed on.
            root.resetZoom()
            root.refreshBookmarks()
            // F6a: mpv keeps ab-loop points across a file change, which
            // would silently loop a region of the NEXT video.
            Player.clearAbLoop()
        }
        function onVideoSizeChanged() {
            if (root.theaterLayout && !root.theaterTransitioning)
                root.updateTheaterWindow()
            // A complete display-size pair for the fresh file landed: fit
            // the video once per file. Audio-only reports 0x0 and is
            // skipped inside fitWindowToVideo().
            root.fitWindowToVideo()
        }
        function onPauseChanged(paused) {
            if (!paused)
                return
            // An external pause (SPACE, media key, click) ends a shuttle
            // burst; the stepped speed is kept.
            root.endShuttleHold()
        }
        function onScrubbingChanged(scrubbing) {
            if (scrubbing)
                return
            // Releasing a scrub ends a shuttle burst; speed is kept.
            root.endShuttleHold()
        }
        function onAppCommand(name, args) {
            if (name === "app-open-file") {
                root.openMediaDialog()
            } else if (name === "app-load-subtitles") {
                root.openSubtitleDialog()
            } else if (name === "app-toggle-fullscreen") {
                root.toggleFullscreen()
            } else if (name === "app-escape") {
                // While a popup owns the keyboard, Escape still reaches mpv
                // (ShortcutManager forwards it) and closes the popup in QML
                // first. This command then arrives after the popup is gone;
                // without this guard it peeled a second layer too, so one
                // Escape on the right-click menu also closed the theater
                // playlist.
                if (Date.now() - root.lastPopupCloseMs < 400)
                    return
                // ESC peels one layer at a time: dialog, track picker, then
                // drawer, then fullscreen. Never two at once. The dialogs
                // also handle ESC themselves while their text field has
                // focus (key forwarding is suppressed then, A19); this
                // covers the case where focus sits anywhere else in them.
                if (aboutBox.visible) {
                    aboutBox.visible = false
                } else if (playbackInfo.active) {
                    playbackInfo.closeDialog()
                } else if (shortcutGuide.active)
                    shortcutGuide.closeGuide()
                else if (settingsDialog.active)
                    settingsDialog.closeDialog()
                else if (openUrlDialog.active)
                    openUrlDialog.closeDialog()
                else if (goToDialog.active)
                    goToDialog.closeDialog()
                else if (osd.hasOpenPopup())
                    osd.closePopups()
                else if (playlistDrawer.shown)
                    playlistDrawer.shown = false
                else if (root.isFullscreen)
                    root.toggleFullscreen()
                else if (root.miniPlayer)
                    root.leaveMiniPlayer()
            } else if (name === "app-quit") {
                Qt.quit()
            } else if (name === "app-toggle-playlist") {
                root.togglePlaylist()
            } else if (name === "app-playback-info") {
                // The panel no longer owns the keyboard (D4), so its own key
                // reaches here while it is open and closes it again.
                if (playbackInfo.active) {
                    playbackInfo.closeDialog()
                } else {
                    osd.closePopups()
                    playbackInfo.openDialog()
                }
            } else if (name === "app-chapters") {
                // Say so instead of doing nothing (D5), like the other toggles.
                if (!Player.chapterList.length)
                    root.showHud(Player.currentFile !== "" ? "No chapters in this file" : "Nothing playing")
                else
                    osd.openMenu("chapters")
            } else if (name === "app-always-on-top") {
                Native.toggleAlwaysOnTop()
                root.showHud(Native.alwaysOnTop ? "Always on top: on" : "Always on top: off")
            } else if (name === "app-mini-player") {
                if (root.miniPlayer)
                    root.leaveMiniPlayer()
                else
                    root.enterMiniPlayer("")
            } else if (name === "app-pin-corner") {
                root.enterMiniPlayer(args && args.length > 0 ? String(args[0]) : "")
            } else if (name === "app-window-scale") {
                root.setWindowScale(Number(args && args.length > 0 ? args[0] : 1))
            } else if (name === "app-toggle-theater") {
                if (root.theaterMode)
                    root.leaveTheater()
                else
                    root.enterTheater()
            } else if (name === "app-shader-dialog") {
                root.openShaderDialog()
            } else if (name === "app-shuttle-down") {
                // F36: J trims the speed down in 0.25x steps (0.25x floor).
                root.endShuttleHold()
                root.stepShuttle(-0.25)
            } else if (name === "app-shuttle-up") {
                // Menu and remapped-key path; plain L is handled by the
                // ShortcutManager tap/hold filter (onShuttleStepUp).
                root.endShuttleHold()
                root.stepShuttle(0.25)
            } else if (name === "app-toggle-pause") {
                // F36: K toggles pause; the stepped speed is kept.
                root.endShuttleHold()
                Player.togglePause()
            } else if (name === "app-frame-step-forward") {
                // Stepping implies pausing first, or the frame would play on.
                if (!Player.isPaused)
                    Player.setPaused(true)
                Player.command(["frame-step"])
            } else if (name === "app-frame-step-back") {
                if (!Player.isPaused)
                    Player.setPaused(true)
                Player.command(["frame-back-step"])
            } else if (name === "app-loop-file") {
                root.loopFile = !root.loopFile
                Player.command(["set", "loop-file", root.loopFile ? "inf" : "no"])
                root.showHud(root.loopFile ? "Loop video: on" : "Loop video: off")
            } else if (name === "app-cycle-aspect") {
                root.setAspect(root.aspectIdx + 1)
            } else if (name === "app-set-aspect") {
                root.setAspect(Number(args && args.length > 0 ? args[0] : 0))
            } else if (name === "app-toggle-playlist-repeat") {
                root.setPlaylistRepeat(!root.playlistRepeat)
            } else if (name === "app-shortcut-guide") {
                if (shortcutGuide.active) shortcutGuide.closeGuide()
                else shortcutGuide.openGuide()
            } else if (name === "app-ab-in") {
                // F6a: A and B are set independently rather than through
                // mpv's single cycling "ab-loop" command, so b/Shift+B/Alt+B
                // each do exactly one predictable thing. mpv swaps the pair
                // itself if A lands after B.
                Player.setAbLoopA(Player.position)
                root.showHud("A-B loop: A at " + KeyNames.clock(Player.position))
            } else if (name === "app-ab-out") {
                Player.setAbLoopB(Player.position)
                root.showHud("A-B loop: B at " + KeyNames.clock(Player.position))
            } else if (name === "app-ab-clear") {
                Player.clearAbLoop()
                root.showHud("A-B loop cleared")
            } else if (name === "app-cycle-time-display") {
                root.timeDisplayMode = (root.timeDisplayMode + 1) % 3
                root.showHud("Time: " + root.timeDisplayName())
            } else if (name === "app-zoom-in") {
                root.setZoom(root.videoZoom + root.zoomStep)
            } else if (name === "app-zoom-out") {
                root.setZoom(root.videoZoom - root.zoomStep)
            } else if (name === "app-zoom-reset") {
                root.resetZoom()
                root.showHud("Zoom reset")
            } else if (name === "app-open-clipboard") {
                // F45: Ctrl+V opens whatever the clipboard holds — a stream
                // URL or a copied media file. resolveOpenTarget does the
                // classifying so a stray text copy reports cleanly instead of
                // making mpv fail on nonsense. openPaths keeps a local file
                // on the same F16 folder-enqueue path as every other open;
                // a URL has no siblings, so it falls through to openFile.
                const target = Player.resolveOpenTarget(Native.clipboardText())
                if (target === "") {
                    root.showHud("Clipboard has no media link or file")
                } else {
                    root.openPaths([target])
                    root.showHud(target.indexOf("://") > 0 ? "Opening stream…"
                                                           : "Opening from clipboard")
                }
            } else if (name === "app-open-url") {
                if (playbackInfo.active) playbackInfo.closeDialog()
                openUrlDialog.openDialog()
            } else if (name === "app-screenshot-clipboard") {
                // F9: mpv has no clipboard screenshot command, so this is the
                // app-side counterpart of "s" (which writes a file).
                if (Player.currentFile === "")
                    root.showHud("Nothing playing")
                else if (!Player.screenshotToClipboard())
                    root.showHud("Could not copy this frame")
            } else if (name === "app-goto-time") {
                if (Player.currentFile !== "") {
                    // One centred panel at a time: the info panel would
                    // otherwise sit on top of the go-to field.
                    if (playbackInfo.active) playbackInfo.closeDialog()
                    goToDialog.openDialog()
                }
            } else if (name === "app-bookmark-add") {
                root.toggleBookmarkHere()
            } else if (name === "app-open-recent") {
                // F33: recent-files entries share the folder auto-enqueue.
                if (args.length > 0 && args[0] !== "") {
                    root.openWithFolderEnqueue(args[0])
                    Settings.addRecent(args[0])
                }
            } else if (name === "app-settings") {
                if (playbackInfo.active) playbackInfo.closeDialog()
                settingsDialog.openDialog()
            } else if (name === "app-audio-menu") {
                osd.openMenu("audio")
            } else if (name === "app-subtitle-menu") {
                osd.openMenu("sub")
            } else if (name === "app-speed-menu") {
                osd.openMenu("speed")
            } else if (name === "app-playback-menu") {
                osd.openMenu("settings")
            } else if (name === "app-check-updates") {
                aboutBox.visible = true
                ReleaseChecker.check()
            } else if (name === "app-about") {
                aboutBox.visible = true
            } else if (name === "app-subtitle-cycle") {
                // C / CC (F24): a single C cycles to the next subtitle track;
                // a second C inside the debounce window toggles visibility
                // (Subtitle On/Off) instead. mpv forwards every C press as
                // this command; the timer decides which meaning it carries.
                if (subtitleCycleTimer.running) {
                    subtitleCycleTimer.stop()
                    Player.command(["cycle", "sub-visibility"])
                } else {
                    subtitleCycleTimer.restart()
                }
            }
        }
    }

    // Fires only when a single C was pressed (no CC follow-up).
    Timer {
        id: subtitleCycleTimer
        interval: 350
        onTriggered: {
            Player.command(["cycle", "sub"])
        }
    }

    // K held + L tapped (Premiere-style slow shuttle, intercepted in
    // ShortcutManager A12): halves the speed down to 0.125x; from pause it
    // starts at 0.5x. K alone still pauses (app-shuttle-stop).
    Connections {
        target: Shortcuts
        ignoreUnknownSignals: true
        // F36: a quick L tap steps the speed up 0.25x (J steps down via its
        // app command).
        function onShuttleStepUp() {
            root.endShuttleHold()
            root.stepShuttle(0.25)
        }
        // F30/F36 hold-shuttle: taps 1/2/3 → hold targets 2.5x / 5x / 8x,
        // ramped up in 0.25x steps from the current speed; release reverts
        // to the pre-hold speed (onShuttleHoldEnd).
        function onShuttleHoldStart(taps) {
            root.holdTarget = taps >= 3 ? 8.0 : (taps === 2 ? 5.0 : 2.5)
            root.holdWasPaused = Player.isPaused
            root.holdPreSpeed = Player.speed
            root.holdActive = true
            if (root.holdWasPaused)
                Player.setPaused(false)
            holdRampTimer.restart()
        }
        function onShuttleHoldEnd() {
            root.endShuttleHold()
        }
        // F27: a non-navigation shortcut pressed while a track menu (or the
        // context menu) is open dismisses the pickers first; the filter then
        // forwards the key, so e.g. F or C work with a menu expanded. Menu
        // navigation keys (arrows/Enter/Tab) never reach this.
        function onDismissMenusRequested() {
            osd.closePopups()
            contextMenu.close()
        }
    }

    // Saved geometry (SettingsStore.windowGeometry) if at least a title-bar
    // sized corner of it lies on one of the current screens; null otherwise.
    function savedWindowRect() {
        const geo = Settings.windowGeometry()
        if (!geo || geo.width === undefined || geo.height === undefined
                || geo.x === undefined || geo.y === undefined)
            return null
        const w = Math.max(root.minimumWidth, Math.round(Number(geo.width)))
        const h = Math.max(root.minimumHeight, Math.round(Number(geo.height)))
        const x = Math.round(Number(geo.x)), y = Math.round(Number(geo.y))
        if (!isFinite(w) || !isFinite(h) || !isFinite(x) || !isFinite(y))
            return null
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; ++i) {
            const s = screens[i]
            const overlapW = Math.min(x + w, s.virtualX + s.width) - Math.max(x, s.virtualX)
            const overlapH = Math.min(y + h, s.virtualY + s.height) - Math.max(y, s.virtualY)
            if (overlapW >= 200 && overlapH >= 120)
                return { x: x, y: y, width: Math.min(w, s.width), height: Math.min(h, s.height) }
        }
        return null
    }

    Component.onCompleted: {
        // Hold the first frame before startup opens media, not after the
        // guide's entrance delay. A deliberate Play/Pause overrides the hold.
        if (root.firstRunGuidePending)
            Player.setInteractionPause(true)
        // Interface pane accent-colour override, read once at startup;
        // SettingsDialog reassigns Colors.accent live on change.
        const savedAccent = Settings.value("interface/accentColor", "")
        Colors.accent = Colors.restoredAccent(savedAccent)
        if (savedAccent !== "" && String(Colors.accent).toUpperCase() !== String(savedAccent).toUpperCase())
            Settings.setValue("interface/accentColor", String(Colors.accent).toUpperCase())

        // F32/F37: an empty player opens at ~60% of the MAIN monitor
        // (16:9, centered on it) — not whichever screen the window manager
        // happened to create the window on. The rectangle is still saved on
        // quit (aboutToQuit) for future resume-on-launch work; opening a
        // video then auto-fits the window to it (F2).
        // Show the window without opening another decoder to probe the file.
        // The playback decoder supplies display dimensions before its first
        // frame is presented; fitWindowToVideo then applies the video aspect.
        const pg = Native.primaryScreenGeometry()
        let w = Math.round(pg.width * 0.6)
        let h = Math.round(w * 9 / 16)
        const maxH = Math.round(pg.height * 0.6)
        if (h > maxH) {
            h = maxH
            w = Math.round(h * 16 / 9)
        }
        let x = Math.round(pg.x + (pg.width - w) / 2)
        let y = Math.round(pg.y + (pg.height - h) / 2)
        // The rectangle saved on the last quit wins when enough of it is
        // still on a connected screen (a monitor that has since been
        // unplugged must not leave the window off-screen). A video opened
        // afterwards still auto-fits (F2).
        const saved = root.savedWindowRect()
        if (saved) {
            w = saved.width; h = saved.height; x = saved.x; y = saved.y
        }
        root.width = w
        root.height = h
        root.x = x
        root.y = y
        osd.showControls()
        // F40: the CLI startupFile is NOT opened here — at this point the
        // window is not even shown and the deferred mpv surface does not
        // exist, so playback would start with audio only (dropped video).
        // MpvItem.onRenderContextReady opens it once the surface can render.
    }
}

