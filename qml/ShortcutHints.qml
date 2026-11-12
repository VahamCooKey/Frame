import QtQuick

// A row of "label [key]" pairs for menu footers and tooltips. Each entry is
// { label, command } (resolved to the user's effective key),
// { label, keys } (a raw mpv key name) or { label, keys, command } (that
// key, while it still runs the command). An unbound key shows the label
// alone, or "label  Not set" when showUnset is true.
Flow {
    id: root
    property var hints: []
    property bool showUnset: false
    // Footers of key-only hints drop entries whose action has no key.
    property bool hideUnbound: false
    property color labelColor: Colors.textDim
    property int labelSize: 12
    property real maxWidth: 10000
    // Menus draw keys as plain text rather than chips (compact menu spec).
    property bool plainKeys: false
    property int measureRevision: 0
    readonly property real naturalWidth: {
        const revision = measureRevision
        let total = 0
        for (let i = 0; i < hintRepeater.count; ++i) {
            const item = hintRepeater.itemAt(i)
            if (item) total += item.implicitWidth + (i > 0 ? spacing : 0)
        }
        return total
    }
    width: Math.min(naturalWidth, maxWidth)
    // Plain-text form for accessibility and tests.
    readonly property string text: resolved.map(h => [h.label, h.key].filter(s => s).join(" ")).join("  ")
    readonly property var resolved: {
        const out = []
        const bound = Shortcuts.commandKeys // re-evaluate when bindings change
        for (const h of hints) {
            // { keys, command }: one specific alternate key, shown only while
            // it still runs that command (a remap releases it).
            const key = h.keys === undefined ? KeyNames.forCommand(h.command || "")
                : !h.command || Shortcuts.commandForKey(h.keys) === h.command ? KeyNames.humanize(h.keys) : ""
            if (key || (h.label && !root.hideUnbound)) out.push({ label: h.label || "", key: key })
        }
        return out
    }

    spacing: 14

    Repeater {
        id: hintRepeater
        model: root.resolved
        onItemAdded: root.measureRevision++
        onItemRemoved: root.measureRevision++
        delegate: Row {
            required property var modelData
            spacing: 6
            onImplicitWidthChanged: root.measureRevision++
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: text !== ""
                text: parent.modelData.label
                color: root.labelColor
                font.family: Fonts.ui
                font.pixelSize: root.labelSize
            }
            KeyCaps {
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.plainKeys
                sequence: root.plainKeys ? "" : parent.modelData.key
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.plainKeys && text !== ""
                text: parent.modelData.key
                color: Colors.textDim
                font.family: Fonts.ui
                font.pixelSize: root.labelSize
                font.weight: Font.Medium
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: parent.modelData.key === "" && root.showUnset
                text: "Not set"
                color: Colors.textDim
                font.family: Fonts.ui
                font.pixelSize: 12
                font.italic: true
            }
        }
    }
}
