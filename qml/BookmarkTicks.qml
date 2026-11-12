import QtQuick

// F6e. Loaded only for files that actually have bookmarks, mirroring
// ChapterTicks: keeps the Repeater (and QtQml.Models) out of the base OSD.
//
// The model is a plain JS array of seconds owned by main.qml, refreshed on
// file load and after every toggle — SettingsStore is a stateless store, so
// there is no property to bind to directly.
Item {
    id: root

    property var marks: []

    Repeater {
        model: root.marks

        delegate: Item {
            id: tick

            required property var modelData

            readonly property real markX: Player.duration > 0
                ? Math.min(parent.width - width,
                           Math.max(0, modelData / Player.duration * parent.width))
                : 0

            x: markX
            width: 10                       // hit target; the visual is 2px
            height: parent.height

            // Drawn below the playhead handle but above the track fill, and
            // in the accent amber so it never reads as a chapter boundary.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: 2
                height: parent.height * 0.6
                radius: 1
                color: tickArea.containsMouse ? "#FFE9A8" : "#FFC93F"
            }

            MouseArea {
                id: tickArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Player.seek(tick.modelData)
            }
        }
    }
}
