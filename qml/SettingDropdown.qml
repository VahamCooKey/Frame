import QtQuick
import QtQuick.Window

Rectangle {
    id: root
    property var options: []
    property var value: undefined
    property bool open: false
    property string accessibleLabel: parent && parent.parent ? parent.parent.label || "Choose an option" : "Choose an option"
    property point below: Qt.point(0, 0)
    property point above: Qt.point(0, 0)
    readonly property Item popupHost: findHost()
    signal activated(var newValue)
    width: 220
    radius: 7
    height: 30
    color: area.containsMouse || open ? Colors.fieldHover : Colors.field
    border.color: activeFocus ? Colors.accent : Colors.fieldBorder
    opacity: enabled ? 1 : 0.4
    activeFocusOnTab: true
    Accessible.role: Accessible.ComboBox
    Accessible.name: accessibleLabel
    Accessible.description: labelFor(value)
    Accessible.onPressAction: toggle()
    Keys.onReturnPressed: toggle()
    Keys.onSpacePressed: toggle()
    Keys.onDownPressed: if (!open) toggle()

    function findHost() {
        let node = parent
        while (node) {
            if (node.settingsPopupLayer !== undefined) return node.settingsPopupLayer
            node = node.parent
        }
        return root.Window.window ? root.Window.window.contentItem : root
    }
    function labelFor(v) {
        for (let i = 0; i < options.length; ++i)
            if (String(options[i].value) === String(v)) return options[i].label
        return String(v)
    }
    function toggle() {
        if (!enabled || options.length === 0) return
        if (open) { open = false; return }
        if (popupHost.dismiss) popupHost.dismiss()
        below = mapToItem(popup, 0, height + 6)
        above = mapToItem(popup, 0, -6)
        open = true
        choices.currentIndex = 0
        for (let i = 0; i < options.length; ++i)
            if (String(options[i].value) === String(value)) choices.currentIndex = i
        choices.forceActiveFocus()
        choices.positionViewAtIndex(choices.currentIndex, ListView.Contain)
    }
    function choose(index) {
        if (index < 0 || index >= options.length) return
        // The consumer owns value; assigning here would break its binding
        // to saved preferences and prevent later reset/external updates.
        activated(options[index].value)
        open = false
        forceActiveFocus()
    }
    onVisibleChanged: if (!visible) open = false
    Connections {
        target: root.popupHost
        ignoreUnknownSignals: true
        function onDismiss() { root.open = false }
    }
    Text {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: arrow.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.labelFor(root.value)
        color: Colors.text
        font.family: Fonts.ui
        font.pixelSize: 13
        elide: Text.ElideRight
    }
    Image {
        id: arrow
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 12
        height: 12
        sourceSize: Qt.size(24, 24)
        source: "qrc:/assets/icons/chevron-down.svg"
        opacity: 0.65
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggle()
    }
    // Portal into the settings panel; flip above the control when needed.
    Item {
        id: popup
        parent: root.popupHost
        anchors.fill: parent
        z: 100
        visible: root.open && root.visible
        MouseArea { anchors.fill: parent; onClicked: { root.open = false; root.forceActiveFocus() } }
        Rectangle {
            id: optionPanel
            objectName: "settingsChoicePopup"
            width: Math.min(popup.width - 32, Math.max(root.width, 244))
            height: Math.min(root.options.length * 26 + 8, popup.height - 32, 242)
            x: Math.max(16, Math.min(popup.width - width - 16, root.below.x))
            y: root.below.y + height <= popup.height - 16 ? root.below.y
               : Math.max(16, Math.min(popup.height - height - 16, root.above.y - height))
            radius: 8
            color: Colors.surface
            border.color: Colors.border
            ListView {
                id: choices
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: root.open ? root.options : []
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: true
                keyNavigationWraps: true
                Keys.onReturnPressed: root.choose(currentIndex)
                Keys.onEnterPressed: root.choose(currentIndex)
                Keys.onSpacePressed: root.choose(currentIndex)
                Keys.onEscapePressed: { root.open = false; root.forceActiveFocus() }
                delegate: Rectangle {
                    id: option
                    required property int index
                    required property var modelData
                    width: choices.width
                    height: 26
                    radius: 5
                    color: optionArea.containsMouse || ListView.isCurrentItem ? Colors.hover : "transparent"
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 34
                        anchors.verticalCenter: parent.verticalCenter
                        text: option.modelData.label
                        elide: Text.ElideRight
                        color: Colors.text
                        font.family: Fonts.ui
                        font.pixelSize: 13
                    }
                    Image {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14
                        height: 14
                        sourceSize: Qt.size(28, 28)
                        source: "qrc:/assets/icons/check.svg"
                        visible: String(option.modelData.value) === String(root.value)
                    }
                    MouseArea {
                        id: optionArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        Accessible.role: Accessible.MenuItem
                        Accessible.name: option.modelData.label
                        Accessible.onPressAction: root.choose(option.index)
                        onClicked: root.choose(option.index)
                    }
                }
            }
            Rectangle {
                x: parent.width - 4
                y: 6 + choices.visibleArea.yPosition * choices.height
                width: 2
                height: Math.max(16, choices.visibleArea.heightRatio * choices.height)
                color: Colors.scrollThumb
                visible: choices.contentHeight > choices.height
            }
        }
    }
}
