import QtQuick

// One shortcut drawn as small key chips ("Ctrl" "PgUp"). Every menu,
// tooltip and panel uses this so shortcuts read the same everywhere.
Row {
    id: root
    // Humanized key sequence, e.g. "Ctrl+PgUp". Empty draws nothing.
    property string sequence: ""
    property color textColor: Colors.textDim
    readonly property var keys: KeyNames.split(sequence)

    // Keys of one chord sit 2 px apart; callers separate alternatives
    // (a different key for the same action) with a dim "/".
    spacing: 2
    visible: keys.length > 0

    Repeater {
        model: root.keys
        delegate: Rectangle {
            required property string modelData
            width: Math.max(20, cap.implicitWidth + 10)
            height: 20
            radius: 4
            color: "#14FFFFFF"
            border.width: 1
            border.color: "#1FFFFFFF"
            Text {
                id: cap
                anchors.centerIn: parent
                text: parent.modelData
                color: root.textColor
                font.family: Fonts.ui
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
    }
}
