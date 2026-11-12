import QtQuick

// F6b go-to-time. Deliberately QtQuick-only (no QtQuick.Controls): this is a
// text field and two labels, and the startup budget does not need another
// Controls style pulled in on first use.
//
// While it is open ShortcutManager.textInputActive suppresses key forwarding
// (A19) — otherwise typing a time also runs the bindings behind it, and "0"
// would seek to the start mid-edit.
Item {
    id: root

    anchors.fill: parent

    signal closed()

    readonly property color cPanel:   Colors.panel
    readonly property color cText:    Colors.text
    readonly property color cDim:     Colors.textDim
    readonly property color cAccent:  Colors.accent
    readonly property color cBad:     Colors.danger

    property bool invalid: false

    function clock(seconds) { return KeyNames.clock(seconds) }

    function openDialog() {
        root.invalid = false
        field.text = ""
        Shortcuts.textInputActive = true
        field.forceActiveFocus()
    }

    function closeDialog() {
        Shortcuts.textInputActive = false
        root.closed()
    }

    function commit() {
        const seconds = Player.parseTimeSpec(field.text)
        if (seconds < 0) {
            root.invalid = true
            return
        }
        // Past the end is a no-op rather than an error: mpv would clamp, but
        // saying so is friendlier than silently landing on the last frame.
        if (Player.duration > 0 && seconds > Player.duration) {
            root.invalid = true
            return
        }
        Player.seek(seconds)
        root.closeDialog()
    }

    // Swallow clicks outside the panel so they neither pause nor open the
    // context menu; clicking away closes, like the other overlays.
    // The shared modal dim, as behind Settings and the shortcut guide.
    Rectangle { anchors.fill: parent; color: Colors.scrim }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: root.closeDialog()
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(480, parent.width - 32)
        // Sized to its content (D7): title, field, one line of help.
        height: hint.y + hint.height + 16
        radius: 12
        color: root.cPanel
        border.color: Colors.border
        border.width: 1

        // Clicks inside must not reach the dismiss layer above.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: field.forceActiveFocus()
        }

        Text {
            id: title
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 18
            anchors.rightMargin: 48
            anchors.topMargin: 16
            text: "Go to time"
            color: root.cText
            font.pixelSize: 15
            font.weight: Font.DemiBold
            font.family: Fonts.ui
            Accessible.role: Accessible.Heading
        }

        Rectangle {
            id: box
            anchors.left: parent.left
            anchors.right: submit.left
            anchors.top: title.bottom
            anchors.leftMargin: 18
            anchors.rightMargin: 8
            anchors.topMargin: 10
            height: 32
            radius: 7
            color: Colors.control
            border.width: 1
            border.color: root.invalid ? root.cBad
                                       : (field.activeFocus ? Qt.alpha(root.cAccent, 0.6) : Colors.fieldBorder)

            TextInput {
                id: field
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                verticalAlignment: TextInput.AlignVCenter
                color: root.cText
                font.pixelSize: 13
                font.family: Fonts.ui
                selectByMouse: true
                selectionColor: root.cAccent
                selectedTextColor: Colors.textOnAccent
                clip: true

                // Typing again is the user disagreeing with the error.
                onTextChanged: root.invalid = false

                Keys.onReturnPressed: root.commit()
                Keys.onEnterPressed: root.commit()
                Keys.onEscapePressed: root.closeDialog()

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: field.text === "" && !field.activeFocus
                    text: "e.g. 90, 1:30, 01:30:00.250"
                    color: root.cDim
                    font: field.font
                }
            }
        }

        FlatButton {
            id: submit
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: box.verticalCenter
            height: 32
            width: Math.max(56, implicitWidth)
            primary: true
            enabled: field.text.trim() !== ""
            label: "Go"
            onActivated: root.commit()
        }

        // An example of what to type, or the error.
        Item {
            id: hint
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: box.bottom
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            anchors.topMargin: 10
            height: message.implicitHeight
            Text {
                id: message
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.invalid ? "Not a time in this file." : "e.g. 1:30 or 90" + (Player.duration > 0 ? " · Length " + root.clock(Player.duration) : "")
                color: root.invalid ? root.cBad : root.cDim
                font.pixelSize: 12
                font.family: Fonts.ui
                font.features: { "tnum": 1 }
                elide: Text.ElideRight
            }
        }

        IconButton {
            anchors.right: parent.right; anchors.rightMargin: 10
            y: 10
            width: 28; height: 28; iconSize: 14
            iconSource: "qrc:/assets/icons/close.svg"
            accessibleName: "Close"
            tooltipEnabled: true
            tooltipHints: [{ label: "Close", keys: "ESC" }]
            onActivated: root.closeDialog()
        }
    }
}
