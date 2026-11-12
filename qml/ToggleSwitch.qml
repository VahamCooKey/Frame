import QtQuick

// Menu-sized on/off switch, the same look as the settings SettingToggle.
// Display only: the menu row around it handles clicks and keys.
Rectangle {
    id: root
    property bool checked: false
    width: 28
    height: 16
    radius: height / 2
    color: checked ? Colors.accent : Colors.trackOff
    border.color: checked ? Colors.accent : Colors.trackOffBorder
    Behavior on color { ColorAnimation { duration: Native.animationsEnabled() ? 120 : 0 } }
    Rectangle {
        width: 12; height: 12; radius: 6
        y: 2
        x: root.checked ? root.width - width - 2 : 2
        color: root.checked ? Colors.knobOn : Colors.knobOff
        Behavior on x { NumberAnimation { duration: Native.animationsEnabled() ? 120 : 0 } }
    }
}
