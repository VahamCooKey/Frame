import QtQuick

// A62: delayed, non-modal feedback for outstanding subtitle preparation.
// Completion comes from the controller; there is no guessed percentage.
Rectangle {
    id: notice
    property bool loading: false
    property string fontFamily: Qt.application.font.family
    property int delayMs: 180
    readonly property bool shown: visible
    visible: false
    implicitWidth: label.implicitWidth + 28
    implicitHeight: 38
    color: "#E6202020"
    radius: 8
    Accessible.role: Accessible.Notification
    Accessible.name: qsTr("Loading subtitles…")

    function updateLoading() {
        if (loading) {
            if (!visible && !delay.running) delay.start()
        } else {
            delay.stop()
            visible = false
        }
    }
    onLoadingChanged: updateLoading()
    Component.onCompleted: updateLoading()
    Timer {
        id: delay
        interval: notice.delayMs
        onTriggered: if (notice.loading) notice.visible = true
    }
    Text {
        id: label
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        verticalAlignment: Text.AlignVCenter
        text: qsTr("Loading subtitles…")
        font.family: notice.fontFamily
        font.pixelSize: 13
        color: "#EEEEEE"
        elide: Text.ElideRight
    }
    // No input handler: playback controls keep their focus and pointer input.
}
