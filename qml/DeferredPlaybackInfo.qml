import QtQuick

Item {
    id: root
    anchors.fill: parent
    property bool active: false
    visible: active
    enabled: active
    function openDialog() {
        active = true
        if (loader.item) loader.item.openDialog()
    }
    function closeDialog() {
        if (loader.item) loader.item.closeDialog()
        else active = false
    }
    Loader {
        id: loader
        anchors.fill: parent
        active: root.active
        sourceComponent: Component { PlaybackInfo {} }
        onLoaded: {
            item.closed.connect(function() { root.active = false })
            item.openDialog()
        }
    }
}
