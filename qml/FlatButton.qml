import QtQuick

// The two button styles used everywhere (2026-11-05): secondary (filled
// dark, hairline border) and primary (light fill, dark text), one primary
// per dialog. `danger` tints a secondary button's text for destructive
// actions; `confirmLabel` makes the first press arm the button and the
// second (within four seconds) run it.
Rectangle {
    id: root
    property string label: ""
    property bool highlighted: false
    property bool primary: false
    property bool danger: false
    property string confirmLabel: ""
    property bool armed: false
    signal activated()
    readonly property bool playerControl: true
    readonly property string shownLabel: armed && confirmLabel !== "" ? confirmLabel : label
    implicitWidth: text.implicitWidth + 24
    height: 28
    radius: 7
    // A disabled primary keeps its shape as a dim secondary fill.
    color: primary && !enabled ? Colors.field
         : primary ? (area.containsMouse ? Colors.primaryHover : Colors.primary)
         : armed ? Qt.alpha(Colors.danger, 0.16)
         : highlighted || area.containsMouse ? Colors.fieldHover : Colors.field
    border.color: activeFocus ? Colors.accent
                : primary ? "transparent"
                : armed ? Qt.alpha(Colors.danger, 0.5) : Colors.fieldBorder
    border.width: activeFocus ? 2 : 1
    opacity: enabled || primary ? 1 : 0.4
    activeFocusOnTab: true
    function press() {
        if (!enabled) return
        if (confirmLabel !== "" && !armed) { armed = true; disarm.restart(); return }
        armed = false
        activated()
    }
    Timer { id: disarm; interval: 4000; onTriggered: root.armed = false }
    Keys.onSpacePressed: press()
    Keys.onReturnPressed: press()
    Keys.onEnterPressed: press()
    Accessible.role: Accessible.Button
    Accessible.name: shownLabel
    Accessible.onPressAction: press()
    Text {
        id: text
        anchors.centerIn: parent
        text: root.shownLabel
        color: root.primary ? (root.enabled ? Colors.textOnAccent : Colors.textDim)
             : root.danger ? Colors.danger : Colors.text
        font.family: Fonts.ui
        font.pixelSize: 13
        font.weight: root.primary ? Font.Medium : Font.Normal
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.press()
    }
}
