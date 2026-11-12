import QtQuick

// Keeps the open-URL panel (and its TextInput) out of the startup object
// graph; it is built the first time Ctrl+U is pressed and stays cached
// afterwards, so repeat opens are instant.
Item {
    id: root

    anchors.fill: parent

    property bool active: false
    // Nothing to hit-test while closed, so the video keeps its own clicks.
    visible: active
    enabled: active

    // Re-emitted from the inner dialog: main.qml sees this wrapper, never the
    // component the Loader builds, so it can bind one handler that survives
    // the panel being destroyed and rebuilt.
    signal accepted(string target)

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
        sourceComponent: Component { OpenUrlDialog {} }

        onLoaded: {
            item.closed.connect(function() { root.active = false })
            item.accepted.connect(function(target) { root.accepted(target) })
            item.openDialog()
        }
    }
}
