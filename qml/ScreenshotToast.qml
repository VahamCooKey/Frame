import QtQuick

// A54. Corner notice for a saved screenshot: what it was called, where it
// went, and a click to open that folder with the file selected. The same
// card reports a failed save with mpv's reason. It never takes focus and
// never blocks a click meant for the video: it only covers its own area.
Rectangle {
    id: toast
    // Absolute path with forward slashes, as PlayerController reports it.
    property string filePath: ""
    // Non-empty switches the card to the failure variant.
    property string failure: ""
    property bool shown: false
    // Set once the file is also on the clipboard (arrives a moment after
    // the save, from the controller's reader thread).
    property bool copied: false
    property int holdMs: 4200
    readonly property bool failed: failure !== ""
    readonly property bool onWindows: Qt.platform.os === "windows"
    readonly property string nativePath: onWindows ? filePath.replace(/\//g, "\\") : filePath
    readonly property string fileName: filePath.substring(filePath.lastIndexOf("/") + 1)
    readonly property string folder: nativePath.substring(0, Math.max(0, nativePath.lastIndexOf(onWindows ? "\\" : "/")))
    signal revealRequested(string path)

    function showSaved(path) {
        failure = ""
        filePath = path
        copied = false
        shown = true
        hideTimer.restart()
    }
    function markCopied(path, ok) {
        if (ok && path === filePath && shown)
            copied = true
    }
    function showFailed(message) {
        failure = message
        shown = true
        hideTimer.restart()
    }
    function dismiss() {
        shown = false
        hideTimer.stop()
    }

    objectName: "screenshotToast"
    width: 320
    height: column.implicitHeight + 24
    radius: 8
    color: "#F0181818"
    border.color: "#25FFFFFF"
    visible: opacity > 0
    opacity: shown ? 1 : 0
    // Slides in a few pixels from the edge and settles; reduced motion makes both instant.
    anchors.rightMargin: shown ? 20 : 8
    Behavior on opacity { NumberAnimation { duration: Native.animationsEnabled() ? 160 : 0 } }
    Behavior on anchors.rightMargin { NumberAnimation { duration: Native.animationsEnabled() ? 200 : 0; easing.type: Easing.OutCubic } }

    Accessible.role: Accessible.Notification
    Accessible.name: failed ? "Screenshot not saved. " + failure
                            : "Screenshot saved as " + fileName + " in " + folder
                              + (copied ? ", and copied to the clipboard" : "")

    // Hovering holds the card; a click (saved variant) reveals the file.
    HoverHandler { id: hover; cursorShape: toast.failed ? Qt.ArrowCursor : Qt.PointingHandCursor }
    TapHandler {
        onTapped: {
            if (toast.failed) { toast.dismiss(); return }
            toast.revealRequested(toast.filePath)
            toast.dismiss()
        }
    }
    Timer {
        id: hideTimer
        interval: toast.holdMs
        onTriggered: {
            if (hover.hovered) { restart(); return }
            toast.shown = false
        }
    }

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 3

        Row {
            spacing: 8
            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 14; height: 14
                visible: !toast.failed
                source: "qrc:/assets/icons/check.svg"
                sourceSize: Qt.size(Math.ceil(14 * Screen.devicePixelRatio), Math.ceil(14 * Screen.devicePixelRatio))
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: toast.failed ? "Couldn't save the screenshot" : "Screenshot saved"
                color: toast.failed ? "#F0A8A8" : "#F3F3F3"
                font.family: Fonts.ui
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: toast.failed ? toast.failure : toast.fileName
            elide: Text.ElideMiddle
            color: "#F3F3F3"
            font.family: Fonts.ui
            font.pixelSize: 12
        }
        Text {
            width: parent.width
            visible: !toast.failed && text !== ""
            text: toast.folder
            elide: Text.ElideMiddle
            color: "#A3A3A3"
            font.family: Fonts.ui
            font.pixelSize: 12
        }
        Text {
            width: parent.width
            visible: !toast.failed
            text: toast.copied ? "Copied to clipboard  ·  Click to show in folder" : "Click to show in folder"
            color: hover.hovered ? "#F3F3F3" : "#7A7A7A"
            font.family: Fonts.ui
            font.pixelSize: 12
            topPadding: 3
        }
    }
}
