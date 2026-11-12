import QtQuick

Item {
    id: root
    property real from: 0
    property real to: 1
    property real value: 0
    property real stepSize: 1
    property int decimals: 0
    property string suffix: ""
    signal moved(real newValue)
    width: 220
    height: 28
    activeFocusOnTab: true
    opacity: enabled ? 1 : 0.4
    Accessible.role: Accessible.Slider
    Accessible.name: parent && parent.parent ? parent.parent.label || "Adjust value" : "Adjust value"
    Accessible.description: value.toFixed(decimals) + suffix
    Accessible.onIncreaseAction: setValue(value + stepSize)
    Accessible.onDecreaseAction: setValue(value - stepSize)
    Keys.onLeftPressed: setValue(value - stepSize)
    Keys.onRightPressed: setValue(value + stepSize)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Home) { setValue(from); event.accepted = true }
        if (event.key === Qt.Key_End) { setValue(to); event.accepted = true }
    }
    function ratio() { return Math.max(0, Math.min(1, (value - from) / Math.max(0.0001, to - from))) }
    function setValue(v) {
        value = Math.max(from, Math.min(to, Math.round(v / stepSize) * stepSize))
        moved(value)
    }
    Rectangle {
        id: track
        x: 7; width: root.width - 78
        anchors.verticalCenter: parent.verticalCenter
        height: 4; radius: 2; color: Colors.trackOff
        // A range around zero (delays) fills from the centre to the knob,
        // so 0 reads as "none" rather than "half".
        readonly property real zeroRatio: root.from < 0 && root.to > 0 ? (0 - root.from) / (root.to - root.from) : 0
        Rectangle {
            x: Math.min(track.zeroRatio, root.ratio()) * parent.width
            width: Math.abs(root.ratio() - track.zeroRatio) * parent.width
            height: parent.height; radius: 2; color: Colors.accent
        }
        Rectangle {
            visible: track.zeroRatio > 0
            x: track.zeroRatio * parent.width - 1
            anchors.verticalCenter: parent.verticalCenter
            width: 2; height: 10; radius: 1
            color: Colors.trackOffBorder
        }
        Rectangle {
            x: root.ratio() * track.width - 7
            anchors.verticalCenter: parent.verticalCenter
            width: 14; height: 14; radius: 7
            color: Colors.text
            border.width: root.activeFocus ? 2 : 0
            border.color: Colors.accent
        }
    }
    MouseArea {
        width: track.width + 14; height: parent.height
        cursorShape: Qt.PointingHandCursor
        onPressed: mouse => root.setValue(root.from + Math.max(0, Math.min(1, (mouse.x - 7) / track.width)) * (root.to - root.from))
        onPositionChanged: mouse => { if (pressed) root.setValue(root.from + Math.max(0, Math.min(1, (mouse.x - 7) / track.width)) * (root.to - root.from)) }
    }
    Rectangle {
        anchors.right: parent.right
        width: 58; height: 26; radius: 6
        anchors.verticalCenter: parent.verticalCenter
        color: Colors.field
        border.color: Colors.fieldBorder
        Text {
            anchors.centerIn: parent
            text: root.value.toFixed(root.decimals) + root.suffix
            font.features: { "tnum": 1 }
            color: Colors.text
            font.family: Fonts.ui
            font.pixelSize: 12
        }
    }
}
