import QtQuick
import QtQuick.Window

Rectangle {
    id: root

    height: Qt.platform.os === "windows" ? 32 : 40
    color: root.cBar

    // The Colors singleton can be briefly unresolved during engine startup;
    // keep local fallbacks so first-frame bindings never assign undefined.
    readonly property bool hasColors: typeof Colors !== "undefined" && Colors !== null
    // The same panel colour as the theater dock and control band.
    readonly property color cBar: hasColors ? Colors.panel : "#171717"
    readonly property color cText: hasColors ? Colors.text : "#F3F3F3"
    readonly property color cHover: hasColors ? Colors.hover : "#22FFFFFF"
    readonly property bool windowsCaption: Qt.platform.os === "windows"
    readonly property bool windowActive: Window.window !== null && Window.window.active

    readonly property bool maximized: Window.window !== null && Window.window.visibility === Window.Maximized
    // Frame's fullscreen is a borderless monitor-sized window, not a Qt
    // visibility state, so it is read from the window's own flag. In
    // fullscreen the middle caption button leaves fullscreen; it used to
    // offer Maximize, which maximized the still-fullscreen window.
    readonly property bool fullscreen: Window.window !== null && Window.window.isFullscreen === true
    readonly property bool hovered: barHover.hovered

    signal interaction()

    HoverHandler {
        id: barHover
        onHoveredChanged: if (hovered) root.interaction()
    }

    component CaptionButton: Rectangle {
        id: captionButton

        required property string buttonId

        width: 46
        height: root.height
        radius: 0
        // Resource icons do not depend on an OS-specific symbol font.
        readonly property string iconSource: buttonId === "minimize"
                    ? "qrc:/assets/icons/minimize.svg"
                    : buttonId === "maximize"
                      ? (root.fullscreen ? "qrc:/assets/icons/fullscreen-exit.svg"
                         : root.maximized ? "qrc:/assets/icons/restore.svg"
                                          : "qrc:/assets/icons/maximize.svg")
                      : "qrc:/assets/icons/close.svg"
        readonly property string accessibleName: buttonId === "minimize" ? "Minimize"
                        : buttonId === "maximize"
                          ? (root.fullscreen ? "Exit fullscreen" : root.maximized ? "Restore" : "Maximize")
                          : "Close"
        readonly property bool playerControl: true
        color: captionPointer.pressed ? (buttonId === "close" ? "#B50D1C" : "#353535")
             : captionPointer.containsMouse ? (buttonId === "close" ? "#C42B1C" : "#2D2D2D")
             : "transparent"
        activeFocusOnTab: true
        border.width: activeFocus ? 1 : 0
        border.color: root.cText
        Accessible.role: Accessible.Button
        Accessible.name: accessibleName
        Accessible.onPressAction: activate()
        Keys.onReturnPressed: activate()
        Keys.onEnterPressed: activate()
        Keys.onSpacePressed: activate()
        onActiveFocusChanged: if (activeFocus) root.interaction()

        Image {
            anchors.centerIn: parent
            width: 18; height: 18
            source: captionButton.iconSource
            opacity: root.windowActive || captionPointer.containsMouse ? 1 : 0.6
        }
        MouseArea {
            id: captionPointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.ArrowCursor
            onClicked: captionButton.activate()
            onPositionChanged: if (containsMouse) root.interaction()
            onContainsMouseChanged: if (containsMouse) root.interaction()
        }
        function activate() {
            root.interaction()
            if (captionButton.buttonId === "minimize") {
                Native.minimize()
            } else if (captionButton.buttonId === "maximize") {
                // The window's toggleMaximized() captures the pre-maximized
                // geometry first (F22); the Native call is the fallback.
                const win = Window.window
                if (root.fullscreen && win && typeof win.toggleFullscreen === "function")
                    win.toggleFullscreen()
                else if (win && typeof win.toggleMaximized === "function")
                    win.toggleMaximized()
                else
                    Native.toggleMaximize()
            } else {
                Qt.quit()
            }
        }
    }

    // Hover anywhere on the bar keeps the chrome clock alive (the OSD hide
    // timer); sits under the buttons so it only catches bare bar areas.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: root.interaction()
        onContainsMouseChanged: if (containsMouse) root.interaction()
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: root.captionButtonsLeading ? buttonsRow.width + 16 : 20
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, Math.min(implicitWidth, root.width - buttonsRow.width - 40))
        objectName: "videoTitle"
        text: Player.failedFile !== "" ? MediaNames.display(Player.failedFile, MediaNames.cleanupEnabled)
              : Player.currentFile === "" ? "Frame"
                                       : MediaNames.display(Player.currentFile, MediaNames.cleanupEnabled)
        elide: Text.ElideRight
        color: root.cText
        font.pixelSize: 13
        font.family: Fonts.ui
    }

    MouseArea {
        id: moveArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        onPressed: (mouse) => {
            // Dragging the caption hands the grab point (window coordinates)
            // to the window: captionDrag routes fullscreen AND maximized
            // restore into the system move loop (F22). dragFromFullscreen is
            // the older fullscreen-only seam, Native.startMove() the last
            // fallback.
            const win = Window.window
            const p = mapToItem(null, mouse.x, mouse.y)
            if (win && typeof win.captionDrag === "function")
                win.captionDrag(p.x, p.y)
            else if (win && typeof win.dragFromFullscreen === "function")
                win.dragFromFullscreen(p.x, p.y)
            else
                Native.startMove()
        }
        onDoubleClicked: {
            // Same F22 seam: the window's toggleMaximized() pre-captures
            // geometry before maximizing.
            const win = Window.window
            // In fullscreen the first press of the double-click already
            // restored the window (captionDrag). Maximizing on the second
            // left it windowed but monitor-sized.
            if (win && win.lastFullscreenExitMs !== undefined
                    && Date.now() - win.lastFullscreenExitMs < 800)
                return
            if (win && typeof win.toggleMaximized === "function")
                win.toggleMaximized()
            else
                Native.toggleMaximize()
        }
        onPositionChanged: root.interaction()
        onContainsMouseChanged: if (containsMouse) root.interaction()
    }

    // INativeWindow Rule 2: the buttons and the side they sit on are DATA
    // from the platform seam, never hardcoded here. Windows and most Linux
    // desktops put them trailing; macOS puts traffic lights on the leading
    // edge. Read once — the layout cannot change while the app runs.
    readonly property var captionButtons: Native.titleBarButtons()
    readonly property bool captionButtonsLeading: Native.titleBarButtonsLeading()

    Row {
        id: buttonsRow
        anchors.left: root.captionButtonsLeading ? parent.left : undefined
        anchors.right: root.captionButtonsLeading ? undefined : parent.right
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            model: root.captionButtons
            delegate: CaptionButton {
                required property var modelData
                buttonId: modelData.id
            }
        }
    }

}
