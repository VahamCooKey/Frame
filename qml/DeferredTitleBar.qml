import QtQuick

// TitleBar itself is small, but loading it eagerly also resolves its Window
// dependencies during cold startup.  Keep it in the same staged chrome pass
// as the OSD and playlist.
Item {
    id: root

    property bool active: false
    readonly property bool hovered: loader.item ? loader.item.hovered : false
    height: Qt.platform.os === "windows" ? 32 : 40

    signal interaction()

    Loader {
        id: loader
        anchors.fill: parent
        active: root.active
        asynchronous: false
        sourceComponent: Component { TitleBar {} }

        onLoaded: {
            console.log("startup: titlebar loaded")
            item.interaction.connect(function() { root.interaction() })
        }
    }
}
