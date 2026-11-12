import QtQuick

// Prepare after playback settles; keep the created panel across closes.
// Hidden preparation never opens the panel, focuses a field, or blocks keys.
Item {
    id: root
    anchors.fill: parent
    property bool active: false
    property bool loadRequested: false
    readonly property bool ready: loader.status === Loader.Ready
    property real prepareStarted: 0
    property real openStarted: 0
    opacity: active ? 1 : 0
    visible: active || opacity > 0
    enabled: active
    Behavior on opacity {
        NumberAnimation { duration: Native.animationsEnabled() ? (root.active ? 140 : 90) : 0; easing.type: Easing.OutCubic }
    }
    signal openShaderRequested()

    function preload() {
        if (loadRequested) return
        prepareStarted = Date.now()
        loadRequested = true
    }
    function present() {
        loader.item.openDialog()
        console.info("ui: settings open " + (Date.now() - openStarted) + " ms; prepared=" + (prepareStarted < openStarted))
    }
    function openDialog() {
        if (active) return
        openStarted = Date.now()
        active = true
        preload()
        if (ready) present()
    }
    function closeDialog() {
        if (ready) loader.item.closeDialog()
        else active = false
    }
    Loader {
        id: loader
        anchors.fill: parent
        active: root.loadRequested
        asynchronous: true
        sourceComponent: Component { SettingsDialog {} }
        onLoaded: {
            item.closed.connect(function() { root.active = false })
            item.openShaderRequested.connect(function() { root.openShaderRequested() })
            item.prepare()
            console.info("ui: settings prepared " + (Date.now() - root.prepareStarted) + " ms")
            if (root.active) root.present()
        }
    }
    // An unusually early click gets immediate feedback while incubation
    // continues, instead of freezing the video behind a synchronous load.
    Rectangle {
        anchors.fill: parent
        color: "#88000000"
        visible: !root.ready
        MouseArea { anchors.fill: parent; onClicked: root.closeDialog() }
        Text {
            anchors.centerIn: parent
            text: loader.status === Loader.Error ? "Settings could not open. Click to dismiss." : "Opening settings…"
            color: Colors.text
            font.family: Fonts.ui
            font.pixelSize: 14
        }
    }
}
