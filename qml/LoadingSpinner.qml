import QtQuick
import QtQuick.Window

// Turning ring for "opening" states. An image rather than a glyph (the UI
// font has none; a missing glyph makes Qt open every installed font), and
// an Animator so it keeps turning on the render thread while the GUI
// thread is busy with the open itself.
Item {
    id: root
    property bool running: visible
    width: 44
    height: 44
    Image {
        id: ring
        anchors.fill: parent
        source: "qrc:/assets/icons/loading.svg"
        sourceSize: Qt.size(root.width * root.Screen.devicePixelRatio,
                            root.height * root.Screen.devicePixelRatio)
        RotationAnimator on rotation {
            running: root.running && Native.animationsEnabled()
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
        }
    }
}
