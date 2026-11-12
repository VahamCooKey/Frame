import QtQuick
import QtQuick.Effects
import QtQuick.Window

// One deferred, keyboard-accessible picker for tracks and quick settings.
// Shared popover shell (see rework-spec.md): 5 px padding, 28 px rows, 16 px
// icon / 14 px check column at x 8, labels at x 33, 12 px dim values and
// shortcuts, 32x18 toggles; the right edge of chevron, toggle and shortcut
// is the row's right padding (8 px). Width fits the content (230-320 px).
Rectangle {
    id: root
    objectName: "trackPicker"
    property var entries: []
    property var setter: null
    property var labeler: null
    property string emptyLabel: ""
    property string menuType: ""
    property bool autoClosed: false
    // A submenu opened from quick settings shows a back arrow (and Left
    // goes back) so the user is never stranded one level down.
    property bool canGoBack: false
    // Live on/off state for switch rows, keyed by entry id (set by the OSD).
    property var toggleStates: ({})
    readonly property bool isSubtitleMenu: menuType === "sub"
    readonly property bool hasChecks: entries.some(e => e.selected !== undefined)
    readonly property string heading: isSubtitleMenu ? "Subtitles" : menuType === "audio" ? "Audio track"
        : menuType === "speed" ? "Playback speed" : menuType === "aspect" ? "Aspect ratio" : "Quick settings"
    // The subtitle picker ends with an ordinary "Load subtitle file..." row.
    readonly property var addEntry: ({ id: "__add", label: "Load subtitle file…", icon: "folder", group: "add", add: true })
    readonly property var rows: isSubtitleMenu ? entries.concat([addEntry]) : entries
    readonly property int pad: 5
    readonly property int rowHeight: 28
    readonly property int headerHeight: 28
    readonly property int rightPad: 8
    // The keyboard pill stays hidden until the first navigation key.
    property bool navActive: false
    signal interacted()
    signal autoDismissed()
    signal addSubtitleRequested()
    signal backRequested()

    function rowLabel(entry, index) {
        return entry.add ? entry.label : (labeler ? labeler(entry, index + 1) : "")
    }

    FontMetrics { id: bodyMetrics; font.family: Fonts.ui; font.pixelSize: 13 }
    FontMetrics { id: smallMetrics; font.family: Fonts.ui; font.pixelSize: 12 }
    function rowWidth(entry, index) {
        let w = 33 + bodyMetrics.advanceWidth(rowLabel(entry, index)) + 24 + rightPad
        if (entry.detail) w += smallMetrics.advanceWidth(entry.detail)
        if (entry.toggle) w += 32
        if (entry.submenu) w += 5 + (entry.detail ? 6 : 0)
        if (entry.opens) w += smallMetrics.advanceWidth(KeyNames.forCommand(entry.command || ""))
        return w
    }
    readonly property real naturalWidth: {
        let w = (canGoBack ? 36 : 13) + bodyMetrics.advanceWidth(heading) + 24
        for (let i = 0; i < rows.length; ++i)
            w = Math.max(w, rowWidth(rows[i], i))
        if (emptyLabel && entries.length === 0) w = Math.max(w, bodyMetrics.advanceWidth(emptyLabel) + 40)
        return w + pad * 2
    }
    width: Math.max(230, Math.min(320, Math.ceil(naturalWidth)))
    // Entries may carry a `group`; each new group after the first gets a
    // 9 px divider (ListView sections below).
    readonly property int groupDividers: {
        let n = 0, last = undefined
        for (let i = 0; i < rows.length; ++i) {
            const g = rows[i].group || ""
            if (i > 0 && g !== last) ++n
            last = g
        }
        return n
    }
    // Keep the menu reachable even in the smallest player window.
    height: Math.min(pad * 2 + headerHeight + Math.max(rowHeight, Math.min(10, rows.length) * rowHeight)
                     + groupDividers * 9,
                     Math.max(120, Window.height - 145))
    visible: false
    color: Colors.surface
    border.color: Colors.menuBorder
    border.width: 1
    radius: 8
    layer.enabled: true
    // Room around the panel so the blurred shadow is not cut at its bounds.
    layer.sourceRect: Qt.rect(-32, -32, width + 64, height + 72)
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowBlur: 1.0
        shadowVerticalOffset: 8
        shadowOpacity: 0.55
        shadowColor: "black"
    }

    function configure(newEntries, newEmptyLabel, newSetter, newLabeler, newMenuType, newCanGoBack) {
        canGoBack = newCanGoBack === true
        entries = newEntries
        emptyLabel = newEmptyLabel
        setter = newSetter
        labeler = newLabeler
        menuType = newMenuType || ""
        autoClosed = false
        navActive = false
        visible = true
        list.currentIndex = 0
        for (let i = 0; i < entries.length; ++i)
            if (entries[i].selected) list.currentIndex = i
        list.forceActiveFocus()
        list.positionViewAtIndex(list.currentIndex, ListView.Contain)
    }
    function close() { visible = false }
    function choose(index) {
        if (index < 0 || index >= rows.length) return
        const entry = rows[index]
        if (entry.add) {
            close()
            addSubtitleRequested()
            interacted()
            return
        }
        const action = setter
        const id = entry.id
        // A switch flips in place; anything else closes BEFORE invoking,
        // because settings actions may open another picker.
        if (!entry.toggle)
            close()
        if (action) action(id)
        interacted()
    }
    function goBack() {
        if (!canGoBack) return
        close()
        backRequested()
    }
    onVisibleChanged: {
        Shortcuts.menuOpen = visible
        if (!visible) Shortcuts.wheelOverScroller = false
    }
    // Explicitly opened menus stay open until a choice, Escape or click away.
    HoverHandler {
        onHoveredChanged: {
            Shortcuts.wheelOverScroller = hovered && root.visible
            if (hovered) root.interacted()
        }
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    Rectangle {
        id: backButton
        visible: root.canGoBack
        x: root.pad + 3; y: root.pad + 2
        width: 24; height: 24; radius: 5
        color: backArea.containsMouse ? Colors.hover : "transparent"
        Image {
            anchors.centerIn: parent
            width: 14; height: 14
            source: "qrc:/assets/icons/chevron-left.svg"
            sourceSize: Qt.size(14 * Screen.devicePixelRatio, 14 * Screen.devicePixelRatio)
            opacity: 0.85
        }
        MouseArea {
            id: backArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            Accessible.role: Accessible.Button
            Accessible.name: "Back to quick settings"
            Accessible.onPressAction: root.goBack()
            onClicked: root.goBack()
        }
    }
    Text {
        x: root.canGoBack ? root.pad + 31 : root.pad + 8
        y: root.pad + (root.headerHeight - height) / 2
        text: root.heading
        color: Colors.text
        font.family: Fonts.ui
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }
    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: root.pad
        anchors.topMargin: root.pad + root.headerHeight
        clip: true
        model: root.rows
        keyNavigationEnabled: true
        keyNavigationWraps: true
        boundsBehavior: Flickable.StopAtBounds
        // First navigation key only reveals the pill on the selected (or
        // first) row; later keys move normally.
        Keys.onPressed: event => {
            if (root.navActive) return
            if (event.key === Qt.Key_Up || event.key === Qt.Key_Down || event.key === Qt.Key_Home
                || event.key === Qt.Key_End || event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                root.navActive = true
                let idx = 0
                for (let i = 0; i < root.entries.length; ++i)
                    if (root.entries[i].selected) idx = i
                currentIndex = idx
                positionViewAtIndex(idx, ListView.Contain)
                event.accepted = true
            }
        }
        function activateCurrent() {
            if (root.navActive || (currentItem && currentItem.hovered)) root.choose(currentIndex)
        }
        Keys.onReturnPressed: activateCurrent()
        Keys.onEnterPressed: activateCurrent()
        Keys.onLeftPressed: root.goBack()
        Keys.onRightPressed: if (currentIndex >= 0 && root.rows[currentIndex].submenu) root.choose(currentIndex)
        section.property: "group"
        section.delegate: Item {
            required property string section
            width: list.width
            // The first group needs no divider above it.
            height: section !== "" ? 9 : 0
            Rectangle {
                visible: parent.height > 0
                x: 6; width: parent.width - 12; height: 1
                anchors.verticalCenter: parent.verticalCenter
                color: Colors.divider
            }
        }
        delegate: Rectangle {
            id: row
            required property int index
            required property var modelData
            width: list.width
            height: root.rowHeight
            radius: 5
            readonly property bool hovered: rowArea.containsMouse
            color: rowArea.containsMouse || (ListView.isCurrentItem && list.activeFocus && root.navActive)
                   ? Colors.hover : "transparent"
            // Check (pickers) or icon (quick settings, load file) in the
            // leading column; labels start at x 33 either way.
            Image {
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 14; height: 14
                visible: row.modelData.selected === true
                source: "qrc:/assets/icons/check.svg"
                sourceSize: Qt.size(14 * Screen.devicePixelRatio, 14 * Screen.devicePixelRatio)
            }
            Image {
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 16; height: 16
                visible: !!row.modelData.icon
                source: row.modelData.icon ? "qrc:/assets/icons/" + row.modelData.icon + ".svg" : ""
                sourceSize: Qt.size(16 * Screen.devicePixelRatio, 16 * Screen.devicePixelRatio)
                layer.enabled: visible
                layer.effect: MultiEffect { colorization: 1.0; colorizationColor: Colors.menuIcon }
            }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 33
                anchors.right: detail.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: root.rowLabel(row.modelData, row.index)
                color: Colors.text
                font.family: Fonts.ui
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            Row {
                id: detail
                readonly property string text: row.modelData.detail || ""
                anchors.right: parent.right
                anchors.rightMargin: root.rightPad
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: text !== ""
                    text: row.modelData.detail || ""
                    color: Colors.textDim
                    font.family: Fonts.ui
                    font.pixelSize: 12
                }
                Rectangle {
                    id: toggle
                    readonly property bool on: root.toggleStates[row.modelData.id] === true
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.modelData.toggle === true
                    width: 32; height: 18; radius: 9
                    color: on ? Colors.accent : Colors.trackOff
                    border.color: on ? Colors.accent : Colors.trackOffBorder
                    Behavior on color { ColorAnimation { duration: Native.animationsEnabled() ? 120 : 0 } }
                    Rectangle {
                        width: 14; height: 14; radius: 7
                        y: 2
                        x: toggle.on ? toggle.width - width - 2 : 2
                        color: toggle.on ? Colors.knobOn : Colors.knobOff
                        Behavior on x { NumberAnimation { duration: Native.animationsEnabled() ? 120 : 0 } }
                    }
                }
                // Rows that open a panel or dialog show their key as dim text.
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.modelData.opens === true && text !== ""
                    text: row.modelData.opens ? KeyNames.forCommand(row.modelData.command || "") : ""
                    color: Colors.textDim
                    font.family: Fonts.ui
                    font.pixelSize: 12
                }
                // The glyph's ink spans 9..18 of its 24 grid, so the 5 px
                // wrapper is the ink: its right edge is the row's right padding.
                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.modelData.submenu === true
                    width: 5; height: 14
                    Image {
                        x: -5; width: 14; height: 14
                        source: "qrc:/assets/icons/chevron-right.svg"
                        sourceSize: Qt.size(14 * Screen.devicePixelRatio, 14 * Screen.devicePixelRatio)
                        opacity: 0.85
                    }
                }
            }
            MouseArea {
                id: rowArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                Accessible.role: row.modelData.toggle ? Accessible.CheckBox : Accessible.MenuItem
                Accessible.checkable: row.modelData.toggle === true
                Accessible.checked: root.toggleStates[row.modelData.id] === true
                Accessible.name: root.rowLabel(row.modelData, row.index)
                Accessible.description: detail.text
                Accessible.onPressAction: root.choose(row.index)
                onClicked: root.choose(row.index)
                onPositionChanged: list.currentIndex = row.index
            }
        }
    }
    Text {
        anchors.centerIn: list
        visible: root.entries.length === 0 && root.emptyLabel !== ""
        text: root.emptyLabel
        color: Colors.textDim
        font.family: Fonts.ui
        font.pixelSize: 13
    }
}
