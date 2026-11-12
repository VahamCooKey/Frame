import QtQuick

Item {
    id: root
    property bool checked: false
    signal toggled(bool value)
    width: 38
    height: 24
    opacity: enabled ? 1 : 0.4
    activeFocusOnTab: true
    function toggle() { if (enabled) { checked = !checked; toggled(checked) } }
    Accessible.role: Accessible.CheckBox
    Accessible.name: parent && parent.parent ? parent.parent.label || "Toggle setting" : "Toggle setting"
    Accessible.checked: checked
    Accessible.onPressAction: toggle()
    Keys.onSpacePressed: toggle()
    Keys.onReturnPressed: toggle()
    Rectangle {
        anchors.centerIn: parent
        width: 34
        height: 18
        radius: 9
        // On: accent track, dark knob. Off: dark track with a visible rim
        // and a dimmer knob, so the state reads at a glance in Mono too.
        readonly property bool on: root.checked && root.enabled
        color: on ? Colors.accent : Colors.trackOff
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Colors.text : (on ? Colors.accent : Colors.trackOffBorder)
        Behavior on color { ColorAnimation { duration: Native.animationsEnabled() ? 120 : 0 } }
        Rectangle {
            width: 14; height: 14; radius: 7
            y: 2
            x: parent.on ? 18 : 2
            color: parent.on ? Colors.knobOn : Colors.knobOff
            Behavior on x { NumberAnimation { duration: Native.animationsEnabled() ? 120 : 0 } }
        }
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.toggle() }
}
