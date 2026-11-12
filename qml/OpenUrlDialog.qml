import QtQuick

// F9 open-URL. QtQuick-only (no QtQuick.Controls) for the same reason as the
// go-to dialog: one text field and two labels do not justify pulling a
// Controls style into the startup budget.
//
// While it is open Shortcuts.textInputActive suppresses key forwarding (A19).
// That is also what lets Ctrl+V paste into the field instead of running F45
// and opening the clipboard behind the dialog.
Item {
    id: root

    anchors.fill: parent

    signal closed()
    // Handed to main.qml rather than opened here: openPaths() is the single
    // open path that also does the F16 folder enqueue and the recents entry.
    signal accepted(string target)

    readonly property color cPanel:   Colors.panel
    readonly property color cText:    Colors.text
    readonly property color cDim:     Colors.textDim
    readonly property color cAccent:  Colors.accent
    readonly property color cBad:     Colors.danger

    property bool invalid: false

    function openDialog() {
        root.invalid = false
        // Prefill only when the clipboard actually holds something openable,
        // and with the original text rather than the resolved target: the user
        // recognises what they copied, and commit() resolves it again anyway.
        const clip = Native.clipboardText()
        field.text = Player.resolveOpenTarget(clip) === "" ? "" : clip
        Shortcuts.textInputActive = true
        field.forceActiveFocus()
        // Selected, not merely present, so Enter is the whole interaction and
        // the first keystroke still replaces it. After the focus change, not
        // before it, so nothing in taking focus clears the selection.
        if (field.text !== "")
            field.selectAll()
    }

    function closeDialog() {
        Shortcuts.textInputActive = false
        root.closed()
    }

    function commit() {
        // F45's classifier is the only validator here. A second check in QML
        // would disagree with the one drops, argv and Ctrl+V already use, and
        // it would have to re-learn the traps: a Windows drive letter parses
        // as a URL with a one-character scheme, and a URL counts only when it
        // has a host. It is also why pasting a local path into this dialog
        // just works.
        const target = Player.resolveOpenTarget(field.text)
        if (target === "") {
            root.invalid = true
            return
        }
        root.accepted(target)
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
        // Wider than the go-to panel because a URL is long: at 340 a plain
        // YouTube link is already scrolled out of sight on open.
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
            text: "Open URL"
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

            // TextInput, not TextEdit: a long link scrolls horizontally under
            // the cursor instead of wrapping the panel into three lines.
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
                    text: "https://... or a file path"
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
            width: Math.max(64, implicitWidth)
            primary: true
            enabled: field.text.trim() !== ""
            label: "Open"
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
                text: root.invalid ? "Not a media link or file." : "A web link or a file path"
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
