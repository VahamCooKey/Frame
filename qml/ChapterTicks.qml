import QtQuick

// Instantiated only for media that actually contains chapters. Keeping the
// Repeater out of the base OSD avoids loading QtQml.Models at app startup.
Item {
    Repeater {
        model: Player.chapterList

        delegate: Item {
            id: tick

            required property var modelData

            // mpv's chapter-list entries are {title, time}: position was
            // never a key, so the old binding produced NaN and no ticks.
            readonly property real markX: Player.duration > 0
                ? Math.min(parent.width - width,
                           (modelData.time !== undefined ? modelData.time : 0)
                           / Player.duration * parent.width)
                : 0

            x: markX
            width: 10                       // hit target; the visual is 2px
            height: parent.height

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 2
                height: parent.height
                radius: 1
                color: tickArea.containsMouse ? "#F3F3F3" : "#A3A3A3"
            }

            MouseArea {
                id: tickArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: if (Player.duration > 0)
                               Player.seek(tick.modelData.time !== undefined ? tick.modelData.time : 0)
            }
        }
    }
}
