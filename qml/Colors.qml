pragma Singleton
import QtQuick

QtObject {
    readonly property color bg: "#080808"
    // Every floating panel (playlist drawer, settings, track menu, go-to/open-
    // URL dialogs, OSD gradient tail) used to carry its own near-duplicate of
    // this at a slightly different hue and alpha (90-95%, #121219/#1A1A22/
    // #1C1C22/#080808) -- drift, not a style. One token, one look.
    // Popovers over the picture (menus, pickers, notices). Opaque, like
    // every panel: translucency let bright video show through as shapes.
    readonly property color surface: "#1A1A1A"
    readonly property color raised: "#242424"
    // Empty thumbnail / loading tile.
    readonly property color tile: "#1E1E1E"
    // The border on every panel, input box and flat button converged on this
    // same value independently; naming it stops the next one from drifting.
    readonly property color border: "#24FFFFFF"
    readonly property color text: "#F3F3F3"
    readonly property color textDim: "#A3A3A3"
    // Interface pane accent-colour override (settings/interface/accentColor).
    // Not readonly: main.qml sets it once from Settings at startup, and the
    // Interface pane reassigns it live on change so every one of the ~40
    // chrome bindings that reference Colors.accent updates immediately
    // without a restart.
    property color accent: "#E0E0E0"
    readonly property color hover: "#18FFFFFF"
    // Popover menus (2026-11-05 rework): a visible 1 px edge so the panel
    // lifts off dark video, and a dimmed tint for row icons so labels lead.
    // Opaque: the same grey the translucent value gave over `surface`, so it
    // stays visible when the panel sits over bright video (playlist drawer).
    readonly property color menuBorder: "#393939"
    readonly property color menuIcon: "#B5B5B5"
    readonly property color danger: "#F0A8A8"
    readonly property color warning: "#E8CE9B"
    readonly property color textOnAccent: "#161616"

    // Settings workspace and its controls (2026-11-05). Opaque on purpose:
    // these sit on the scrim, and a translucent fill would let the video
    // tint every row differently.
    // One dim behind every modal (settings, guide, go to, open URL).
    readonly property color scrim: "#80000000"
    // Non-modal overlay panel over the picture (playback info).
    readonly property color overlay: surface
    // Primary button: light fill, dark text (one per dialog).
    readonly property color primary: "#ECECEC"
    readonly property color primaryHover: "#FFFFFF"
    readonly property color panel: "#171717"
    readonly property color sidebar: "#111111"
    // Hairline between rows and around the sidebar.
    readonly property color divider: "#2A2A2A"
    readonly property color scrollThumb: "#5C5C5C"
    // Segmented choice: a recessed well with a lifted selected segment.
    readonly property color control: "#0F0F0F"
    readonly property color controlSelected: "#383838"
    // Buttons, dropdowns and text fields.
    readonly property color field: "#262626"
    readonly property color fieldHover: "#313131"
    readonly property color fieldBorder: "#3A3A3A"
    readonly property color fieldFocus: "#8A8A8A"
    // Popup lists opened from a field (dropdown choices).
    readonly property color popup: "#2A2A2A"
    readonly property color popupHover: "#3A3A3A"
    // Switch and slider tracks when off / unfilled. Dark enough that the
    // accent-filled "on" state reads at a glance.
    readonly property color trackOff: "#333333"
    readonly property color trackOffBorder: "#4A4A4A"
    readonly property color knobOff: "#9C9C9C"
    readonly property color knobOn: "#141414"
    // Accent swatches, in picker order. Mono stays the default.
    readonly property var accents: [
        { label: "Mono", name: "Monochrome", color: "#E0E0E0" },
        { label: "Mint", name: "Mint", color: "#B8DDCF" },
        { label: "Blue", name: "Blue", color: "#7DB2EE" },
        { label: "Sand", name: "Sand", color: "#E8CE9B" },
        { label: "Rose", name: "Rose", color: "#E8BBC5" }
    ]

    // Upgrade the previous saturated palette, including a saved violet
    // default. Every panel background remains neutral regardless of accent.
    // The 2026-11-05 Blue is clearer than the old pale ice blue, which read
    // as grey on switches; a saved ice blue moves to it.
    function restoredAccent(value) {
        const old = { "#7B6CFF": "#E0E0E0", "#4E9EE0": "#7DB2EE",
                      "#B9D5EB": "#7DB2EE",
                      "#4EC98A": "#B8DDCF", "#E06A9E": "#E8BBC5",
                      "#E0A24E": "#E8CE9B" }
        const key = String(value).toUpperCase()
        return old[key] || (key !== "" ? key : "#E0E0E0")
    }
}
