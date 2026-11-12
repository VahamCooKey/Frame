import QtQuick
import QtQuick.Window

// Open from Playback only when chapterList is nonempty. Seeking uses the
// existing seek path and never changes pause or the PgUp/PgDn video bindings.
Rectangle {
    id: root
    objectName: "chapterPicker"
    property var returnFocusItem: null
    property bool canGoBack: true
    readonly property var chapters: Player.chapterList
    readonly property int currentChapter: {
        if (!visible) return -1
        let current = -1
        for (let i = 0; i < chapters.length; ++i) {
            const time = Number(chapters[i].time)
            if (isFinite(time) && time <= Player.position + 0.001) current = i
        }
        return current
    }
    signal interacted()
    signal closed()
    signal backRequested()
    // Compact menu spec (see ContextMenu.qml): 28 px rows, plain keys.
    width: 260
    height: Math.min(38 + Math.max(1, Math.min(9, chapters.length)) * 26,
                     Math.max(120, Window.height - 145))
    visible: false
    color: Colors.surface
    border.color: Colors.border
    radius: 8
    clip: true

    function timeLabel(seconds) { return KeyNames.clock(Math.round(Number(seconds) || 0)) }
    function open() {
        if (!chapters.length) return
        root.visible = true
        list.currentIndex = Math.max(0, currentChapter)
        list.forceActiveFocus(Qt.PopupFocusReason)
        list.positionViewAtIndex(list.currentIndex, ListView.Contain)
        interacted()
    }
    function close() {
        if (!visible) return
        visible = false
        if (returnFocusItem) returnFocusItem.forceActiveFocus(Qt.PopupFocusReason)
        closed()
    }
    function choose(index) {
        if (index < 0 || index >= chapters.length) return
        const seconds = Number(chapters[index].time)
        if (!isFinite(seconds) || seconds < 0) return
        close()
        Player.seek(seconds)
        interacted()
    }
    function back() {
        close()
        if (canGoBack) backRequested()
    }
    onChaptersChanged: if (visible && !chapters.length) close()
    onVisibleChanged: {
        Shortcuts.menuOpen = visible
        if (!visible) Shortcuts.wheelOverScroller = false
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
    HoverHandler {
        onHoveredChanged: {
            Shortcuts.wheelOverScroller = hovered && root.visible
            if (hovered) root.interacted()
        }
    }
    Rectangle {
        x: 6; y: 6; width: 24; height: 24; radius: 5
        visible: root.canGoBack
        color: backArea.containsMouse ? Colors.hover : "transparent"
        Image { anchors.centerIn: parent; width: 14; height: 14; sourceSize: Qt.size(28, 28); opacity: 0.85; source: "qrc:/assets/icons/chevron-left.svg" }
        MouseArea {
            id: backArea
            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            Accessible.role: Accessible.Button
            Accessible.name: "Back to Playback"
            Accessible.onPressAction: root.back()
            onClicked: root.back()
        }
    }
    Text {
        x: root.canGoBack ? 34 : 26; y: 9
        text: "Chapters"; color: Colors.text; font.family: Fonts.ui; font.pixelSize: 13; font.weight: Font.DemiBold
    }
    Text {
        objectName: "chapterMenuShortcut"
        property string sequence: KeyNames.forCommand("script-message app-chapters")
        anchors.right: parent.right; anchors.rightMargin: 14; y: 10
        text: sequence
        color: Colors.textDim; font.family: Fonts.ui; font.pixelSize: 12
    }
    ListView {
        id: list
        objectName: "chapterList"
        readonly property bool playerControl: true
        x: 4; y: 34; width: parent.width - 8; height: Math.max(0, parent.height - y - 4)
        clip: true
        model: root.chapters
        keyNavigationEnabled: true
        keyNavigationWraps: true
        Keys.onReturnPressed: root.choose(currentIndex)
        Keys.onEnterPressed: root.choose(currentIndex)
        Keys.onSpacePressed: root.choose(currentIndex)
        Keys.onEscapePressed: root.close()
        Keys.onLeftPressed: root.back()
        delegate: Rectangle {
            id: row
            required property int index
            required property var modelData
            readonly property bool isCurrent: index === root.currentChapter
            readonly property string label: String(modelData.title || "").trim() || "Chapter " + (index + 1)
            width: list.width; height: 26; radius: 5
            color: rowArea.containsMouse || (ListView.isCurrentItem && list.activeFocus) ? Colors.hover : "transparent"
            Accessible.role: Accessible.MenuItem
            Accessible.name: label + ", " + root.timeLabel(modelData.time) + (isCurrent ? ", current chapter" : "")
            Accessible.selected: isCurrent
            Accessible.onPressAction: root.choose(index)
            Image {
                x: 4; anchors.verticalCenter: parent.verticalCenter; width: 14; height: 14; sourceSize: Qt.size(28, 28)
                source: "qrc:/assets/icons/check.svg"; visible: row.isCurrent
            }
            Text {
                x: 22; width: parent.width - 90; anchors.verticalCenter: parent.verticalCenter
                text: row.label; elide: Text.ElideRight
                color: Colors.text; font.family: Fonts.ui; font.pixelSize: 13
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                text: root.timeLabel(row.modelData.time)
                color: Colors.textDim; font.family: Fonts.ui; font.pixelSize: 12; font.features: { "tnum": 1 }
            }
            MouseArea {
                id: rowArea
                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: root.choose(row.index)
            }
        }
    }
}
