import QtQuick

Rectangle {
    id: root
    objectName: "playbackFailure"
    property string fileName: ""
    property string message: ""
    signal retryRequested()
    signal openRequested()
    color: Colors.bg
    Accessible.role: Accessible.Pane
    Accessible.name: "Couldn't open this video"
    Accessible.description: fileName + ". " + message

    Column {
        anchors.centerIn: parent
        width: Math.max(160, Math.min(420, parent.width - 48))
        spacing: 12
        Text {
            width: parent.width
            text: "Couldn't open this video"
            color: Colors.text
            font.family: Fonts.ui
            font.pixelSize: 19
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            text: root.fileName
            visible: text !== ""
            color: Colors.text
            font.family: Fonts.ui
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
        }
        Text {
            width: parent.width
            text: root.message
            color: Colors.textDim
            font.family: Fonts.ui
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Flow {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width, openButton.implicitWidth + retryButton.implicitWidth + spacing)
            spacing: 10
            FlatButton {
                id: openButton
                objectName: "errorOpenButton"
                label: "Open another file"
                primary: true
                onActivated: root.openRequested()
            }
            FlatButton {
                id: retryButton
                objectName: "errorRetryButton"
                label: "Retry"
                enabled: root.fileName !== ""
                onActivated: root.retryRequested()
            }
        }
    }
}
