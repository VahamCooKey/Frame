import QtQuick

// The essential shortcuts on one screen. Shown on first launch and on F1.
// Keys stay live underneath, so a shortcut can be tried while reading it.
// Every key is resolved through the user's bindings, so a remap shows here.
Item {
    id: root
    objectName: "shortcutGuide"

    signal closed()

    // Each row lists commands; an entry with "keys" names one specific
    // alternate key, shown only while that key still runs the command.
    readonly property var groups: [
        { title: "Playback", rows: [
            { label: "Play/pause", parts: [{ command: "cycle pause" }, { keys: "k", command: "cycle pause" }] },
            { label: "Back/forward 5 s", parts: [{ command: "seek -5" }, { command: "seek 5" }] },
            // The bar's seek buttons step 10 s; list their keys too (D14).
            { label: "Back/forward 10 s", parts: [{ command: "seek -10" }, { command: "seek 10" }] },
            { label: "Slower/faster", parts: [{ command: "script-message app-shuttle-down" }, { command: "script-message app-shuttle-up" }] },
            { label: "Normal speed", parts: [{ command: "set speed 1.0" }] },
            { label: "Frame by frame", parts: [{ command: "frame-back-step" }, { command: "frame-step" }] },
            { label: "Go to time", parts: [{ command: "script-message app-goto-time" }] }
        ] },
        { title: "Audio and subtitles", rows: [
            { label: "Volume", parts: [{ command: "add volume 5" }, { command: "add volume -5" }] },
            { label: "Mute", parts: [{ command: "cycle mute" }] },
            { label: "Audio track", parts: [{ command: "script-message app-audio-menu" }] },
            { label: "Subtitles", parts: [{ command: "script-message app-subtitle-menu" }] },
            { label: "Subtitles on/off", parts: [{ command: "cycle sub-visibility" }] },
            { label: "Subtitles earlier/later", parts: [{ command: "add sub-delay -0.1" }, { command: "add sub-delay 0.1" }] },
            { label: "Load subtitle file", parts: [{ command: "script-message app-load-subtitles" }] }
        ] },
        { title: "View and playlist", rows: [
            { label: "Show playlist", parts: [{ command: "script-message app-toggle-playlist" }] },
            { label: "Previous/next video", parts: [{ command: "playlist-prev" }, { command: "playlist-next" }] },
            { label: "Fullscreen", parts: [{ command: "script-message app-toggle-fullscreen" }] },
            { label: "Theater layout", parts: [{ command: "script-message app-toggle-theater" }] },
            { label: "Mini player", parts: [{ command: "script-message app-mini-player" }] },
            { label: "Screenshot", parts: [{ command: "screenshot" }] },
            { label: "Quick settings", parts: [{ command: "script-message app-playback-menu" }] },
            { label: "This guide", parts: [{ command: "script-message app-shortcut-guide" }] }
        ] }
    ]

    readonly property int columnWidth: 268
    readonly property int columnGap: 36
    readonly property int columnCount: width >= 3 * columnWidth + 2 * columnGap + 120 ? 3
                                     : width >= 2 * columnWidth + columnGap + 96 ? 2 : 1
    // Group indexes per column, balanced by row count.
    // Three groups of seven or eight rows, so the columns end together.
    readonly property var layout: columnCount === 3 ? [[0], [1], [2]]
                                : columnCount === 2 ? [[0, 1], [2]] : [[0, 1, 2]]

    // Humanized key for one part, or "" when it no longer runs the command.
    function keyFor(part) {
        const keys = Shortcuts.commandKeys // re-evaluate when bindings change
        if (part.keys !== undefined)
            return Shortcuts.commandForKey(part.keys) === part.command ? KeyNames.humanize(part.keys) : ""
        return KeyNames.humanize(keys[part.command] || "")
    }

    function openGuide() {
        panel.forceActiveFocus()
    }
    function closeGuide() { root.closed() }

    opacity: 0
    Component.onCompleted: opacity = 1
    Behavior on opacity { NumberAnimation { duration: Native.animationsEnabled() ? 140 : 0; easing.type: Easing.OutQuad } }

    Rectangle {
        anchors.fill: parent
        color: Colors.scrim
    }
    // Clicking anywhere outside the panel closes it.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: root.closeGuide()
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(root.width - 32, body.width + 56)
        height: Math.min(root.height - 32, header.height + scroller.contentHeight + footer.height)
        radius: 12
        color: Colors.panel
        border.color: Colors.border
        Accessible.role: Accessible.Dialog
        Accessible.name: "Keyboard shortcuts"
        Keys.onReturnPressed: root.closeGuide()
        Keys.onEnterPressed: root.closeGuide()

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
        }

        Item {
            id: header
            width: parent.width
            height: 68
            IconButton {
                anchors.right: parent.right; anchors.rightMargin: 14
                y: 16
                width: 28; height: 28; iconSize: 14
                iconSource: "qrc:/assets/icons/close.svg"
                accessibleName: "Close shortcut guide"
                tooltipEnabled: true
                tooltipHints: [{ label: "Close", keys: "ESC" }]
                onActivated: root.closeGuide()
            }
            Text {
                id: heading
                x: 28; y: 22
                text: "Keyboard shortcuts"
                color: Colors.text
                font.family: Fonts.ui
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                anchors.left: heading.left
                anchors.top: heading.bottom; anchors.topMargin: 4
                text: "The essentials. Every shortcut can be changed in Settings."
                color: Colors.textDim
                font.family: Fonts.ui
                font.pixelSize: 12
            }
        }

        Flickable {
            id: scroller
            anchors.top: header.bottom
            anchors.bottom: footer.top
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: 28; anchors.rightMargin: 28
            contentWidth: body.width
            // 20 px below the grid, matching the space around it.
            contentHeight: body.height + 24
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height

            Row {
                id: body
                y: 4
                spacing: root.columnGap
                Repeater {
                    model: root.layout
                    delegate: Column {
                        id: column
                        required property var modelData
                        width: root.columnWidth
                        spacing: 18
                        Repeater {
                            model: column.modelData
                            delegate: Column {
                                id: group
                                required property int modelData
                                readonly property var spec: root.groups[modelData]
                                width: root.columnWidth
                                spacing: 2
                                Text {
                                    text: group.spec.title
                                    color: Colors.textDim
                                    font.family: Fonts.ui
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    bottomPadding: 4
                                }
                                Repeater {
                                    model: group.spec.rows
                                    delegate: Item {
                                        id: row
                                        required property var modelData
                                        readonly property var keys: modelData.parts.map(p => root.keyFor(p)).filter(k => k !== "")
                                        width: root.columnWidth
                                        height: 28
                                        visible: keys.length > 0
                                        Text {
                                            anchors.left: parent.left
                                            anchors.right: keyRow.left; anchors.rightMargin: 10
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: row.modelData.label
                                            color: Colors.text
                                            font.family: Fonts.ui
                                            font.pixelSize: 13
                                            elide: Text.ElideRight
                                        }
                                        Row {
                                            id: keyRow
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 6
                                            Repeater {
                                                model: row.keys
                                                delegate: Row {
                                                    required property string modelData
                                                    required property int index
                                                    spacing: 6
                                                    // Alternatives: a dim slash between them.
                                                    Text {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        visible: index > 0
                                                        text: "/"
                                                        color: Colors.textDim
                                                        font.family: Fonts.ui
                                                        font.pixelSize: 12
                                                    }
                                                    KeyCaps {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        sequence: modelData
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // No footer: the × closes it (as do Esc, Enter and F1), so a
        // "Got it" bar would only repeat that.
        Item { id: footer; anchors.bottom: parent.bottom; width: parent.width; height: 0 }
    }
}
