// Minimal repro: Menu populated via createObject/addItem at popup time (app pattern).
import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: win
    width: 400
    height: 300
    visible: true

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                console.log("RIGHT CLICK at", mouse.x, mouse.y)
                win.ctx.openAt(mouse.x, mouse.y)
            }
        }
    }

    Component {
        id: leafComp
        MenuItem { font.pixelSize: 12 }
    }

    Menu {
        id: ctx
        width: 200
        property var items: []
        function openAt(x, y) {
            while (ctx.count > 0)
                ctx.removeItem(ctx.itemAt(ctx.count - 1))
            for (var i = 0; i < ctx.items.length; i++) {
                const it = leafComp.createObject(ctx, { text: "Item " + i })
                it.onTriggered.connect(function() { console.log("CLICKED item " + i) })
                ctx.addItem(it)
            }
            console.log("popup at", x, y, "items=", ctx.count)
            ctx.popup(x, y)
        }
    }

    Timer {
        interval: 500
        running: true
        onTriggered: {
            console.log("TRYING right-click path")
            // simulate: openAt directly
            ctx.items = ["A", "B", "C"]
            ctx.openAt(200, 150)
        }
    }
}
