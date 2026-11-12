import QtQuick

// Keeps the go-to-time panel (and its TextInput) out of the startup object
// graph; it is built the first time Ctrl+G is pressed and stays cached
// afterwards, so repeat opens are instant.
Item {
    id: root

    anchors.fill: parent

    property bool active: false
    // Nothing to hit-test while closed, so the video keeps its own clicks.
    visible: active
    enabled: active

    function openDialog() {
        root.active = true
        if (loader.item)
            loader.item.openDialog()
    }

    function closeDialog() {
        if (loader.item)
            loader.item.closeDialog()
        else
            root.active = false
    }

    Loader {
        id: loader
        anchors.fill: parent
        active: root.active
        asynchronous: false
        sourceComponent: Component { GoToDialog {} }

        onLoaded: {
            item.closed.connect(function() { root.active = false })
            item.openDialog()
        }
    }
}
