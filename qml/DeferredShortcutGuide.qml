import QtQuick

// Keeps the shortcut guide out of the startup object graph. It is built only
// while open (first launch, F1 or the menu) and released when closed.
Item {
    id: root

    objectName: "shortcutGuideHost"
    anchors.fill: parent

    property bool active: false
    // Nothing to hit-test while closed, so the video keeps its own clicks.
    visible: active
    enabled: active

    // Emitted on every close; main.qml records that the guide was seen.
    signal dismissed()

    function openGuide() {
        root.active = true
        if (loader.item)
            loader.item.openGuide()
    }

    function closeGuide() {
        if (loader.item)
            loader.item.closeGuide()
        else
            root.active = false
    }

    Loader {
        id: loader
        anchors.fill: parent
        active: root.active
        asynchronous: false
        sourceComponent: Component { ShortcutGuide {} }

        onLoaded: {
            item.closed.connect(function() {
                root.active = false
                root.dismissed()
            })
            item.openGuide()
        }
    }
}
