import QtQuick

// Proxy for the playlist drawer while its Controls/ListView implementation is
// loaded after the video surface is available.
Item {
    id: root

    property bool active: false
    property bool shown: false
    property bool sidebar: false
    readonly property bool interacting: loader.item ? loader.item.interacting : false
    // Set on first load and never cleared: the drawer stays instantiated so
    // auto-hide only plays the slide-out (re-creating the ListView per hide
    // re-marshalled the whole playlist each time).
    property bool everLoaded: false
    property bool preloadRequested: false
    readonly property bool ready: loader.status === Loader.Ready
    function preload() { preloadRequested = true }

    signal interaction()
    signal showHudRequested(string text)

    onShownChanged: if (loader.item && loader.item.shown !== shown) loader.item.shown = shown

    Loader {
        id: loader
        anchors.fill: parent
        // A hidden playlist has no startup value, while ListView cold-loads
        // the model-view module. Instantiate it only on the first open.
        active: root.active && (root.shown || root.everLoaded || root.preloadRequested)
        asynchronous: !root.shown
        sourceComponent: Component { PlaylistDrawer { sidebar: root.sidebar } }

        onLoaded: {
            console.log("startup: playlist loaded")
            root.everLoaded = true
            item.shown = root.shown
            item.interaction.connect(function() { root.interaction() })
            item.showHudRequested.connect(function(text) { root.showHudRequested(text) })
        }
    }

    Connections {
        target: loader.item

        function onShownChanged() {
            if (root.shown !== target.shown)
                root.shown = target.shown
        }
    }
}
