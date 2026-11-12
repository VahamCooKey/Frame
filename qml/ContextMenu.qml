import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Effects

// Right-click menu, round-12 rework (2026-11-05,
// build-audit/ui-audit/rework-spec.md). The root lists direct actions with
// 16 px icons instead of seven bare folders; submenus hold the rest.
// Shared popover shell: Colors.surface panel, 1 px Colors.menuBorder edge,
// radius 8, soft drop shadow, 5 px padding, 28 px rows. Every menu at every
// level shares one text column: a 16 px icon column at x 8, labels at x 33
// (submenus leave the column blank, or put a check in it). Rows that open
// a submenu show the current value before the chevron; rows that fire an
// action show their shortcut; a root toggle that is on shows a check in
// place of its shortcut. No footer rows.
Menu {
    id: root

    property var model: []
    readonly property int minWidth: 230
    readonly property int maxWidth: 320
    // Inline values longer than this end in an ellipsis.
    readonly property int maxValueChars: 14
    // Visible width of the 14 px chevron (ink x 9..18 of 24).
    readonly property real chevronInk: 14 * 9 / 24

    // F11: command -> user key, refreshed on every open. The hint column has
    // to show the key that actually fires, not the input.conf default, or a
    // rebind looks like it did nothing.
    property var keyOverrides: ({})
    // command -> on/off, supplied by main.qml. Toggle actions with an entry
    // here show a check mark (D2), the same state Quick settings shows.
    property var toggleStates: ({})
    // Root submenu row -> current value shown inline ("Speed": "Normal").
    // Supplied by main.qml.
    property var valueHints: ({})
    // Aspect ratio names, index = the app-set-aspect argument (main.qml
    // passes OnScreenController.aspectLabels).
    property var aspectLabels: []
    // Mini player corner ("tl", "tr", "bl", "br"), "" when not in the mini player.
    property string miniCorner: ""
    readonly property var fallbackAspectLabels: ["Original", "16:9", "4:3", "2.35:1"]
    readonly property var speedPresets: [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]

    // Commands given a row during the current rebuild. Whatever input.conf
    // has that the layout below does not place lands at the end of More.
    property var placed: ({})

    // Entries moved into a different submenu than input.conf gives them:
    // Jump to start joins Seek, the chapter actions get their own submenu.
    // Optional text replaces the label there.
    readonly property var menuMoves: ({
        "seek 0 absolute-percent": { to: ["Playback", "Seek"] },
        "script-message app-chapters": { to: ["Playback", "Chapters"], text: "Choose chapter…" },
        "add chapter -1": { to: ["Playback", "Chapters"] },
        "add chapter 1": { to: ["Playback", "Chapters"] }
    })

    // Submenu order and separators: related actions sit together and a
    // divider marks each group. Keys are section paths ("Playback > Seek");
    // entries are commands, or "menu:<title>" for nested submenus. Anything
    // not listed (a new binding, a user's own) lands in a final group.
    readonly property var menuGroups: ({
        "Playback > Seek": [["script-message app-goto-time"],
                            ["seek -5", "seek 5", "seek -10", "seek 10", "seek -30", "seek 30",
                             "seek -90", "seek 90"],
                            ["seek 0 absolute-percent"]],
        "Playback > Speed": [["script-message app-shuttle-down", "script-message app-shuttle-up", "set speed 1.0"]],
        "Playback > Chapters": [["script-message app-chapters"], ["add chapter -1", "add chapter 1"]],
        "Audio": [["script-message app-audio-menu", "cycle audio"],
                  ["add volume 5", "add volume -5", "cycle mute"],
                  ["add audio-delay -0.1", "add audio-delay 0.1", "set audio-delay 0"]],
        "Subtitles": [["script-message app-subtitle-menu", "script-message app-subtitle-cycle",
                       "cycle sub-visibility", "script-message app-load-subtitles"],
                      ["sub-seek -1", "sub-seek 1"],
                      ["add sub-delay -0.1", "add sub-delay 0.1"]],
        "Picture": [["cycle-values video-rotate 90 180 270 0",
                     "script-message app-zoom-in", "script-message app-zoom-out", "script-message app-zoom-reset"],
                    ["screenshot", "script-message app-screenshot-clipboard"],
                    ["script-message app-shader-dialog"]]
    })

    width: minWidth
    spacing: 0
    leftPadding: 5; rightPadding: 5; topPadding: 5; bottomPadding: 5
    leftInset: 0; rightInset: 0; topInset: 0; bottomInset: 0
    background: PanelBackground {}
    contentItem: MenuList { menu: root }
    delegate: FrameMenuItem {}

    // The style's list adds its own gap between items; this one does not,
    // so rows sit on an exact 28 px pitch.
    component MenuList: ListView {
        property Menu menu
        implicitHeight: contentHeight
        model: menu.contentModel
        spacing: 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: Window.window ? contentHeight + menu.topPadding + menu.bottomPadding > Window.window.height : false
        currentIndex: menu.currentIndex
    }

    // The platform style follows the Windows light/dark setting, so a light
    // system theme drew a white menu over the dark player. Every part of the
    // menu is drawn here with the shared popover tokens instead. The edge is
    // a separate child so the translucent border composites over the opaque
    // fill, not over the shadow; the shadow (MultiEffect auto-padding) is
    // drawn outside the panel bounds.
    component PanelBackground: Rectangle {
        color: Colors.surface
        radius: 8
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowBlur: 1.0
            blurMax: 24
            shadowVerticalOffset: 8
            shadowOpacity: 0.55
            shadowColor: "black"
        }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 1
            border.color: Colors.menuBorder
        }
    }

    component FrameMenuItem: MenuItem {
        id: leaf
        font.pixelSize: 13
        font.family: Fonts.ui
        implicitWidth: (leaf.menu ? leaf.menu.width : root.minWidth) - 10
        implicitHeight: 28
        // Root rows keep the 16 px icon column. A submenu keeps it only
        // when something can sit there (a check or an icon); otherwise its
        // labels start at the row's own inset, with no empty gutter.
        leftPadding: leaf.atRoot || !leaf.menu || leaf.menu.gutter !== false ? 33 : 12
        // One right edge (round 13): shortcut text, chevron ink and check
        // ink all end at the 8 px right padding. A submenu row's value sits
        // 6 px left of the chevron ink.
        rightPadding: 8
        topPadding: 0; bottomPadding: 0
        leftInset: 0; rightInset: 0; topInset: 0; bottomInset: 0

        readonly property bool atRoot: leaf.menu === root
        // 16 px icon in the x 8 column (root rows only). A submenu row takes
        // its icon and value key from the submenu it opens.
        property string rowIcon: ""
        readonly property string iconSource: leaf.subMenu && leaf.subMenu.rowIcon !== undefined
                                             ? leaf.subMenu.rowIcon : leaf.rowIcon
        // One short token; the cap is a safety net for long track names.
        readonly property string valueText: {
            const raw = leaf.subMenu && leaf.subMenu.valueKey
                      ? String(root.valueHints[leaf.subMenu.valueKey] || "") : ""
            return raw.length > root.maxValueChars ? raw.slice(0, root.maxValueChars - 1) + "…" : raw
        }
        // Humanized shortcut (P3/F12), filled at creation from the
        // MenuBuilder leaf "key"; dynamically added entries leave it empty.
        property string shortcutHint: ""
        // A root toggle has its icon in the left column, so its on state is
        // a check at the right, in place of the shortcut.
        readonly property bool rightCheck: leaf.atRoot && leaf.checkable
        readonly property string rightText: leaf.subMenu ? leaf.valueText
                                            : (leaf.rightCheck && leaf.checked ? "" : leaf.shortcutHint)
        // Width this item needs; the menu fits itself to its widest item.
        // Space the chevron column takes from the text at the right edge.
        readonly property real chevronReserve: subMenu ? root.chevronInk + 6 : 0
        readonly property real naturalWidth: leftPadding + rightPadding + label.implicitWidth
            + (rightCheck ? 20 + Math.max(14, hint.implicitWidth)
                          : (hint.text !== "" ? 20 + hint.implicitWidth : 0))
            + (subMenu ? (hint.text !== "" ? leaf.chevronReserve : 20) : 0)

        contentItem: Item {
            Text {
                id: label

                text: leaf.text
                font: leaf.font
                color: Colors.text
                opacity: leaf.enabled ? 1 : 0.4
                elide: Text.ElideRight
                maximumLineCount: 1
                anchors.left: parent.left
                anchors.right: hint.text !== "" ? hint.left : parent.right
                anchors.rightMargin: hint.text !== "" ? 20 : (leaf.subMenu ? 20 : 0)
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: hint

                text: leaf.rightText
                color: Colors.textDim
                font.family: Fonts.ui
                font.pixelSize: 12
                anchors.right: parent.right
                anchors.rightMargin: Math.round(leaf.chevronReserve)
                anchors.verticalCenter: parent.verticalCenter
            }

            // check.svg ink ends at x 19.5 of 24: shift the box so the ink,
            // not the box, meets the right edge.
            Image {
                anchors.right: parent.right
                anchors.rightMargin: -Math.round(14 * (24 - 19.5) / 24)
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                visible: leaf.rightCheck && leaf.checked
                source: "qrc:/assets/icons/check.svg"
                sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
                                    Math.ceil(height * Screen.devicePixelRatio))
            }
        }

        indicator: Item {
            x: 8
            y: (leaf.height - height) / 2
            width: 16
            height: 16

            Image {
                anchors.fill: parent
                visible: leaf.iconSource !== ""
                source: leaf.iconSource
                fillMode: Image.PreserveAspectFit
                // Rasterize at device resolution: SVGs scale, their cached
                // raster does not.
                sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
                                    Math.ceil(height * Screen.devicePixelRatio))
                opacity: leaf.enabled ? 1 : 0.4
                layer.enabled: visible
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: Colors.menuIcon
                }
            }

            // Submenu toggles and choices: the check sits in the icon column.
            Image {
                anchors.centerIn: parent
                width: 14
                height: 14
                visible: leaf.iconSource === "" && !leaf.rightCheck && leaf.checkable && leaf.checked
                source: "qrc:/assets/icons/check.svg"
                sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
                                    Math.ceil(height * Screen.devicePixelRatio))
            }
        }

        // chevron-right.svg ink ends at x 18 of 24: place the box so the
        // ink meets the 8 px right edge.
        arrow: Image {
            x: Math.round(leaf.width - 8 - width * 18 / 24)
            y: (leaf.height - height) / 2
            width: 14
            height: 14
            visible: leaf.subMenu !== null
            opacity: 0.85
            source: "qrc:/assets/icons/chevron-right.svg"
            sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
                                Math.ceil(height * Screen.devicePixelRatio))
        }

        background: Rectangle {
            radius: 5
            color: leaf.highlighted || leaf.down ? Colors.hover : "transparent"
        }

        // Menu makes a submenu's first row current when it opens. From the
        // pointer that reads as a selection the user never made, so clear
        // it; a keyboard open (Right/Enter, pointer elsewhere) keeps it.
        Connections {
            target: leaf.subMenu
            enabled: leaf.subMenu !== null
            function onOpened() {
                if (leaf.hovered)
                    leaf.subMenu.currentIndex = -1
            }
        }
    }

    Component {
        id: leafComp

        FrameMenuItem {}
    }

    Component {
        id: branchComp

        Menu {
            id: branch
            // Root rows read these through their subMenu.
            property string rowIcon: ""
            property string valueKey: ""
            // True when any row can show a check or an icon in the left
            // column (FrameMenuItem reads it for its left padding).
            property bool gutter: true
            function updateGutter() {
                let need = false
                for (let i = 0; i < count && !need; ++i) {
                    const it = itemAt(i)
                    if (!it || it.iconSource === undefined)
                        continue
                    need = it.checkable || it.iconSource !== ""
                }
                gutter = need
            }
            // Rows get their checkable flag right after addItem, so look
            // once the builder has finished.
            onCountChanged: Qt.callLater(updateGutter)
            width: root.minWidth
            // Open just clear of the parent's edge instead of over it.
            overlap: -2
            spacing: 0
            contentItem: MenuList { menu: branch }
            leftPadding: 5; rightPadding: 5; topPadding: 5; bottomPadding: 5
            leftInset: 0; rightInset: 0; topInset: 0; bottomInset: 0
            background: PanelBackground {}
            delegate: FrameMenuItem {}
            // Clamp inside the window, 10 px clear of its edges.
            margins: 10
        }
    }

    Component {
        id: separatorComp

        MenuSeparator {
            topPadding: 4
            bottomPadding: 4
            leftPadding: 6
            rightPadding: 6
            contentItem: Rectangle {
                implicitWidth: 10
                implicitHeight: 1
                color: Colors.divider
            }
        }
    }

    function openAt(x, y) {
        root.keyOverrides = Shortcuts.overrides()
        root.rebuild()
        // Qt 6.8: legacy Menu.popup(x,y) is gone; Popup.open() is the
        // supported API. Position via the popup's x/y.
        root.x = x
        root.y = y
        root.open()
    }

    function iconUrl(name) {
        return name ? "qrc:/assets/icons/" + name + ".svg" : ""
    }

    function speedText(rate) {
        const r = Math.round(rate * 100) / 100
        return r === 1 ? "Normal" : r + "×"
    }

    function isBranch(entry) {
        return entry.children !== undefined && entry.children !== null
    }

    // Section node by path (["Playback", "Seek"]), or null.
    function sectionOf(tree, path) {
        let level = tree
        let node = null
        for (let p = 0; p < path.length; p++) {
            node = level.find(e => root.isBranch(e) && e.text === path[p]) || null
            if (!node)
                return null
            level = node.children
        }
        return node
    }

    // First leaf anywhere in the tree with this command, or null.
    function leafOf(entries, cmd) {
        for (let i = 0; i < entries.length; i++) {
            const e = entries[i]
            if (root.isBranch(e)) {
                const hit = root.leafOf(e.children, cmd)
                if (hit)
                    return hit
            } else if (e.command === cmd) {
                return e
            }
        }
        return null
    }

    // A divider before the next group, unless the menu is empty or already
    // ends in one.
    function addSeparator(menu) {
        if (menu.count === 0)
            return
        const last = menu.itemAt(menu.count - 1)
        if (last && last.naturalWidth === undefined)
            return
        menu.addItem(separatorComp.createObject(menu))
    }

    function dropTrailingSeparator(menu) {
        while (menu.count > 0) {
            const last = menu.itemAt(menu.count - 1)
            if (!last || last.naturalWidth !== undefined)
                return
            const old = menu.removeItem(last)
            if (old)
                old.destroy()
        }
    }

    function rebuild() {
        // Qt 6.8 Menu has no clear(); drain via removeItem + destroy —
        // removeItem alone keeps the old MenuItems parented forever.
        while (root.count > 0) {
            const old = root.removeItem(root.itemAt(root.count - 1))
            if (old)
                old.destroy()
        }
        root.placed = ({})
        const tree = root.model.length > 0 ? root.restructured(root.model) : []

        // One root action row: the input.conf leaf for cmd (its effective
        // shortcut and toggle state), or a plain entry when input.conf has
        // none and a fallback label is given.
        const action = (cmd, iconName, label, fallback) => {
            let entry = root.leafOf(tree, cmd)
            if (!entry && !fallback)
                return null
            entry = entry ? Object.assign({}, entry) : { command: cmd, text: label }
            if (label)
                entry.label = label
            const it = root.buildEntry(entry, root, "")
            if (it)
                it.rowIcon = root.iconUrl(iconName)
            return it
        }
        // One root submenu row from an input.conf section.
        const section = (path, title, iconName, valueKey, skip) => {
            const node = root.sectionOf(tree, path)
            if (!node)
                return null
            const children = skip ? node.children.filter(e => skip.indexOf(e.command) < 0) : node.children
            if (skip)
                skip.forEach(c => root.placed[c] = true)
            const sub = root.buildEntry({ text: node.text, children: children, title: title },
                                        root, path.slice(0, -1).join(" > "))
            if (sub) {
                sub.rowIcon = root.iconUrl(iconName)
                sub.valueKey = valueKey || ""
            }
            return sub
        }

        // Files. Open URL and Open from clipboard live in More (round 14).
        action("script-message app-open-file", "folder", "Open file…", true)
        root.addRecentFiles()

        // Playback.
        root.addSeparator(root)
        const play = action("cycle pause", "", "Play / Pause", true)
        if (play)
            play.rowIcon = Qt.binding(() => Player.isPaused ? root.iconUrl("play-outline")
                                                            : root.iconUrl("pause-outline"))
        root.addSpeedMenu(tree)
        section(["Playback", "Chapters"], "Chapters", "bookmark", "")

        // Tracks.
        root.addSeparator(root)
        section(["Audio"], "Audio", "audio", "Audio")
        section(["Subtitles"], "Subtitles", "captions", "Subtitles")

        // View. Aspect ratio and the Picture rows live in Video (round 14).
        root.addSeparator(root)
        action("script-message app-toggle-fullscreen", "fullscreen", "", false)
        action("script-message app-toggle-theater", "theater", "", false)
        root.addWindowMenu(tree)
        root.addVideoMenu(tree)
        const more = root.addMoreMenu(tree)

        // Panels. Playback info and About live in More (round 13).
        root.addSeparator(root)
        action("script-message app-toggle-playlist", "playlist", "Playlist", false)
        action("script-message app-settings", "settings", "", false)

        root.fillLeftovers(more, tree)
        root.dropTrailingSeparator(root)
        root.fitWidth(root, 0)
    }

    function addRecentFiles() {
        if (Settings.recentFiles.length === 0)
            return
        const sub = branchComp.createObject(root)
        sub.title = "Recent files"
        sub.rowIcon = root.iconUrl("clock")
        const paths = Settings.recentFiles
        const maxEntries = Math.min(paths.length, 6)
        for (var k = 0; k < maxEntries; k++) {
            const path = paths[k]
            if (path === "" || path === null || path === undefined)
                continue
            const displayName = MediaNames.displayFor(path)
            const item = leafComp.createObject(sub, { text: displayName })
            item.onTriggered.connect(function() {
                // F33: recent opens go through the app command so they
                // get the same folder auto-enqueue as the open dialog
                // (main.qml owns the enqueue orchestration).
                Player.command(["script-message", "app-open-recent", path])
            })
            sub.addItem(item)
        }
        if (sub.count === 0) {
            sub.destroy()
            return
        }
        root.fitWidth(sub, 0)
        root.addMenu(sub)
    }

    // Speed: the preset rates (current one checked), then the Slower /
    // Faster / Normal speed actions with their shortcuts.
    function addSpeedMenu(tree) {
        const sub = branchComp.createObject(root)
        sub.title = "Speed"
        sub.rowIcon = root.iconUrl("gauge")
        sub.valueKey = "Speed"
        for (let i = 0; i < root.speedPresets.length; i++) {
            const rate = root.speedPresets[i]
            const it = leafComp.createObject(sub, { text: root.speedText(rate), checkable: true })
            it.checked = Qt.binding(() => Math.abs(Player.speed - rate) < 0.01)
            it.onTriggered.connect(function() { Player.setSpeed(rate) })
            sub.addItem(it)
        }
        const node = root.sectionOf(tree, ["Playback", "Speed"])
        if (node) {
            const before = sub.count
            root.fillMenu(sub, node.children, "Playback > Speed")
            if (sub.count > before)
                sub.insertItem(before, separatorComp.createObject(sub))
        }
        root.fitWidth(sub, 0)
        root.addMenu(sub)
    }

    // Window: Mini player and its corner, Always on top | the size presets.
    // The corners have no key bindings, so they are built here like the
    // aspect ratios; picking one enters the mini player at that corner.
    function addWindowMenu(tree) {
        const node = root.sectionOf(tree, ["Window"])
        if (!node)
            return
        const sub = branchComp.createObject(root)
        sub.title = "Window"
        sub.rowIcon = root.iconUrl("window")
        sub.valueKey = "Window"
        const leaf = cmd => {
            const entry = root.leafOf(tree, cmd)
            if (entry) root.buildEntry(entry, sub, "")
        }
        leaf("script-message app-mini-player")
        const corners = branchComp.createObject(sub)
        corners.title = "Pin to corner"
        const names = [["tl", "Top left"], ["tr", "Top right"], ["bl", "Bottom left"], ["br", "Bottom right"]]
        for (let i = 0; i < names.length; i++) {
            const code = names[i][0]
            const it = leafComp.createObject(corners, { text: names[i][1], checkable: true })
            it.checked = Qt.binding(() => root.miniCorner === code)
            it.onTriggered.connect(function() {
                Player.command(["script-message", "app-pin-corner", code])
            })
            corners.addItem(it)
        }
        root.fitWidth(corners, 0)
        sub.addMenu(corners)
        leaf("script-message app-always-on-top")
        root.addSeparator(sub)
        for (let i = 0; i < node.children.length; i++) {
            const e = node.children[i]
            if (!root.isBranch(e) && e.command.indexOf("app-window-scale") >= 0)
                root.buildEntry(e, sub, "")
        }
        node.children.forEach(e => { if (!root.isBranch(e)) root.placed[e.command] = true })
        root.dropTrailingSeparator(sub)
        root.fitWidth(sub, 0)
        root.addMenu(sub)
    }

    // Video: the nested Aspect ratio submenu, then the input.conf Picture
    // rows (Rotate, Zoom | Screenshot, Copy frame | Apply shader).
    function addVideoMenu(tree) {
        const sub = branchComp.createObject(root)
        sub.title = "Video"
        sub.rowIcon = root.iconUrl("picture")
        root.addAspectMenu(sub)
        // Cycle aspect is replaced by the Aspect ratio submenu.
        root.placed["script-message app-cycle-aspect"] = true
        const node = root.sectionOf(tree, ["Picture"])
        if (node) {
            root.addSeparator(sub)
            root.fillMenu(sub, node.children.filter(e => e.command !== "script-message app-cycle-aspect"),
                          "Picture")
        }
        root.dropTrailingSeparator(sub)
        root.fitWidth(sub, 0)
        root.addMenu(sub)
    }

    // Aspect ratio: the four ratios, current one checked. Same command as
    // the Quick settings aspect picker. Nested in Video, so its row in
    // Video has no icon, only the current ratio before the chevron.
    function addAspectMenu(parentMenu) {
        const labels = root.aspectLabels && root.aspectLabels.length ? root.aspectLabels
                                                                     : root.fallbackAspectLabels
        const sub = branchComp.createObject(parentMenu)
        sub.title = "Aspect ratio"
        sub.valueKey = "Aspect ratio"
        for (let i = 0; i < labels.length; i++) {
            const index = i
            const name = labels[i]
            const it = leafComp.createObject(sub, { text: name, checkable: true })
            it.checked = Qt.binding(() => (root.valueHints["Aspect ratio"] || labels[0]) === name)
            it.onTriggered.connect(function() {
                Player.command(["script-message", "app-set-aspect", String(index)])
            })
            sub.addItem(it)
        }
        root.fitWidth(sub, 0)
        parentMenu.addMenu(sub)
    }

    // More: the rarer actions. Leftovers are appended by fillLeftovers()
    // once every other row has claimed its commands.
    function addMoreMenu(tree) {
        const sub = branchComp.createObject(root)
        sub.title = "More"
        sub.rowIcon = root.iconUrl("more")
        const groups = [
            ["script-message app-open-url", "script-message app-open-clipboard"],
            ["script-message app-loop-file",
             { path: ["Playback", "Seek"], title: "Seek" },
             { path: ["Playback", "A-B Loop"], title: "A-B loop" }],
            ["playlist-prev", "playlist-next"],
            ["script-message app-bookmark-add"],
            ["script-message app-cycle-time-display", "script-message app-shortcut-guide"],
            // About has no input.conf binding; the entry is made here. Its
            // dialog carries Check for updates (main.qml).
            ["script-message app-playback-info",
             { command: "script-message app-about", label: "About Frame" }]
        ]
        for (let g = 0; g < groups.length; g++) {
            root.addSeparator(sub)
            for (let j = 0; j < groups[g].length; j++) {
                const spec = groups[g][j]
                if (typeof spec === "string") {
                    const entry = root.leafOf(tree, spec)
                    if (entry)
                        root.buildEntry(entry, sub, "")
                } else if (spec.command) {
                    const entry = root.leafOf(tree, spec.command)
                    root.buildEntry(entry ? Object.assign({ label: spec.label }, entry)
                                          : { command: spec.command, text: spec.label, label: spec.label },
                                    sub, "")
                } else {
                    const node = root.sectionOf(tree, spec.path)
                    if (node)
                        root.buildEntry({ text: node.text, children: node.children, title: spec.title },
                                        sub, spec.path.slice(0, -1).join(" > "))
                }
            }
        }
        root.addMenu(sub)
        return sub
    }

    // Every input.conf entry the layout did not place, in input.conf order,
    // as the last group of More, so nothing bound becomes unreachable.
    function fillLeftovers(more, tree) {
        // Pickers the menu already covers: the Speed and Aspect submenus
        // replace the speed picker, and Quick settings is the gear button.
        // Listing them again under More would only duplicate rows.
        const covered = { "script-message app-speed-menu": true,
                          "script-message app-playback-menu": true }
        const rest = []
        const walk = entries => {
            for (let i = 0; i < entries.length; i++) {
                const e = entries[i]
                if (root.isBranch(e))
                    walk(e.children)
                else if (!root.placed[e.command] && !covered[e.command]
                         && rest.every(r => r.command !== e.command))
                    rest.push(e)
            }
        }
        walk(tree)
        if (rest.length > 0) {
            root.addSeparator(more)
            for (let i = 0; i < rest.length; i++)
                root.buildEntry(rest[i], more, "")
        }
        root.dropTrailingSeparator(more)
        if (more.count === 0) {
            root.removeMenu(more)
            more.destroy()
            return
        }
        root.fitWidth(more, 0)
    }

    // A copy of the MenuBuilder tree with menuMoves applied. Moved leaves
    // leave their section and join the named submenu (created when absent).
    function restructured(model) {
        const copy = entries => entries.map(e => root.isBranch(e)
            ? { text: e.text, children: copy(e.children) }
            : Object.assign({}, e))
        const tree = copy(model)
        const moved = []
        const strip = entries => {
            for (let i = entries.length - 1; i >= 0; i--) {
                const e = entries[i]
                if (root.isBranch(e))
                    strip(e.children)
                else if (root.menuMoves[e.command] !== undefined)
                    moved.unshift(entries.splice(i, 1)[0])
            }
        }
        strip(tree)
        for (let m = 0; m < moved.length; m++) {
            const spec = root.menuMoves[moved[m].command]
            let level = tree
            for (let p = 0; p < spec.to.length; p++) {
                let next = level.find(e => root.isBranch(e) && e.text === spec.to[p])
                if (!next) {
                    next = { text: spec.to[p], children: [] }
                    level.push(next)
                }
                level = next.children
            }
            if (spec.text) moved[m].text = spec.text
            level.push(moved[m])
        }
        return tree
    }

    // Size a menu to its widest item, within minWidth..maxWidth.
    function fitWidth(menu, extra) {
        let widest = extra || 0
        for (let i = 0; i < menu.count; i++) {
            const it = menu.itemAt(i)
            if (it && it.naturalWidth !== undefined)
                widest = Math.max(widest, it.naturalWidth)
        }
        menu.width = Math.max(root.minWidth, Math.min(root.maxWidth, Math.ceil(widest) + 10))
    }

    function splitCommand(cmd) {
        const args = []
        let i = 0
        while (i < cmd.length) {
            while (i < cmd.length && cmd[i] === " ")
                i++
            if (i >= cmd.length)
                break
            if (cmd[i] === "\"") {
                i++
                let token = ""
                while (i < cmd.length && cmd[i] !== "\"") {
                    token += cmd[i]
                    i++
                }
                if (i < cmd.length)
                    i++
                args.push(token)
            } else {
                let token = ""
                while (i < cmd.length && cmd[i] !== " ") {
                    token += cmd[i]
                    i++
                }
                args.push(token)
            }
        }
        return args
    }

    // The key that actually fires this command: the user override when
    // there is one, otherwise the input.conf key MenuBuilder parsed. An
    // override of "" means the user disabled the action, so no hint.
    function effectiveKey(entry) {
        return Shortcuts.commandKeys[entry.command] || ""
    }

    // Children of one submenu in display order, as groups (arrays). Known
    // entries follow menuGroups; the rest keep their input.conf order in a
    // final group. Empty groups (e.g. Chapters on a file without them) drop.
    function groupedChildren(section, children) {
        const spec = root.menuGroups[section]
        if (!spec)
            return [children]
        const keyOf = entry => root.isBranch(entry) ? "menu:" + entry.text : entry.command
        const groups = spec.map(() => [])
        const rest = []
        for (let i = 0; i < children.length; i++) {
            const key = keyOf(children[i])
            let placed = false
            for (let g = 0; g < spec.length && !placed; g++) {
                if (spec[g].indexOf(key) >= 0) {
                    groups[g].push(children[i])
                    placed = true
                }
            }
            if (!placed)
                rest.push(children[i])
        }
        for (let g = 0; g < spec.length; g++)
            groups[g].sort((a, b) => spec[g].indexOf(keyOf(a)) - spec[g].indexOf(keyOf(b)))
        groups.push(rest)
        return groups
    }

    // Append a section's children to sub, group by group with dividers.
    function fillMenu(sub, children, name) {
        const groups = root.groupedChildren(name, children)
        let first = true
        for (let g = 0; g < groups.length; g++) {
            const before = sub.count
            for (let j = 0; j < groups[g].length; j++)
                root.buildEntry(groups[g][j], sub, name)
            if (sub.count > before) {
                if (!first)
                    sub.insertItem(before, separatorComp.createObject(sub))
                first = false
            }
        }
    }

    // Adds one MenuBuilder entry to parentMenu: a submenu for a section, a
    // row for a leaf. Returns the submenu or the row (null when hidden).
    function buildEntry(entry, parentMenu, section) {
        if (root.isBranch(entry)) {
            const name = section === "" ? entry.text : section + " > " + entry.text
            const sub = branchComp.createObject(parentMenu)
            sub.title = entry.title || KeyNames.sentenceCase(entry.text)
            root.fillMenu(sub, entry.children, name)
            // A submenu emptied by a hidden entry is left out entirely.
            if (sub.count === 0) {
                sub.destroy()
                return null
            }
            root.fitWidth(sub, 0)
            parentMenu.addMenu(sub)
            return sub
        }
        const cmd = entry.command
        root.placed[cmd] = true
        // No chapters: no chapter actions, so the Chapters submenu
        // empties and is left out.
        if ((cmd === "script-message app-chapters" || cmd.indexOf("add chapter") === 0)
                && !Player.chapterList.length)
            return null
        const it = leafComp.createObject(parentMenu, {
            // A real ellipsis, never three ASCII dots.
            text: String(entry.label || KeyNames.actionLabel(cmd, entry.text)).replace(/\.\.\.$/, "…"),
            shortcutHint: KeyNames.humanize(root.effectiveKey(entry))
        })
        if (root.toggleStates[cmd] !== undefined) {
            it.checkable = true
            it.checked = Qt.binding(() => root.toggleStates[cmd] === true)
        }
        it.onTriggered.connect(function() { Player.command(root.splitCommand(cmd)) })
        parentMenu.addItem(it)
        return it
    }
}
