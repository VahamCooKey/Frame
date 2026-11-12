import QtQuick

// One titled group of settings rows, drawn flat on the page: a 16 px
// title, then rows separated by hairlines (SettingRow draws them). No card
// fill or border, so the panel never shows a box inside a box.
Column {
    id: root
    default property alias rows: body.data
    property string title: ""
    width: parent ? parent.width : 0
    spacing: 2
    Text {
        visible: root.title !== ""
        text: root.title
        color: Colors.text
        font.family: Fonts.ui
        font.pixelSize: 16
        font.weight: Font.DemiBold
        bottomPadding: 2
        Accessible.role: Accessible.Heading
        Accessible.name: root.title
    }
    Column { id: body; width: parent.width }
}
