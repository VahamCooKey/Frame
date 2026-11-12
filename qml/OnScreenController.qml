import QtQuick

FocusScope {
    id: osd
    objectName: "playbackControls"

    // Current track as one short token for the right-click menu's inline
    // values (main.qml valueHints): the language when known ("English"),
    // else the codec ("AAC"). The full label stays in the pickers.
    function shortTrackLabel(t) {
        return languageName(t.lang) || codecName(t.codec) || ("Track " + t.id)
    }
    readonly property string currentAudioLabel: {
        const list = tracksOf("audio")
        for (let i = 0; i < list.length; ++i)
            if (list[i].selected) return shortTrackLabel(list[i])
        return ""
    }
    readonly property string currentSubtitleLabel: {
        const list = tracksOf("sub")
        for (let i = 0; i < list.length; ++i)
            if (list[i].selected) return shortTrackLabel(list[i])
        return "Off"
    }

    component ControlButton: IconButton {
        hoverColor: osd.cHover
        width: osd.compact ? 34 : 42
        height: 42
        iconSize: 24
        tooltipEnabled: true
        idleIconOpacity: 1.0
        activeUnderline: false
        onInteraction: if (hovered) osd.showControls()
        // Active state (playlist shown, a popover open): a 32 px plate
        // behind the icon; the band turns IconButton's underline off.
        Rectangle {
            z: -1
            anchors.centerIn: parent
            width: 32; height: 32; radius: 6
            color: Colors.hover
            visible: parent.active
        }
    }

    signal fullscreenRequested()
    signal playlistRequested()
    signal settingsRequested()
    signal hidden()
    signal openSubtitleRequested()

    readonly property bool compact: width < 700
    readonly property real sideMargin: compact ? 16 : 28
    // Theater gives the controls their own 72 px band below the picture
    // (24 timeline + 42 buttons + 6), drawn on the window colour instead of
    // a scrim over the video.
    readonly property bool band: height <= 80
    // Height of the letterbox bar under the picture (main.qml). When it is
    // tall enough, the button row sits centred in it instead of straddling
    // the picture's bottom edge.
    property real letterbox: 0
    property bool menuOpen: false
    property bool shown: false
    property bool playlistInteracting: false
    property bool pinned: false
    property bool fullscreenActive: false
    // F29: the playlist toggle button tints with the accent while the
    // drawer is shown; main.qml binds it to playlistDrawer.shown.
    property bool playlistActive: false
    property real scrubPos: 0
    readonly property real displayedPosition: Player.scrubbing ? scrubPos
        : Player.seekPending ? Player.seekTarget : Player.position
    property int previewEpoch: 0
    property bool previewActive: false
    property bool previewBusy: false
    property real previewMs: 0.0
    property real previewLastSentMs: 0
    property real previewLastSentSec: -1
    property string trackMenuType: ""
    // Quick-settings state owned by main.qml.
    property bool loopFile: false
    property bool playlistRepeat: false
    property int aspectIndex: 0
    // PlayerController::State ordinals (QML cannot reach Q_ENUMs through a
    // context-property instance, so keep the numeric contract here).
    readonly property int stateLoading: 1
    readonly property int stateBuffering: 2
    readonly property int statePlaying: 3

    // The Colors singleton can be briefly unresolved during engine startup;
    // keep local fallbacks so first-frame bindings never assign undefined.
    readonly property bool hasColors: typeof Colors !== "undefined" && Colors !== null
    readonly property color cBar: hasColors ? Colors.surface : "#F5181818"
    readonly property color cText: hasColors ? Colors.text : "#F3F3F3"
    readonly property color cTextDim: hasColors ? Colors.textDim : "#A3A3A3"
    readonly property color cAccent: hasColors ? Colors.accent : "#E0E0E0"
    readonly property color cHover: hasColors ? Colors.hover : "#22FFFFFF"

    enabled: shown
    visible: false
    opacity: shown ? 1 : 0

    // Show controls promptly; let them fade gently back into the video.
    readonly property int animMs: Native.animationsEnabled() ? (shown ? 140 : 180) : 0

    // When chrome auto-hides under a stationary mouse, Qt synthesizes a
    // hover event for the exposed video area; without this cooldown that
    // event re-shows the chrome and it flickers back on in a loop.
    property real lastAutoHide: 0

    Behavior on opacity { NumberAnimation { duration: animMs; easing.type: Easing.OutQuad } }
    onOpacityChanged: if (opacity <= 0.01 && !shown) visible = false

    function showControls() {
        if (Date.now() - lastAutoHide < 400)
            return
        showControlsNow()
    }

    function showControlsNow() {
        visible = true
        shown = true
        hideTimer.restart()
    }

    function hideNow() {
        hideTimer.stop()
        // F39: a track menu never outlives the chrome — when the bar goes
        // (auto-hide, theater leave), the picker collapses with it, marked
        // auto-closed so the close handler does not re-arm the chrome.
        if (trackMenuLoader.item && trackMenuLoader.item.visible) {
            trackMenuLoader.item.autoClosed = true
            trackMenuLoader.item.close()
        }
        if (chapterMenuLoader.item) chapterMenuLoader.item.close()
        shown = false
        lastAutoHide = Date.now()
        hidden()
    }

    function formatTime(t) { return KeyNames.clock(t) }

    // F7b/F7d helpers over mpv's {title, time} chapter-list entries (sorted).
    // chapterAt returns the chapter covering sec — the last one starting at
    // or before it — as {title, index}, or null when there is none.
    function chapterAt(sec) {
        var found = null
        var foundIdx = -1
        var list = Player.chapterList
        for (var i = 0; i < list.length; i++) {
            var t = list[i].time !== undefined ? list[i].time : 0
            if (t <= sec + 0.001) {
                found = list[i]
                foundIdx = i
            } else
                break
        }
        if (!found)
            return null
        return { title: found.title !== undefined ? found.title : "", index: foundIdx }
    }

    // F7d: Ctrl-drag is magnetic — once the cursor comes within ~12 px of a
    // chapter start, the scrub time sticks to it (Premiere-style); without
    // Ctrl the raw cursor time applies.
    function snapToChapter(rawSec, trackWidth) {
        if (trackWidth <= 0 || Player.duration <= 0)
            return rawSec
        const tolSec = 12 / trackWidth * Player.duration
        var best = rawSec
        var bestDist = tolSec
        var list = Player.chapterList
        for (var i = 0; i < list.length; i++) {
            var t = list[i].time !== undefined ? list[i].time : 0
            const d = Math.abs(t - rawSec)
            if (d < bestDist) {
                bestDist = d
                best = t
            }
        }
        return best
    }

    // Shared press/drag time computation for the timeline.
    function scrubTimeFor(mouse) {
        var raw = Math.max(0, Math.min(Player.duration, mouse.x / timeline.width * Player.duration))
        if (mouse.modifiers & Qt.ControlModifier)
            raw = snapToChapter(raw, timeline.width)
        return raw
    }

    // F6c. Only the playhead label follows this; the duration label and the
    // scrub tooltip stay plain elapsed, so the total never reads "-0:00".
    property int timeDisplayMode: 0

    // F6e bookmark positions in seconds (main.qml owns the list).
    property var bookmarks: []

    function pad2(n) { return n < 10 ? "0" + n : "" + n }

    // container-fps "can easily contain bogus values" (mpv manual), hence
    // the container-fps -> estimated-vf-fps -> 24 chain.
    function timecodeFps() {
        if (Player.containerFps > 0.1 && Player.containerFps < 1000)
            return Player.containerFps
        if (Player.estimatedVfFps > 0.1 && Player.estimatedVfFps < 1000)
            return Player.estimatedVfFps
        return 24
    }

    function formatTimecode(t) {
        const fps = osd.timecodeFps()
        const safe = Math.max(0, t)
        var frames = Math.floor((safe - Math.floor(safe)) * fps)
        if (frames >= fps)
            frames = Math.floor(fps) - 1
        var total = Math.floor(safe)
        return osd.pad2(Math.floor(total / 3600)) + ":"
             + osd.pad2(Math.floor(total / 60) % 60) + ":"
             + osd.pad2(total % 60) + ":" + osd.pad2(frames)
    }

    function formatPosition(t) {
        if (osd.timeDisplayMode === 1 && Player.duration > 0)
            return "-" + osd.formatTime(Player.duration - t)
        if (osd.timeDisplayMode === 2)
            return osd.formatTimecode(t)
        return osd.formatTime(t)
    }

    // Track names read "English · SRT" rather than mpv's raw
    // "Track 1 - Plain SRT [eng]" (D16). Without a language or title the
    // label falls back to the track's actual mpv ID ("Track 3"), and a
    // duplicate label gets that ID too, so every row stays distinct.
    function languageName(code) { return KeyNames.languageName(code) }
    function codecName(codec) { return KeyNames.codecName(codec) }
    function channelName(count) {
        return count === 1 ? "Mono" : count === 2 ? "Stereo" : count === 6 ? "5.1"
             : count === 8 ? "7.1" : count > 0 ? count + " ch" : ""
    }
    function baseTrackLabel(t) {
        const parts = []
        const language = languageName(t.lang)
        const title = String(t.title || "").trim()
        const codec = codecName(t.codec)
        if (language) parts.push(language)
        // A title that only repeats the language or codec adds nothing.
        if (title && title.toLowerCase() !== language.toLowerCase()) parts.push(title)
        if (codec && title.toLowerCase().indexOf(codec.toLowerCase()) < 0) parts.push(codec)
        if (t.type === "audio") {
            const count = Number(t["demux-channel-count"] || t["audio-channels"] || 0)
            const layout = String(t["demux-channels"] || "")
            const channels = count > 0 ? channelName(count)
                           : layout ? layout.charAt(0).toUpperCase() + layout.slice(1) : ""
            if (channels) parts.push(channels)
        }
        if (t.forced) parts.push("Forced")
        if (t.external) parts.push("External")
        return parts.length && (language || title) ? parts.join(" · ") : ""
    }
    function trackLabel(t, n) {
        if (t.label)
            return t.label
        if (t.offEntry)
            return "Off"
        const id = t.id !== undefined ? t.id : n
        const base = baseTrackLabel(t)
        if (!base)
            return "Track " + id + (t.codec ? " · " + codecName(t.codec) : "")
        let twins = 0
        const list = Player.trackList
        for (let i = 0; i < list.length; ++i)
            if (list[i].type === t.type && baseTrackLabel(list[i]) === base) ++twins
        return twins > 1 ? base + " · Track " + id : base
    }

    function tracksOf(type) {
        var out = []
        for (var i = 0; i < Player.trackList.length; i++)
            if (Player.trackList[i].type === type)
                out.push(Player.trackList[i])
        return out
    }

    function openTrackMenu(type, noneLabel, setter) {
        if (chapterMenuLoader.item) chapterMenuLoader.item.close()
        if (trackMenuLoader.item && trackMenuLoader.item.visible && trackMenuType === type) {
            trackMenuLoader.item.close()
            return
        }
        trackMenuLoader.preparing = false
        trackMenuType = type
        var entries = tracksOf(type)
        // Subtitles can also be turned off entirely; mpv models that as
        // sid=no, which is not a track-list entry.
        if (type === "sub")
            entries.unshift({ id: "off", offEntry: true, title: "Off",
                              selected: !entries.some(t => t.selected) })
        trackMenuLoader.pendingEntries = entries
        trackMenuLoader.pendingEmptyLabel = noneLabel
        trackMenuLoader.pendingSetter = setter
        trackMenuLoader.active = true
        if (trackMenuLoader.item) {
            trackMenuLoader.item.configure(trackMenuLoader.pendingEntries,
                                           trackMenuLoader.pendingEmptyLabel,
                                           trackMenuLoader.pendingSetter,
                                           osd.trackLabel,
                                           osd.trackMenuType)
        }
        showControlsNow()
    }

    // ESC peeling support: main.qml closes an open track picker before it
    // touches the drawer or fullscreen state.
    function hasOpenPopup() {
        return (trackMenuLoader.item && trackMenuLoader.item.visible)
            || (chapterMenuLoader.item && chapterMenuLoader.item.visible)
    }

    function closePopups() {
        if (trackMenuLoader.item) trackMenuLoader.item.close()
        if (chapterMenuLoader.item) chapterMenuLoader.item.close()
    }

    function setVolumeFromX(x, w) {
        Player.setVolume(Math.max(0, Math.min(130, x / w * 130)))
    }

    // Send one immediate frame, then always send a trailing request for the
    // final hover position. Without the trailing request a quick mouse stop
    // leaves the preview showing the last frame from the previous cooldown.
    function cancelPreview() {
        previewRequestTimer.stop()
        previewEpoch++
        previewLastSentMs = 0
        previewLastSentSec = -1
        previewThumb.latestOk = false
        previewThumb.source = ""
        // A completed preview can remain visible while the shared worker
        // starts a poster. Only supersede a still-pending preview capture.
        if (previewBusy) Thumbs.clear()
        previewActive = false
        previewBusy = false
    }

    function requestPreview(sec) {
        if (Player.currentFile === "")
            return
        previewActive = true
        previewMs = sec
        if (Date.now() - previewLastSentMs >= 180)
            sendPreview()
        previewRequestTimer.restart()
    }

    function sendPreview() {
        if (!previewActive || Player.currentFile === "")
            return
        previewLastSentMs = Date.now()
        previewLastSentSec = previewMs
        previewBusy = true
        Thumbs.requestThumbnail(Player.currentFile, previewMs, 240)
    }

    Timer {
        id: previewRequestTimer
        interval: 120
        repeat: false
        onTriggered: {
            if (Math.abs(osd.previewLastSentSec - osd.previewMs) >= 0.05)
                osd.sendPreview()
        }
    }

    Connections {
        target: Settings
        function onSettingChanged(key, value) {
            if (key === "interface/osdHideMs") hideTimer.interval = Number(value)
        }
    }

    Timer {
        id: hideTimer
        // Sole idle deadline for the playback bar and playlist shelf.
        interval: Settings.value("interface/osdHideMs", 1400)
        onTriggered: {
            if (controlHover.hovered || osd.activeFocus || Player.scrubbing || osd.playlistInteracting) {
                restart()
                return
            }
            if (!pinned && Player.state === osd.statePlaying && !osd.menuOpen && !osd.hasOpenPopup())
                osd.hideNow()
        }
    }

    // Track-menu close bookkeeping (F25/F27): a manual select keeps the
    // chrome up with a fresh clock; an idle auto-dismiss takes the chrome
    // with it (same instant), so the user never sees a lone menu floating.
    // autoClosed is consumed here because TrackMenu's own onVisibleChanged
    // runs before this Connections handler and must not clear the flag.
    Connections {
        target: trackMenuLoader.item
        function onAutoDismissed() {
            if (!osd.pinned && Player.state === osd.statePlaying && !osd.menuOpen)
                osd.hideNow()
        }
        function onVisibleChanged() {
            if (!target || target.visible)
                return
            if (target.autoClosed) {
                target.autoClosed = false
            } else {
                osd.showControlsNow()
            }
        }
        function onAddSubtitleRequested() {
            osd.openSubtitleRequested()
        }
    }

    Connections {
        target: Player
        function onStateChanged() { osd.showControlsNow() }
        function onFileLoaded(path) {
            osd.showControlsNow()
            osd.previewEpoch++
            osd.previewActive = false
            osd.previewBusy = false
            osd.previewLastSentMs = 0
            osd.previewLastSentSec = -1
            previewRequestTimer.stop()
            previewThumb.latestOk = false
            previewThumb.source = ""
            osd.closePopups()
            Thumbs.clear()
        }
    }

    Connections {
        target: Thumbs
        function onPreviewInvalidated() {
            osd.previewEpoch++
            previewThumb.latestOk = false
            previewThumb.source = ""
            osd.previewBusy = false
        }
        function onThumbnailReady(file, seconds, image, success) {
            if (osd.previewActive && file === Player.currentFile && Math.abs(seconds - osd.previewMs) < 0.25) {
                const epoch = osd.previewEpoch
                previewBusy = false
                if (!success) {
                    previewThumb.latestOk = false
                    previewThumb.source = ""
                    return
                }
                // QML's signal hook can run before the C++ provider's slot.
                // Fetch on the next turn, once the provider owns the image.
                Qt.callLater(function() {
                    if (!osd.previewActive || epoch !== osd.previewEpoch || file !== Player.currentFile || Math.abs(seconds - osd.previewMs) >= 0.25) return
                    previewThumb.token++
                    previewThumb.source = "image://preview/" + previewThumb.token
                    previewThumb.latestOk = true
                })
            }
        }
    }

    function preloadMenus() { trackMenuLoader.preparing = true; trackMenuLoader.active = true }

    function showMenu(type, entries, setter, canGoBack) {
        if (chapterMenuLoader.item) chapterMenuLoader.item.close()
        trackMenuLoader.preparing = false
        trackMenuType = type
        trackMenuLoader.pendingEntries = entries
        trackMenuLoader.pendingEmptyLabel = ""
        trackMenuLoader.pendingSetter = setter
        trackMenuLoader.pendingCanGoBack = canGoBack === true
        trackMenuLoader.active = true
        if (trackMenuLoader.item)
            trackMenuLoader.item.configure(entries, "", setter, osd.trackLabel, type, canGoBack === true)
        showControlsNow()
    }

    readonly property var aspectLabels: ["Original", "16:9", "4:3", "2.35:1"]
    function speedLabel(rate) {
        const r = Math.round(rate * 100) / 100
        return r === 1 ? "Normal" : r + "\u00d7"
    }

    // Quick settings: the things people change mid-video. Audio and
    // subtitles have their own buttons on the bar, so they are not repeated.
    function openSettingsMenu() {
        if (hasOpenPopup() && trackMenuType === "settings") {
            closePopups()
            return
        }
        const entries = [
            { id: "speed", label: "Playback speed", detail: speedLabel(Player.speed),
              command: "script-message app-speed-menu", submenu: true, icon: "gauge" },
            { id: "aspect", label: "Aspect ratio", detail: aspectLabels[osd.aspectIndex] || "",
              command: "script-message app-cycle-aspect", submenu: true, icon: "aspect" },
            { id: "loop", label: "Loop video", command: "script-message app-loop-file", toggle: true, icon: "loop", group: "toggles" },
            { id: "repeat", label: "Repeat playlist", toggle: true, icon: "repeat", group: "toggles" },
            { id: "top", label: "Always on top", command: "script-message app-always-on-top", toggle: true, icon: "pin", group: "toggles" },
            // Commands that open another surface sit below a divider.
            { id: "info", label: "Playback info", command: "script-message app-playback-info", opens: true, icon: "info", group: "open" },
            { id: "all", label: "All settings", command: "script-message app-settings", opens: true, icon: "settings", group: "open" }
        ]
        if (Player.chapterList.length)
            entries.splice(2, 0, { id: "chapters", label: "Chapters", command: "script-message app-chapters", submenu: true, icon: "bookmark" })
        showMenu("settings", entries, function(id) {
            if (id === "speed") {
                osd.openSpeedMenu(true)
            } else if (id === "aspect") {
                osd.openAspectMenu(true)
            } else if (id === "chapters") {
                osd.openChapters()
            } else if (id === "loop") {
                Player.command(["script-message", "app-loop-file"])
            } else if (id === "repeat") {
                Player.command(["script-message", "app-toggle-playlist-repeat"])
            } else if (id === "top") {
                Player.command(["script-message", "app-always-on-top"])
            } else if (id === "info") {
                Player.command(["script-message", "app-playback-info"])
            } else {
                osd.settingsRequested()
            }
        })
    }
    function openAspectMenu(fromSettings) {
        showMenu("aspect", aspectLabels.map((label, i) => ({
            id: i, label: label, selected: i === osd.aspectIndex
        })), i => Player.command(["script-message", "app-set-aspect", String(i)]), fromSettings)
    }

    function openAudioMenu() {
        openTrackMenu("audio", "No audio tracks", tid => Player.setAudioTrack(tid))
    }
    function openSubtitleMenu() {
        openTrackMenu("sub", "No subtitles", tid => {
            if (tid === "off") Player.command(["set", "sid", "no"])
            else Player.setSubtitleTrack(tid)
        })
    }
    function openSpeedMenu(fromSettings) {
        const rates = [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]
        showMenu("speed", rates.map(rate => ({
            id: rate, label: speedLabel(rate),
            command: rate === 1 ? "set speed 1.0" : "",
            selected: Math.abs(Player.speed - rate) < 0.01
        })), rate => Player.setSpeed(rate), fromSettings)
    }

    function openMenu(type) {
        if (type === "audio") openAudioMenu()
        else if (type === "sub") openSubtitleMenu()
        else if (type === "speed") openSpeedMenu()
        else if (type === "chapters") openChapters()
        else openSettingsMenu()
    }

    function openChapters() {
        if (!Player.chapterList.length) return
        closePopups()
        trackMenuType = "chapters"
        chapterMenuLoader.active = true
        if (chapterMenuLoader.item) chapterMenuLoader.item.open()
        showControlsNow()
    }

    // The scrim reaches full black at the window bottom, so the bottom
    // letterbox and the scrim meet without a visible seam.
    // Theater band: the panel colour shared with the caption and the dock.
    Rectangle {
        anchors.fill: parent
        visible: osd.band
        color: Colors.panel
    }
    Rectangle {
        anchors.fill: parent
        visible: !osd.band
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 0.4; color: "#8C000000" }
            GradientStop { position: 0.75; color: "#E6000000" }
            GradientStop { position: 1.0; color: "#FF000000" }
        }
    }

    HoverHandler { id: controlHover }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: timeline.top
        anchors.bottomMargin: 8
        visible: Player.state === osd.stateLoading || Player.state === osd.stateBuffering || Player.eofReached
        text: Player.eofReached ? "Ended \u00b7 Play to restart"
              : Player.state === osd.stateBuffering ? "Buffering " + Player.cacheBuffering + "%" : "Loading…"
        color: osd.cTextDim
        font.pixelSize: 12
        font.family: Fonts.ui
    }

    Item {
        id: timeline
        objectName: "playerTimeline"
        anchors.left: parent.left
        anchors.right: parent.right
        // Band: the track starts and ends on the outer glyphs' ink (the
        // first glyph's ink begins 7 px inside its 42 px button).
        anchors.leftMargin: osd.band ? osd.sideMargin + 6 : osd.sideMargin
        anchors.rightMargin: osd.band ? osd.sideMargin + 6 : osd.sideMargin
        anchors.bottom: bar.top
        anchors.bottomMargin: osd.band ? 0 : 4
        height: 24
        activeFocusOnTab: enabled
        enabled: Player.duration > 0
        readonly property bool playerControl: true
        readonly property real frac: Player.duration > 0
            ? Math.max(0, Math.min(1, osd.displayedPosition / Player.duration)) : 0
        readonly property real previewFrac: Player.duration > 0
            ? Math.max(0, Math.min(1, osd.previewMs / Player.duration)) : 0
        readonly property bool engaged: timelineArea.containsMouse || Player.scrubbing || activeFocus
        Accessible.role: Accessible.Slider
        Accessible.name: "Playback position"
        Accessible.description: osd.formatTime(osd.displayedPosition) + " of " + osd.formatTime(Player.duration)
        Accessible.onIncreaseAction: Player.seekRelative(5)
        Accessible.onDecreaseAction: Player.seekRelative(-5)
        Keys.onLeftPressed: Player.seekRelative(-5)
        Keys.onRightPressed: Player.seekRelative(5)
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Home) { Player.seek(0); event.accepted = true }
            else if (event.key === Qt.Key_End) { Player.seek(Player.duration); event.accepted = true }
        }
        onActiveFocusChanged: if (activeFocus) osd.showControlsNow()

        Rectangle {
            id: track
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: timeline.engaged ? 5 : 3
            radius: height / 2
            color: "#4DFFFFFF"

            Rectangle {
                width: timeline.frac * track.width
                height: parent.height
                radius: height / 2
                color: timeline.engaged ? osd.cAccent : osd.cText
            }

            Rectangle {
                readonly property real aFrac: Player.abLoopA >= 0 && Player.duration > 0
                    ? Math.max(0, Math.min(1, Player.abLoopA / Player.duration)) : -1
                readonly property real bFrac: Player.abLoopB >= 0 && Player.duration > 0
                    ? Math.max(0, Math.min(1, Player.abLoopB / Player.duration)) : -1
                visible: aFrac >= 0 || bFrac >= 0
                height: parent.height
                radius: height / 2
                color: Qt.alpha(Colors.warning, 0.6)
                x: (aFrac >= 0 ? aFrac : 0) * track.width
                width: Math.max(2, ((bFrac >= 0 ? bFrac : 1) - (aFrac >= 0 ? aFrac : 0)) * track.width)
            }

            Loader {
                anchors.fill: parent
                active: Player.chapterList.length > 0
                asynchronous: true
                source: "ChapterTicks.qml"
            }
            Loader {
                id: bookmarkTicks
                anchors.fill: parent
                active: osd.bookmarks.length > 0
                asynchronous: true
                source: "BookmarkTicks.qml"
                onLoaded: item.marks = osd.bookmarks
            }
            Connections {
                target: osd
                function onBookmarksChanged() { if (bookmarkTicks.item) bookmarkTicks.item.marks = osd.bookmarks }
            }

            // A 2 px tick marks the position when the pointer is elsewhere
            // (visible at 0:00 too); in the bar it becomes the 10 px knob.
            Rectangle {
                visible: !timeline.engaged && !controlHover.hovered
                width: 2
                height: parent.height + 6
                radius: 1
                color: osd.cText
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(0, Math.min(track.width - width, timeline.frac * track.width - 1))
            }
            Rectangle {
                width: 10
                height: 10
                radius: 5
                color: osd.cText
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(0, Math.min(track.width - width, timeline.frac * track.width - width / 2))
                visible: timeline.engaged || controlHover.hovered
            }
        }

        Rectangle {
            id: previewBox
            visible: timelineArea.containsMouse || Player.scrubbing
            anchors.bottom: parent.top
            anchors.bottomMargin: 10
            x: Math.max(0, Math.min(timeline.width - width, timeline.previewFrac * timeline.width - width / 2))
            width: previewThumb.latestOk ? 180 : Math.max(84, previewTime.implicitWidth + 24)
            height: (previewThumb.latestOk ? 101 : 0) + 32 + (chapterText.visible ? 22 : 0)
            radius: 6
            color: Colors.raised
            border.color: Colors.border
            clip: true
            Image {
                id: previewThumb
                objectName: "timelinePreviewImage"
                width: parent.width
                height: latestOk ? 101 : 0
                fillMode: Image.PreserveAspectFit
                sourceSize: Qt.size(240, 135)
                cache: false
                property bool latestOk: false
                property int token: 0
            }
            Text {
                id: previewTime
                anchors.top: previewThumb.bottom
                anchors.topMargin: 8
                anchors.horizontalCenter: parent.horizontalCenter
                text: osd.formatTime(Player.scrubbing ? osd.scrubPos : osd.previewMs)
                color: osd.cText
                font.family: Fonts.ui
                font.pixelSize: 12
                font.features: { "tnum": 1 }
            }
            Text {
                id: chapterText
                readonly property var chapter: osd.chapterAt(osd.previewMs)
                visible: chapter !== null
                anchors.top: previewTime.bottom
                anchors.topMargin: 4
                width: parent.width - 16
                anchors.horizontalCenter: parent.horizontalCenter
                text: chapter ? chapter.title || "Chapter " + (chapter.index + 1) : ""
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                color: osd.cTextDim
                font.family: Fonts.ui
                font.pixelSize: 12
            }
        }

        MouseArea {
            id: timelineArea
            objectName: "timelineMouseArea"
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onPressed: mouse => {
                Player.scrubbing = true
                osd.scrubPos = osd.scrubTimeFor(mouse)
                osd.showControlsNow()
                osd.requestPreview(osd.scrubPos)
            }
            onPositionChanged: mouse => {
                if (!pressed && !containsMouse) return
                if (pressed) osd.scrubPos = osd.scrubTimeFor(mouse)
                osd.requestPreview(pressed ? osd.scrubPos
                    : Math.max(0, Math.min(Player.duration, mouse.x / timeline.width * Player.duration)))
                osd.showControls()
            }
            onReleased: {
                Player.seek(osd.scrubPos)
                Player.scrubbing = false
                if (!containsMouse) osd.cancelPreview()
                osd.showControlsNow()
            }
            onCanceled: {
                Player.scrubbing = false
                osd.cancelPreview()
            }
            onContainsMouseChanged: {
                if (containsMouse) {
                    osd.showControls()
                    osd.requestPreview(Math.max(0, Math.min(Player.duration, mouseX / timeline.width * Player.duration)))
                } else if (!pressed) {
                    osd.cancelPreview()
                }
            }
        }
    }

    Item {
        id: bar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: osd.sideMargin - 6
        anchors.rightMargin: osd.sideMargin - 6
        // Band: the row's centre 48 px below the band top and the seek
        // track 15 px below it, so the group reads centred in the 72 px.
        anchors.bottomMargin: osd.band ? 3
            // Never below the window: centre in the letterbox only when the
            // whole 42 px row fits there with room to spare; otherwise the
            // row overlaps the picture over the scrim, at the normal margin.
            : Math.max(osd.compact ? 6 : 10,
                       osd.letterbox >= 54 ? Math.round((osd.letterbox - height) / 2) : (osd.compact ? 10 : 16))
        height: 42

        Row {
            id: playbackControls
            spacing: osd.compact ? 0 : 6
            height: parent.height
            // Previous / next video either side of play, only with a queue.
            // Same commands and keys as the dock footer; dimmed at the ends
            // of the queue unless the playlist repeats.
            ControlButton {
                objectName: "previousVideoButton"
                visible: Playlist.totalCount > 1
                iconSource: "qrc:/assets/icons/skip-previous.svg"
                accessibleName: "Previous video"
                shortcutCommand: "playlist-prev"
                enabled: osd.playlistRepeat || Player.playlistPos > 0
                onActivated: Player.command(["playlist-prev"])
            }
            ControlButton {
                objectName: "playButton"
                iconSource: (Player.isPaused || Player.eofReached || Player.currentFile === "")
                            ? "qrc:/assets/icons/play.svg" : "qrc:/assets/icons/pause.svg"
                iconSize: 24
                accessibleName: Player.eofReached ? "Replay" : Player.isPaused ? "Play" : "Pause"
                shortcutCommand: "cycle pause"
                enabled: Player.currentFile !== "" && Player.playbackError === ""
                onActivated: Player.togglePause()
            }
            ControlButton {
                objectName: "nextVideoButton"
                visible: Playlist.totalCount > 1
                iconSource: "qrc:/assets/icons/skip-next.svg"
                accessibleName: "Next video"
                shortcutCommand: "playlist-next"
                enabled: osd.playlistRepeat || (Player.playlistPos >= 0 && Player.playlistPos < Playlist.totalCount - 1)
                onActivated: Player.command(["playlist-next"])
            }
            ControlButton {
                iconSource: "qrc:/assets/icons/seek-back.svg"
                iconSize: 24
                accessibleName: "Back 10 seconds"
                shortcutCommand: "seek -10"
                enabled: Player.duration > 0
                onActivated: Player.seekRelative(-10)
            }
            ControlButton {
                iconSource: "qrc:/assets/icons/seek-forward.svg"
                iconSize: 24
                accessibleName: "Forward 10 seconds"
                shortcutCommand: "seek 10"
                enabled: Player.duration > 0
                onActivated: Player.seekRelative(10)
            }
            Item {
                id: volumeGroup
                height: 42
                width: muteButton.width + (volumeSlider.visible ? volumeSlider.width + 8 : 0)
                ControlButton {
                    id: muteButton
                    iconSource: Player.isMuted || Player.volume === 0
                                ? "qrc:/assets/icons/volume-muted.svg" : "qrc:/assets/icons/volume.svg"
                    accessibleName: Player.isMuted ? "Unmute" : "Mute"
                    shortcutCommand: "cycle mute"
                    tooltipHints: [{ label: accessibleName, command: shortcutCommand },
                                   { label: "Volume", command: "add volume 5" },
                                   { label: "", command: "add volume -5" }]
                    onActivated: Player.setMuted(!Player.isMuted)
                }
                Item {
                    id: volumeSlider
                    anchors.left: muteButton.right
                    anchors.leftMargin: 4
                    width: 80
                    height: 42
                    visible: !osd.compact
                    activeFocusOnTab: visible
                    readonly property bool playerControl: true
                    readonly property real frac: Math.max(0, Math.min(1, Player.volume / 130))
                    Accessible.role: Accessible.Slider
                    Accessible.name: "Volume"
                    Accessible.description: Math.round(Player.volume) + "%"
                    Accessible.onIncreaseAction: Player.setVolume(Math.min(130, Player.volume + 5))
                    Accessible.onDecreaseAction: Player.setVolume(Math.max(0, Player.volume - 5))
                    Keys.onLeftPressed: Player.setVolume(Math.max(0, Player.volume - 5))
                    Keys.onRightPressed: Player.setVolume(Math.min(130, Player.volume + 5))
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 4
                        radius: 2
                        color: "#4DFFFFFF"
                        Rectangle {
                            width: volumeSlider.frac * parent.width
                            height: parent.height
                            radius: 2
                            color: Player.isMuted ? osd.cTextDim : osd.cText
                        }
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        x: Math.max(0, Math.min(parent.width - width, volumeSlider.frac * parent.width - width / 2))
                        width: 10
                        height: 10
                        radius: 5
                        color: osd.cText
                        visible: volumeArea.containsMouse || volumeSlider.activeFocus || controlHover.hovered
                    }
                    MouseArea {
                        id: volumeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onPressed: mouse => { osd.setVolumeFromX(mouse.x, width); Player.setMuted(false) }
                        onPositionChanged: mouse => { if (pressed) osd.setVolumeFromX(mouse.x, width) }
                    }
                }
            }
            Text {
                id: timeLabel
                objectName: "playbackTime"
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: osd.compact ? 6 : 12
                text: osd.formatPosition(osd.displayedPosition)
                      + " / " + osd.formatTime(Player.duration)
                color: osd.cText
                font.pixelSize: osd.compact ? 12 : 13
                font.family: Fonts.ui
                // Tabular figures: the counter does not jitter as it runs.
                font.features: { "tnum": 1 }
            }
        }

        Row {
            id: rightCluster
            anchors.right: parent.right
            height: parent.height
            // Same 48 px pitch as the left cluster.
            spacing: osd.compact ? 0 : 6
            ControlButton {
                iconSource: "qrc:/assets/icons/playlist.svg"
                accessibleName: "Playlist"
                shortcutCommand: "script-message app-toggle-playlist"
                active: osd.playlistActive
                onActivated: osd.playlistRequested()
            }
            ControlButton {
                iconSource: "qrc:/assets/icons/captions.svg"
                accessibleName: "Subtitles"
                shortcutCommand: "script-message app-subtitle-menu"
                active: osd.hasOpenPopup() && osd.trackMenuType === "sub"
                enabled: Player.currentFile !== ""
                onActivated: osd.openSubtitleMenu()
            }
            ControlButton {
                objectName: "audioTrackButton"
                iconSource: "qrc:/assets/icons/audio.svg"
                accessibleName: "Audio track"
                shortcutCommand: "script-message app-audio-menu"
                active: osd.hasOpenPopup() && osd.trackMenuType === "audio"
                enabled: Player.currentFile !== ""
                onActivated: osd.openAudioMenu()
            }
            ControlButton {
                iconSource: "qrc:/assets/icons/settings.svg"
                iconSize: 24
                accessibleName: "Playback settings"
                shortcutCommand: "script-message app-playback-menu"
                active: osd.hasOpenPopup() && ["settings", "speed", "aspect"].indexOf(osd.trackMenuType) >= 0
                onActivated: osd.openSettingsMenu()
            }
            ControlButton {
                iconSource: osd.fullscreenActive ? "qrc:/assets/icons/fullscreen-exit.svg"
                                                 : "qrc:/assets/icons/fullscreen.svg"
                accessibleName: osd.fullscreenActive ? "Exit fullscreen" : "Enter fullscreen"
                shortcutCommand: "script-message app-toggle-fullscreen"
                onActivated: osd.fullscreenRequested()
            }
        }
    }

    Loader {
        id: trackMenuLoader
        z: 30
        anchors.right: parent.right
        anchors.rightMargin: osd.sideMargin
        anchors.bottom: timeline.top
        anchors.bottomMargin: 12
        active: false
        source: "TrackMenu.qml"
        property bool preparing: false
        asynchronous: preparing
        property var pendingEntries: []
        property string pendingEmptyLabel: ""
        property var pendingSetter: null
        property bool pendingCanGoBack: false
        onLoaded: {
            item.toggleStates = Qt.binding(() => ({ loop: osd.loopFile, repeat: osd.playlistRepeat, top: Native.alwaysOnTop }))
            if (!preparing) item.configure(pendingEntries, pendingEmptyLabel, pendingSetter, osd.trackLabel, osd.trackMenuType, pendingCanGoBack)
            item.interacted.connect(function() { osd.showControlsNow() })
            item.backRequested.connect(function() { osd.openSettingsMenu() })
        }
    }

    Loader {
        id: chapterMenuLoader
        z: 30
        anchors.right: parent.right
        anchors.rightMargin: osd.sideMargin
        anchors.bottom: timeline.top
        anchors.bottomMargin: 12
        active: false
        source: "ChapterPicker.qml"
        onLoaded: {
            item.interacted.connect(function() { osd.showControlsNow() })
            item.backRequested.connect(function() { osd.openSettingsMenu() })
            item.open()
        }
    }
}
