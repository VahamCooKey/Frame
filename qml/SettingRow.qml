import QtQuick

Item {
    id: root
    default property alias control: controlSlot.children
    property string label: ""
    property string description: ""
    // [{ label, command }] drawn as label + key chips.
    property var shortcutHints: []
    property bool divider: true
    // The preference this row edits, read from its control's preferenceKey,
    // so every row with a typed default offers "Reset" without declaring
    // anything. Rows whose control is a plain button have no key.
    property string settingKey: ""
    // Differs from the default right now. Re-read on every settings change
    // (the store has no per-key notify), which is cheap: one INI lookup.
    property bool modified: false
    // Shown in place of the description while the control is disabled.
    property string disabledReason: ""
    property Item controlItem: null
    readonly property bool controlEnabled: !controlItem || controlItem.enabled
    width: parent ? parent.width : 0
    readonly property bool stacked: width < 500 && controlSlot.width > 100
    // Flat rows on the page (no card inset): text starts at the content
    // edge and the control ends at it, like the group title above. About
    // 12 px of air above and below the text (compact pass, 2026-11-05).
    height: Math.max(52, (stacked ? textCol.implicitHeight + controlSlot.height + 10
                               : Math.max(textCol.implicitHeight, controlSlot.height)) + 24)

    function refreshModified() {
        modified = settingKey !== "" && Settings.defaultValue(settingKey) !== undefined
                   && !Settings.isDefault(settingKey)
    }
    function resetToDefault() {
        if (settingKey !== "") Settings.resetSetting(settingKey)
    }
    Component.onCompleted: {
        const items = controlSlot.children
        if (items.length > 0) controlItem = items[0]
        for (let i = 0; i < items.length && settingKey === ""; ++i)
            if (items[i].preferenceKey !== undefined) settingKey = items[i].preferenceKey
        refreshModified()
    }
    Connections {
        target: Settings
        function onSettingChanged(key) { if (key === root.settingKey) root.refreshModified() }
        function onSettingsReset() { root.refreshModified() }
    }

    Column {
        id: textCol
        x: 0
        y: root.stacked ? 12 : (root.height - implicitHeight) / 2
        width: root.stacked ? root.width : Math.max(80, root.width - controlSlot.width - 32)
        spacing: 3
        // Above the control, so the shortcut tooltip is never covered.
        z: 2
        // One disabled style: the whole row dims like its control.
        opacity: root.controlEnabled ? 1 : 0.4
        // Title, with the reset link beside it rather than on a line of
        // its own, so a changed setting does not grow its row.
        Item {
            width: parent.width
            height: titleText.height
            z: 5
            Text {
                id: titleText
                width: Math.min(implicitWidth, parent.width - (resetPill.visible ? resetPill.width + 10 : 0)
                                                - (keysGlyph.visible ? keysGlyph.width + 10 : 0))
                text: root.label
                color: Colors.text
                font.family: Fonts.ui
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            // Same quiet link the Shortcuts list uses for a rebound key: only
            // present while the value differs from the default, so it also
            // tells the user which settings they have changed.
            // The setting's shortcut keys, inline as caps beside the title
            // (at most two; hover or focus names each one).
            Item {
                id: keysGlyph
                x: titleText.width + 10
                anchors.verticalCenter: titleText.verticalCenter
                width: inlineKeys.width; height: 20
                visible: keyHints.resolved.length > 0
                activeFocusOnTab: visible
                Accessible.role: Accessible.StaticText
                Accessible.name: "Shortcuts: " + keyHints.text
                Row {
                    id: inlineKeys
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Repeater {
                        model: keyHints.resolved.slice(0, 2)
                        delegate: Row {
                            required property var modelData
                            required property int index
                            spacing: 6
                            Text {
                                visible: index > 0
                                anchors.verticalCenter: parent.verticalCenter
                                text: "/"
                                color: Colors.textDim
                                font.family: Fonts.ui; font.pixelSize: 12
                            }
                            KeyCaps { anchors.verticalCenter: parent.verticalCenter; sequence: modelData.key }
                        }
                    }
                }
                HoverHandler { id: keysHover }
                Rectangle {
                    visible: keysHover.hovered || keysGlyph.activeFocus
                    z: 20
                    x: parent.width + 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: keyHints.width + 20
                    height: keyHints.height + 12
                    radius: 6
                    color: Colors.surface
                    border.color: Colors.border
                    ShortcutHints {
                        id: keyHints
                        anchors.centerIn: parent
                        hints: root.shortcutHints
                        hideUnbound: true
                        labelSize: 12
                        labelColor: Colors.text
                        maxWidth: 420
                    }
                }
            }
            Rectangle {
                id: resetPill
                objectName: "settingReset"
                x: titleText.width + 10 + (keysGlyph.visible ? keysGlyph.width + 10 : 0)
                anchors.verticalCenter: titleText.verticalCenter
                visible: root.modified
                width: resetText.implicitWidth + 16
                height: 20
                radius: 10
                color: resetArea.containsMouse || activeFocus ? Colors.fieldHover : Colors.hover
                border.width: activeFocus ? 1 : 0
                border.color: Colors.accent
                activeFocusOnTab: visible
                Accessible.role: Accessible.Button
                Accessible.name: "Reset " + root.label + " to default"
                Accessible.onPressAction: root.resetToDefault()
                Keys.onReturnPressed: root.resetToDefault()
                Keys.onSpacePressed: root.resetToDefault()
                Text {
                    id: resetText
                    anchors.centerIn: parent
                    text: "Reset to default"
                    color: resetArea.containsMouse || parent.activeFocus ? Colors.text : Colors.textDim
                    font.family: Fonts.ui
                    font.pixelSize: 12
                }
                MouseArea {
                    id: resetArea
                    anchors.fill: parent
                    anchors.margins: -2
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.resetToDefault()
                }
            }
        }
        Text {
            visible: text !== ""
            width: parent.width
            text: !root.controlEnabled && root.disabledReason !== "" ? root.disabledReason : root.description
            color: Colors.textDim
            font.family: Fonts.ui
            font.pixelSize: 13
            lineHeight: 1.1
            wrapMode: Text.WordWrap
        }
    }
    Item {
        id: controlSlot
        x: root.stacked ? 0 : root.width - width
        y: root.stacked ? textCol.y + textCol.implicitHeight + 10 : (root.height - height) / 2
        width: childrenRect.width
        height: childrenRect.height
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Colors.divider
        visible: root.divider && root.parent && root.y + root.height < root.parent.height - 1
    }
}
