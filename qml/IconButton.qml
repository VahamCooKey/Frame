import QtQuick
import QtQuick.Window

Rectangle {
    id: root

    property url iconSource: ""
    property real iconSize: 18
    property color hoverColor: "#22FFFFFF"
    property real idleIconOpacity: 0.78
    property string accessibleName: ""
    property string shortcutCommand: ""
    // Tooltip content: label plus key chips. Override for multi-key tips,
    // e.g. [{ label: "Mute", command: "cycle mute" }, { label: "Volume", keys: "UP" }].
    property var tooltipHints: [{ label: accessibleName, command: shortcutCommand }]
    readonly property string tooltipText: tipHints.text
    // Shared neutral icon geometry; selection is a small underline.
    property bool active: false
    property color activeColor: Colors.accent
    // Buttons that show their active state another way (the control band's
    // plate) turn the underline off.
    property bool activeUnderline: true
    property bool tooltipEnabled: false
    readonly property bool playerControl: true
    readonly property bool hovered: pointer.containsMouse

    signal activated()
    signal interaction()

    width: 34
    height: 34
    radius: 6
    color: pointer.containsMouse || activeFocus ? hoverColor : "transparent"
    border.width: activeFocus ? 1 : 0
    border.color: Colors.accent
    activeFocusOnTab: true
    opacity: enabled ? 1 : 0.35
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.description: tooltipText
    Accessible.onPressAction: if (enabled) activated()
    Keys.onReturnPressed: activated()
    Keys.onEnterPressed: activated()
    Keys.onSpacePressed: activated()
    onActiveFocusChanged: if (activeFocus) interaction()

    Behavior on color { ColorAnimation { duration: Native.animationsEnabled() ? 100 : 0 } }

    Image {
        id: icon
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        source: root.iconSource
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        cache: true
        opacity: pointer.containsMouse ? 1 : root.idleIconOpacity
        sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
                            Math.ceil(height * Screen.devicePixelRatio))

        Behavior on opacity { NumberAnimation { duration: Native.animationsEnabled() ? 100 : 0 } }
    }

    // A tiny indicator communicates state without allocating an effect layer
    // or tinting the otherwise monochrome icon set.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        width: 12
        height: 2
        radius: 1
        color: root.activeColor
        visible: root.active && root.activeUnderline
    }

    Timer {
        id: tipDelay
        interval: 550
        running: root.tooltipEnabled && root.hovered
        onTriggered: tip.visible = true
    }

    Rectangle {
        id: tip
        visible: false
        z: 50
        // Keep long shortcut hints inside the window at either edge.
        x: Math.max(-root.mapToItem(null, 0, 0).x + 8,
                    Math.min((root.width - width) / 2,
                             (root.Window.window ? root.Window.window.width : 10000)
                             - root.mapToItem(null, 0, 0).x - width - 8))
        anchors.bottom: parent.top
        anchors.bottomMargin: 8
        width: tipHints.width + 20
        height: Math.max(30, tipHints.height + 12)
        radius: 6
        color: Colors.raised
        border.color: Colors.border
        ShortcutHints {
            id: tipHints
            anchors.centerIn: parent
            hints: root.tooltipHints
            maxWidth: (root.Window.window ? root.Window.window.width : 10000) - 36
            labelColor: Colors.text
        }
    }
    onHoveredChanged: if (!hovered) tip.visible = false

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: { root.interaction(); root.activated() }
        onPositionChanged: if (containsMouse) root.interaction()
        // A hide/disable can generate a synthetic hover exit. It must not
        // wake the chrome again while its visibility binding is updating.
        onContainsMouseChanged: if (containsMouse) root.interaction()
    }
}
