import QtQuick

// Keeps the complete playback-control tree out of the first QML object
// graph. Its public surface deliberately mirrors
// OnScreenController so main.qml can stage it without changing behaviour.
Item {
    id: root

    property bool active: false
    property bool menuOpen: false
    property bool shown: false
    // Outlives shown during fade-out; reserve subtitle space until the
    // actual controls are no longer painted.
    readonly property bool painting: loader.item !== null
                                    && (loader.item.shown || loader.item.visible)
    property bool pinned: false
    property bool fullscreenActive: false
    property bool playlistActive: false
    property bool playlistInteracting: false
    // F6c: which of elapsed/remaining/timecode the playhead label shows.
    property int timeDisplayMode: 0
    // F6e: seconds owned by main.qml; the ticks Loader activates on it.
    property var bookmarks: []
    property bool loopFile: false
    property bool playlistRepeat: false
    property int aspectIndex: 0
    property real letterbox: 0
    property bool showPending: false
    // Inline values for the right-click menu (main.qml valueHints).
    readonly property string currentAudioLabel: loader.item ? loader.item.currentAudioLabel : ""
    readonly property string currentSubtitleLabel: loader.item ? loader.item.currentSubtitleLabel : ""
    readonly property var aspectLabels: loader.item ? loader.item.aspectLabels : []
    function speedLabel(rate) { return loader.item ? loader.item.speedLabel(rate) : "" }

    signal fullscreenRequested()
    signal playlistRequested()
    signal settingsRequested()
    signal hidden()
    signal openSubtitleRequested()

    function preloadMenus() { if (loader.item) loader.item.preloadMenus() }
    function openMenu(type) {
        active = true
        if (loader.item) loader.item.openMenu(type)
    }

    function showControls() {
        showPending = true
        if (loader.item)
            loader.item.showControls()
    }

    function showControlsNow() {
        showPending = true
        if (loader.item)
            loader.item.showControlsNow()
    }

    function hideNow() {
        showPending = false
        if (loader.item)
            loader.item.hideNow()
        else
            shown = false
    }

    function hasOpenPopup() {
        return loader.item && loader.item.hasOpenPopup()
    }

    function closePopups() {
        if (loader.item)
            loader.item.closePopups()
    }

    onPinnedChanged: if (loader.item) loader.item.pinned = pinned
    onFullscreenActiveChanged: if (loader.item) loader.item.fullscreenActive = fullscreenActive
    onPlaylistActiveChanged: if (loader.item) loader.item.playlistActive = playlistActive
    onPlaylistInteractingChanged: if (loader.item) loader.item.playlistInteracting = playlistInteracting
    onMenuOpenChanged: if (loader.item) loader.item.menuOpen = menuOpen
    onTimeDisplayModeChanged: if (loader.item) loader.item.timeDisplayMode = timeDisplayMode
    onBookmarksChanged: if (loader.item) loader.item.bookmarks = bookmarks
    onLoopFileChanged: if (loader.item) loader.item.loopFile = loopFile
    onPlaylistRepeatChanged: if (loader.item) loader.item.playlistRepeat = playlistRepeat
    onAspectIndexChanged: if (loader.item) loader.item.aspectIndex = aspectIndex
    onLetterboxChanged: if (loader.item) loader.item.letterbox = letterbox

    Loader {
        id: loader
        anchors.fill: parent
        active: root.active
        // This component is now QtQuick-only and inexpensive. Asynchronous
        // incubation spread its object tree across hundreds of render-frame
        // budgets, making the player look unfinished for several seconds.
        asynchronous: false
        sourceComponent: Component { OnScreenController {} }

        onLoaded: {
            console.log("startup: OSD loaded")
            item.pinned = root.pinned
            item.fullscreenActive = root.fullscreenActive
            item.playlistActive = root.playlistActive
            item.playlistInteracting = root.playlistInteracting
            item.menuOpen = root.menuOpen
            item.timeDisplayMode = root.timeDisplayMode
            item.bookmarks = root.bookmarks
            item.loopFile = root.loopFile
            item.playlistRepeat = root.playlistRepeat
            item.aspectIndex = root.aspectIndex
            item.letterbox = root.letterbox
            root.shown = item.shown
            item.fullscreenRequested.connect(function() { root.fullscreenRequested() })
            item.playlistRequested.connect(function() { root.playlistRequested() })
            item.settingsRequested.connect(function() { root.settingsRequested() })
            item.hidden.connect(function() { root.hidden() })
            item.openSubtitleRequested.connect(function() { root.openSubtitleRequested() })
            if (root.showPending)
                item.showControlsNow()
        }
    }

    Connections {
        target: loader.item

        function onShownChanged() {
            root.shown = target.shown
        }
    }
}
