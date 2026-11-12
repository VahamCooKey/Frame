import QtQuick

// The context menu constructs a Controls menu hierarchy.  It is not needed
// until the first right-click, so keep its module import off the initial
// object creation path.
Item {
    id: root

    property bool active: false
    property var model: []
    // command -> current on/off state, for the check marks on toggles.
    property var toggleStates: ({})
    // Root-row label -> inline value ("Speed": "Normal"), and the aspect
    // ratio names for the Aspect ratio submenu.
    property var valueHints: ({})
    property var aspectLabels: []
    property string miniCorner: ""
    property bool hasPendingOpen: false
    property real pendingX: 0
    property real pendingY: 0

    signal opened()
    signal closed()

    function openAt(x, y) {
        if (loader.item) {
            loader.item.model = model
            loader.item.openAt(x, y)
            return
        }
        hasPendingOpen = true
        pendingX = x
        pendingY = y
        active = true
    }

    // Programmatic dismiss (F27: a non-navigation shortcut pressed while the
    // menu is open closes it before the key forwards). No-op when closed.
    function close() {
        hasPendingOpen = false
        if (loader.item)
            loader.item.close()
    }

    onModelChanged: if (loader.item) loader.item.model = model

    Loader {
        id: loader
        active: root.active
        asynchronous: true
        sourceComponent: Component { ContextMenu {
            toggleStates: root.toggleStates
            valueHints: root.valueHints
            aspectLabels: root.aspectLabels
            miniCorner: root.miniCorner
        } }

        onLoaded: {
            item.model = root.model
            if (root.hasPendingOpen) {
                root.hasPendingOpen = false
                item.openAt(root.pendingX, root.pendingY)
            }
        }
    }

    Connections {
        target: loader.item

        function onOpened() { root.opened() }
        function onClosed() { root.closed() }
    }
}
