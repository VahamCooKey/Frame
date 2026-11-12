import QtQuick

Item {
    id: root
    property int fileCount: 0
    property bool subtitle: false
    property int activeChoice: 0
    readonly property bool stacked: width < 600
    readonly property bool shown: fileCount > 0
    // Keep the last labels through the exit fade instead of flashing
    // "0 files" or the other action as the drag state is cleared.
    property int displayedCount: 0
    property bool displayedSubtitle: false
    property int displayedChoice: 1
    onFileCountChanged: if (fileCount > 0) displayedCount = fileCount
    onSubtitleChanged: if (shown) displayedSubtitle = subtitle
    onActiveChoiceChanged: if (activeChoice > 0) displayedChoice = activeChoice
    onShownChanged: if (shown) displayedSubtitle = subtitle
    opacity: shown ? 1 : 0
    visible: shown || opacity > 0
    // The enclosing DropArea owns the drag, including gaps and margins.
    // Nothing here takes a pointer grab or interrupts video playback.
    Behavior on opacity { NumberAnimation { duration: Native.animationsEnabled() ? 100 : 0 } }

    function choiceAt(x, y) {
        if (subtitle) return 1
        return (stacked ? y < height / 2 : x < width / 2) ? 1 : 2
    }

    Rectangle { anchors.fill: parent; color: "#DB080808" }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: options.top
        anchors.bottomMargin: root.stacked ? 16 : 24
        text: root.displayedSubtitle ? "Add subtitles" : root.displayedCount + (root.displayedCount === 1 ? " file" : " files")
        color: Colors.textDim
        font.family: Fonts.ui
        font.pixelSize: 13
    }
    Item {
        id: options
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 760)
        height: root.stacked ? (root.displayedSubtitle ? 108 : 228) : 200
        Repeater {
            model: root.displayedSubtitle ? 1 : 2
            delegate: Rectangle {
                id: zone
                required property int index
                readonly property bool selected: root.displayedChoice === index + 1
                width: root.stacked || root.displayedSubtitle ? options.width : (options.width - 16) / 2
                height: root.stacked ? 108 : options.height
                x: root.stacked ? 0 : index * (width + 16)
                y: root.stacked ? index * (height + 12) : 0
                radius: 12
                color: selected ? "#2A2A2A" : "#161616"
                border.width: 1
                border.color: selected ? Colors.accent : "#454545"
                Accessible.role: Accessible.StaticText
                Accessible.name: title.text + ". " + detail.text
                Behavior on color { ColorAnimation { duration: Native.animationsEnabled() ? 80 : 0 } }
                Column {
                    anchors.centerIn: parent
                    width: parent.width - 32
                    spacing: root.stacked ? 6 : 10
                    Text {
                        id: title
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: root.displayedSubtitle ? "Load subtitles" : zone.index === 0 ? "Play now" : "Add to playlist"
                        color: Colors.text
                        font.family: Fonts.ui
                        font.pixelSize: root.stacked ? 18 : 22
                        font.weight: Font.DemiBold
                    }
                    Text {
                        id: detail
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: root.displayedSubtitle ? "Use with the current video"
                            : zone.index === 0 ? "Add to playlist and start playing"
                            : "Add without changing playback"
                        color: "#BABABA"
                        font.family: Fonts.ui
                        font.pixelSize: 13
                    }
                }
            }
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: options.bottom
        anchors.topMargin: root.stacked ? 16 : 24
        text: root.displayedSubtitle ? "Release to load subtitles"
            : root.displayedChoice === 1 ? "Release to play now" : "Release to add to playlist"
        color: Colors.text
        font.family: Fonts.ui
        font.pixelSize: 12
    }
}
