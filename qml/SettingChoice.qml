import QtQuick

// Segmented choice: a recessed well with the selected segment lifted.
// Segments fit their labels (12 px each side, 48 px minimum), so a long
// label never runs into its neighbours. An option may carry a `swatch`
// colour, drawn as a dot before its label (the accent picker).
Rectangle {
    id: root
    property var options: []
    property var value: undefined
    signal activated(var newValue)
    width: segments.implicitWidth + 4
    height: 28
    radius: 7
    color: Colors.control
    border.color: Colors.divider
    opacity: enabled ? 1 : 0.4
    Row {
        id: segments
        x: 2
        y: 2
        spacing: 2
        Repeater {
            model: root.options
            delegate: Rectangle {
                id: choice
                required property var modelData
                required property int index
                readonly property bool selected: String(root.value).toUpperCase() === String(modelData.value).toUpperCase()
                width: Math.max(48, content.implicitWidth + 24)
                height: root.height - 4
                radius: 5
                color: selected ? Colors.controlSelected : (area.containsMouse ? Colors.hover : "transparent")
                border.color: activeFocus ? Colors.accent : "transparent"
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: modelData.name || modelData.label
                Accessible.checkable: true
                Accessible.checked: selected
                Accessible.onPressAction: choose()
                Keys.onReturnPressed: choose()
                Keys.onSpacePressed: choose()
                function choose() { root.activated(modelData.value) }
                Row {
                    id: content
                    anchors.centerIn: parent
                    spacing: 6
                    Rectangle {
                        visible: choice.modelData.swatch !== undefined
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4
                        color: choice.modelData.swatch || "transparent"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: choice.modelData.label
                        color: choice.selected ? Colors.text : Colors.textDim
                        font.family: Fonts.ui
                        font.pixelSize: 12
                        font.features: { "tnum": 1 }
                        font.weight: choice.selected ? Font.Medium : Font.Normal
                    }
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: choice.choose()
                }
            }
        }
    }
}
