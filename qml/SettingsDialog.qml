import QtQuick

// Persistent settings workspace: text navigation, grouped controls, panel-local popups.
Item {
    id: root

    anchors.fill: parent

    signal closed()
    // Video pane "Apply shader..." needs the platform file dialog, which
    // lives on main.qml's root (Native.openShaderFileDialog on Windows, the
    // deferred QML picker elsewhere) -- forwarded up through
    // DeferredSettingsDialog rather than reached via `parent`, which is the
    // Loader that owns this item, not the application root.
    signal openShaderRequested()

    // Category indices. Keyboard stays last-but-one so its existing
    // tabIndex===0 special-casing (openDialog, filterField focus) becomes
    // the kKeyboard branch below rather than a silent renumber trap.
    readonly property int kGeneral: 0
    readonly property int kVideo: 1
    readonly property int kAudio: 2
    readonly property int kSubtitles: 3
    readonly property int kInterface: 4
    readonly property int kKeyboard: 5
    readonly property int kAdvanced: 6

    property int tabIndex: kGeneral

    // Search metadata stays available without constructing every settings pane.
    // Labels match the rows; keywords include descriptions and familiar aliases.
    readonly property var settingsIndex: [
        { pane: kGeneral, group: "Continue watching", key: "general/resumeEnabled", label: "Remember playback position", keywords: "resume continue watching pick up where you left off" },
        { pane: kGeneral, group: "Files and playlist", key: "general/folderAutoEnqueue", label: "Add videos from the same folder", keywords: "automatically build playlist open video scan enqueue" },
        { pane: kGeneral, group: "Files and playlist", key: "general/singleInstance", label: "Open videos in this window", keywords: "single instance running new window next launch" },
        { pane: kGeneral, group: "Files and playlist", key: "general/playlistRepeat", label: "Repeat playlist", keywords: "loop queue start again after last video" },
        { pane: kGeneral, group: "History", key: "general/recentsLimit", label: "Keep recent files", keywords: "choose how many files appear in history" },
        { pane: kGeneral, group: "History", label: "Clear watch history", keywords: "remove recently opened files privacy" },
        { pane: kGeneral, group: "Screenshots", key: "general/screenshotFormat", label: "Image format", keywords: "saved screenshots png jpeg webp" },
        { pane: kGeneral, group: "Screenshots", key: "general/screenshotDirectory", label: "Screenshot directory", keywords: "save capture folder location" },
        { pane: kGeneral, group: "Screenshots", key: "general/screenshotTemplate", label: "File naming", keywords: "screenshot names timestamp sequence timecode" },
        { pane: kVideo, group: "Decoding", key: "video/hwdec", label: "Hardware decoding", keywords: "graphics card gpu smoother playback acceleration" },
        { pane: kVideo, group: "Decoding", key: "video/deinterlace", label: "Deinterlace", keywords: "smooth interlaced footage tv dvd" },
        { pane: kVideo, group: "Picture", key: "video/aspectOverride", label: "Aspect ratio", keywords: "original picture size shape widescreen stretch borders" },
        { pane: kVideo, group: "Picture", key: "video/videoSync", label: "Playback sync", keywords: "motion display refresh synchronization" },
        { pane: kVideo, group: "Shaders", label: "Video shader", keywords: "apply saved glsl hook file effect" },
        { pane: kVideo, group: "Shaders", key: "video/shadersEnabled", label: "Enable shaders", keywords: "disable saved effect temporarily" },
        { pane: kVideo, group: "Shaders", label: "Reset shaders", keywords: "remove clear saved glsl hook effects" },
        { pane: kAudio, group: "Output", key: "audio/device", label: "Output device", keywords: "choose speakers headphones sound" },
        { pane: kAudio, group: "Output", key: "audio/normalize", label: "Normalize volume", keywords: "loudness consistent between videos sound" },
        { pane: kAudio, group: "Track selection", key: "audio/preferredLanguage", label: "Preferred audio language", keywords: "saved default automatic japanese english hindi tracks session priority" },
        { pane: kAudio, group: "Timing", key: "audio/delayDefault", label: "Default audio delay", keywords: "timing sound earlier later lip sync global current file" },
        { pane: kSubtitles, group: "Track selection", key: "subtitles/preferredLanguage", label: "Preferred subtitle language", keywords: "saved default automatic english captions tracks session off priority" },
        { pane: kSubtitles, group: "Style", key: "subtitles/fontSize", label: "Text size", keywords: "captions font bigger smaller" },
        { pane: kSubtitles, group: "Style", key: "subtitles/color", label: "Text color", keywords: "captions readable colour" },
        { pane: kSubtitles, group: "Style", key: "subtitles/borderSize", label: "Outline", keywords: "captions contrast around text border" },
        { pane: kSubtitles, group: "Style", key: "subtitles/position", label: "Vertical position", keywords: "captions move top bottom" },
        { pane: kSubtitles, group: "Timing and loading", key: "subtitles/delayDefault", label: "Default subtitle delay", keywords: "captions timing earlier later sync global current file" },
        { pane: kSubtitles, group: "Timing and loading", key: "subtitles/autoSelect", label: "Turn on subtitles automatically", keywords: "captions first track default automatic" },
        { pane: kSubtitles, group: "Timing and loading", key: "subtitles/autoLoad", label: "Load matching subtitle files", keywords: "automatically find captions same filename external srt ass" },
        { pane: kInterface, group: "Color", key: "interface/accentColor", label: "Accent color", keywords: "colour monochrome pastel mint blue sand rose highlights" },
        { pane: kInterface, group: "Player controls", key: "interface/playlistMotion", label: "Playlist animation", keywords: "smooth scrolling pages system motion reduced" },
        { pane: kInterface, group: "Player controls", key: "interface/osdHideMs", label: "Hide controls after", keywords: "fade timeout bottom bar timeline" },
        { pane: kInterface, group: "Media names", key: "interface/cleanMediaNames", label: "Simplify filenames", keywords: "short real original titles remove release tags" },
        { pane: kKeyboard, group: "", label: "Keyboard shortcuts", keywords: "keys rebind hotkeys bindings play pause seek volume fullscreen playlist" },
        { pane: kAdvanced, group: "Performance", key: "advanced/demuxerMaxBytes", label: "Read-ahead cache", keywords: "buffer ahead media streams network" },
        { pane: kAdvanced, group: "Troubleshooting", key: "advanced/logLevel", label: "Log detail", keywords: "troubleshoot playback debug trace" },
        { pane: kAdvanced, group: "Troubleshooting", label: "Log files", keywords: "open folder playback log troubleshooting" },
        { pane: kAdvanced, group: "Text rendering", key: "interface/fontEngine", label: "Font engine", keywords: "rendering windows gdi freetype restart" },
        { pane: kAdvanced, group: "Reset", label: "Reset preferences", keywords: "restore defaults history bookmarks shortcuts kept" },
    ]
    // Readable current values for search results ("On", "1.4 s", "PNG").
    readonly property var valueNames: ({
        "interface/playlistMotion": { system: "System", on: "On", off: "Off" },
        "general/screenshotFormat": { png: "PNG", jpg: "JPEG", webp: "WebP" },
        "general/screenshotTemplate": { "Frame_%F_%p": "Filename + timestamp", "%F-%n": "Filename + sequence",
                                        "Frame_%p": "Timestamp only", "%F_%P": "Filename + full timecode" },
        "video/hwdec": { auto: "Auto", no: "Off", "auto-copy": "Auto (copy-back)" },
        "video/aspectOverride": { no: "Original", "16:9": "16:9", "4:3": "4:3", "2.35": "2.35:1" },
        "video/videoSync": { "display-resample": "Match display", audio: "Audio", "display-vdrop": "Display VDrop" },
        "subtitles/color": { "#FFFFFFFF": "White", "#FFFFFF00": "Yellow", "#FF00FFFF": "Cyan" },
        "interface/fontEngine": { "": "System default", gdi: "GDI", freetype: "FreeType" },
        "advanced/demuxerMaxBytes": { "": "Default", "32MiB": "32 MiB", "128MiB": "128 MiB", "512MiB": "512 MiB" },
        "advanced/logLevel": { info: "Info", warn: "Warning", debug: "Debug", trace: "Trace" }
    })
    readonly property var flagKeys: ["general/resumeEnabled", "general/folderAutoEnqueue", "general/singleInstance",
        "general/playlistRepeat", "video/deinterlace", "audio/normalize", "subtitles/autoSelect", "interface/cleanMediaNames"]
    function valueText(entry) {
        const key = entry.key
        if (!key) return ""
        if (root.flagKeys.indexOf(key) >= 0) return Settings.flag(key) ? "On" : "Off"
        if (key === "video/shadersEnabled") return Preferences.shadersEnabled ? "On" : "Off"
        if (key === "video/aspectOverride") return root.valueNames[key][Preferences.defaultAspect] || ""
        if (key === "audio/preferredLanguage" || key === "subtitles/preferredLanguage") {
            const lang = key === "audio/preferredLanguage" ? Preferences.preferredAudioLanguage : Preferences.preferredSubtitleLanguage
            const options = Preferences.languageOptions || []
            for (let i = 0; i < options.length; ++i)
                if (String(options[i].value) === String(lang)) return options[i].label
            return ""
        }
        const v = Settings.value(key)
        if (key === "audio/device") return v === "auto" || !v ? "System default" : ""
        if (key === "general/screenshotDirectory") return v ? "Custom folder" : "Default folder"
        if (key === "subtitles/autoLoad") return v === "no" ? "Off" : "On"
        if (key === "interface/osdHideMs") return (Number(v) / 1000) + " s"
        if (key === "interface/accentColor") {
            for (const a of Colors.accents)
                if (a.color.toUpperCase() === String(Colors.accent).toUpperCase()) return a.label
            return ""
        }
        if (key === "audio/delayDefault" || key === "subtitles/delayDefault") return Number(v).toFixed(2) + " s"
        if (key === "subtitles/position") return Math.round(Number(v)) + "%"
        if (key === "subtitles/fontSize" || key === "subtitles/borderSize") return Math.round(Number(v)) + " px"
        const names = root.valueNames[key]
        if (names) return names[String(v)] !== undefined ? names[String(v)] : ""
        return v === undefined || v === null ? "" : String(v)
    }
    readonly property var categoryLabels: ["Playback", "Video", "Audio", "Subtitles", "Appearance", "Shortcuts", "Advanced"]
    // Line icons for the navigation, indexed like categoryLabels.
    readonly property var categoryIcons: ["nav-playback", "nav-video", "nav-audio", "nav-subtitles",
                                          "nav-appearance", "nav-shortcuts", "nav-advanced"]
    property string revealLabel: ""

    property string searchText: ""
    // On the Shortcuts page the sidebar search filters the shortcut list
    // instead of searching settings, so the page has one search field.
    readonly property bool settingsSearch: searchText !== "" && tabIndex !== kKeyboard
    readonly property var searchResults: {
        const needle = root.searchText.trim().toLowerCase()
        if (needle === "")
            return []
        const words = needle.split(/\s+/)
        const matched = []
        for (let i = 0; i < root.settingsIndex.length; i++) {
            const entry = root.settingsIndex[i]
            const text = (entry.label + " " + root.categoryLabels[entry.pane] + " " + (entry.keywords || "")).toLowerCase()
            if (words.every(word => text.indexOf(word) >= 0)) matched.push(entry)
        }
        // Settings whose own label contains the words come first; the rest
        // (matched through the page name or keywords) follow. Within each,
        // settingsIndex is already in sidebar and page order.
        const inLabel = entry => words.every(word => entry.label.toLowerCase().indexOf(word) >= 0)
        return matched.filter(inLabel).concat(matched.filter(entry => !inLabel(entry)))
    }

    // The result label with every searched word in bold.
    function highlighted(label) {
        const esc = t => t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        const words = root.searchText.trim().toLowerCase().split(/\s+/).filter(w => w !== "")
        const lower = label.toLowerCase()
        const marks = new Array(label.length).fill(false)
        for (const w of words) {
            let at = lower.indexOf(w)
            while (at >= 0) {
                for (let i = at; i < at + w.length; ++i) marks[i] = true
                at = lower.indexOf(w, at + w.length)
            }
        }
        let out = "", bold = false
        for (let i = 0; i < label.length; ++i) {
            if (marks[i] !== bold) { out += marks[i] ? "<b>" : "</b>"; bold = marks[i] }
            out += esc(label[i])
        }
        return out + (bold ? "</b>" : "")
    }

    function goToCategory(index, label) {
        popupLayer.dismiss()
        root.revealLabel = label || ""
        root.searchText = ""
        root.tabIndex = index
        navigation.revealSelected()
        Qt.callLater(root.revealSetting)
    }
    function revealSetting() {
        if (!revealLabel || !paneLoader.item) return
        function findRow(item) {
            if (item.label === root.revealLabel) return item
            for (let i = 0; i < item.children.length; ++i) {
                const found = findRow(item.children[i])
                if (found) return found
            }
            return null
        }
        const row = findRow(paneLoader.item)
        if (row) settingsScroll.contentY = Math.max(0, Math.min(settingsScroll.contentHeight - settingsScroll.height, row.mapToItem(paneLoader.item, 0, 0).y - 12))
        revealLabel = ""
    }

    // Controls can change their own value during interaction, which removes
    // an initial QML binding. Refresh only controls for the changed key;
    // retain the current pane, focus, scroll position and prepared content.
    function refreshSetting(key) {
        if (!paneLoader.item) return
        function refresh(item) {
            if (item.preferenceKey !== undefined && (!key || item.preferenceKey === key)) {
                if (item.refreshPreference !== undefined)
                    item.refreshPreference()
                else if (item.checked !== undefined)
                    item.checked = Settings.flag(item.preferenceKey)
                else
                    item.value = Settings.value(item.preferenceKey)
            }
            for (let i = 0; i < item.children.length; ++i) refresh(item.children[i])
        }
        refresh(paneLoader.item)
    }

    Connections {
        target: Settings
        function onSettingChanged(key) {
            root.refreshSetting(key)
            if (key === "interface/accentColor")
                Colors.accent = Colors.restoredAccent(Settings.value(key))
            else if (key === "interface/cleanMediaNames")
                MediaNames.cleanupEnabled = Settings.flag(key)
        }
        function onSettingsReset() {
            popupLayer.dismiss()
            root.refreshSetting("")
        }
    }

    // The binding table, deduplicated by command (see loadBindings), the
    // filtered view the list renders, and the override map exactly as
    // Settings last handed it over -- never a locally patched copy, so the
    // applied map and the stored map cannot drift.
    property var allRows: []
    property var visibleRows: []
    // What the shortcut list draws: "header" and "row" items. Built from
    // visibleRows so filtering, grouping and collapsing share one path.
    property var shortcutItems: []
    // Category name -> true while expanded. Categories start collapsed so
    // the page opens on the most-used keys; kept for the session.
    property var expandedGroups: ({})
    // The everyday keys, pinned above the categories. They stay in their
    // own category too; both rows edit the same binding.
    readonly property var quickCommands: [
        "cycle pause", "seek -5", "seek 5", "add volume 5", "add volume -5",
        "cycle mute", "script-message app-toggle-fullscreen",
        "playlist-prev", "playlist-next", "script-message app-subtitle-cycle",
        "script-message app-toggle-playlist", "screenshot"
    ]
    property var overrideMap: ({})
    property var commandLabels: ({})

    // Capture state: the command being rebound, plus the command that already
    // owns the pressed key while the conflict warning is up.
    property string captureCommand: ""
    property string conflictOwner: ""
    property string pendingKey: ""

    // Colors is the chrome palette and carries no warning tone; danger is the
    // destructive red, which overstates a rebind that is merely already taken.
    readonly property color cWarn: Colors.warning
    property bool prepared: false

    function prepare() {
        if (prepared) return
        root.loadBindings()
        prepared = true
    }

    Connections {
        target: Menus
        function onMenuChanged() { root.prepared = false }
    }

    function openDialog() {
        root.searchText = ""
        root.endCapture()

        root.prepare()
        root.overrideMap = Settings.shortcutOverrides()
        root.applyFilter()

        Shortcuts.textInputActive = true
        // Keyboard focus restoration: the General pane has no text field of
        // its own, so the panel itself takes focus (Esc still reaches
        // Keys.onEscapePressed below either way).
        panel.forceActiveFocus()
    }

    function closeDialog() {
        popupLayer.dismiss()
        root.endCapture()
        Shortcuts.textInputActive = false
        root.closed()
    }

    // Keyboard focus restoration across categories: the filter box only
    // exists while the Keyboard pane is visible, so switching TO it should
    // refocus that field, and switching AWAY must not leave focus on a now-
    // hidden TextInput (invisible caret, keys silently swallowed).
    onTabIndexChanged: {
        root.endCapture()
        popupLayer.dismiss()
        Qt.callLater(navigation.revealSelected)
        if (!root.visible) return
        if (root.tabIndex === kKeyboard)
            searchField.forceActiveFocus()
        else
            panel.forceActiveFocus()
    }

    function loadBindings() {
        const table = Menus.bindingTable(MenuFiles)
        const rows = []
        const indexOfCommand = {}
        const labels = {}

        for (let i = 0; i < table.length; i++) {
            const key = table[i].key === undefined ? "" : table[i].key
            const command = table[i].command === undefined ? "" : table[i].command
            const menuPath = table[i].menuPath === undefined ? "" : table[i].menuPath
            if (key === "" || command === "")
                continue
            // A wheel binding cannot be produced by key capture, so a row
            // offering to rebind it would be a dead end.
            if (key.indexOf("WHEEL_") === 0)
                continue

            // An override is keyed on the command, so two keys running the
            // same command are one action with two keys, not two rows: two
            // rows would move in lockstep and one of them would always be
            // lying about its key. The extras become a hint instead.
            const known = indexOfCommand[command]
            if (known !== undefined) {
                rows[known].alt.push(key)
                continue
            }

            const segments = []
            const parts = menuPath.split(">")
            for (let p = 0; p < parts.length; p++) {
                const trimmed = parts[p].trim()
                if (trimmed !== "")
                    segments.push(trimmed)
            }
            // No "#menu:" comment means no path (frame stepping, the media
            // keys); the raw command is still a listable action, and showing
            // it is the difference between a shortcut editor and a menu
            // editor. The handful of such commands get plain names here so
            // the list never reads "set pause yes" next to "Play/Pause".
            const plain = root.plainCommandNames[command]
            const group = segments.length > 1 ? segments[0] : (plain ? plain.group : "")
            const label = segments.length > 0 ? KeyNames.actionLabel(command, segments[segments.length - 1])
                                              : (plain ? plain.label : command)

            indexOfCommand[command] = rows.length
            labels[command] = group === "" ? label : group + " " + label
            rows.push({
                command: command,
                key: key,
                group: group,
                category: root.shortcutCategory(group, segments, command),
                label: label,
                alt: [],
                search: (label + " " + group + " " + command + " "
                         + KeyNames.humanize(key)).toLowerCase()
            })
        }

        root.commandLabels = labels
        root.allRows = rows
    }

    function applyFilter() {
        const needle = filterField.text.trim().toLowerCase()
        if (needle === "") {
            root.visibleRows = root.allRows
            root.buildShortcutItems()
            return
        }
        const matched = []
        for (let i = 0; i < root.allRows.length; i++) {
            const row = root.allRows[i]
            // The search string is built from the defaults, so a rebound key
            // is matched separately or an action would drop out of a filter
            // that names the key it now has.
            const shown = KeyNames.humanize(root.effectiveKey(row.command, row.key)).toLowerCase()
            if (row.search.indexOf(needle) >= 0 || shown.indexOf(needle) >= 0)
                matched.push(row)
        }
        root.visibleRows = matched
        root.buildShortcutItems()
    }

    // Unfiltered: "Most used" first, then each category (in input.conf
    // order) as a collapsible header. Filtered: matching rows under their
    // categories, all open, without the pinned duplicates.
    function buildShortcutItems() {
        const filtering = filterField.text.trim() !== ""
        const items = []
        if (!filtering) {
            const byCommand = {}
            for (let i = 0; i < root.allRows.length; i++)
                byCommand[root.allRows[i].command] = root.allRows[i]
            const quick = []
            for (let q = 0; q < root.quickCommands.length; q++) {
                const row = byCommand[root.quickCommands[q]]
                if (row !== undefined)
                    quick.push(row)
            }
            if (quick.length > 0) {
                items.push({ kind: "header", group: "Most used", count: quick.length,
                             collapsible: false, expanded: true, first: true })
                for (let r = 0; r < quick.length; r++)
                    items.push({ kind: "row", row: quick[r], showGroup: true, nested: false })
            }
        }
        const order = []
        const members = {}
        for (let i = 0; i < root.visibleRows.length; i++) {
            const row = root.visibleRows[i]
            const group = row.category
            if (members[group] === undefined) {
                members[group] = []
                order.push(group)
            }
            members[group].push(row)
        }
        // Fixed order, so related categories sit together whatever order
        // the binding table lists them in; unknown categories go last.
        const preferred = ["File", "Playback", "Seeking", "Speed", "Playlist and chapters",
                           "Audio", "Subtitles", "View", "Window", "Picture", "Other"]
        const rank = name => {
            const i = preferred.indexOf(name)
            return i < 0 ? preferred.length : i
        }
        order.sort((a, b) => rank(a) - rank(b))
        for (let g = 0; g < order.length; g++) {
            const group = order[g]
            const expanded = filtering || root.expandedGroups[group] === true
            items.push({ kind: "header", group: group, count: members[group].length,
                         collapsible: !filtering, expanded: expanded, first: items.length === 0 })
            if (expanded) {
                for (let r = 0; r < members[group].length; r++)
                    items.push({ kind: "row", row: members[group][r], showGroup: false, nested: !filtering })
            }
        }
        root.shortcutItems = items
    }

    function toggleShortcutGroup(group) {
        const next = Object.assign({}, root.expandedGroups)
        next[group] = !(next[group] === true)
        root.expandedGroups = next
        // A new model resets the view to the top; keep the header the user
        // clicked where it was.
        const y = list.contentY
        root.buildShortcutItems()
        list.forceLayout()
        list.contentY = Math.max(0, Math.min(y, list.contentHeight - list.height))
    }

    function effectiveKey(command, defaultKey) {
        const stored = root.overrideMap[command]
        return stored === undefined ? defaultKey : stored
    }

    function isOverridden(command) {
        return root.overrideMap[command] !== undefined
    }

    // The command that took <key> away from <command>, or "". A row whose
    // default key was handed to another action still shows that key, so it
    // has to be marked rather than left looking bound.
    function shadowedBy(command, key) {
        if (key === "")
            return ""
        const map = root.overrideMap
        for (const other in map) {
            if (other !== command && map[other] === key)
                return other
        }
        return ""
    }

    // Readable names for input.conf bindings that carry no "#menu:" path.
    readonly property var plainCommandNames: ({
        "set pause no": { label: "Play", group: "Playback" },
        "set pause yes": { label: "Pause", group: "Playback" },
        "stop": { label: "Stop", group: "Playback" },
        "seek 0 absolute-percent": { label: "Go to start", group: "Playback" },
        "script-message app-frame-step-forward": { label: "Next frame", group: "Playback" },
        "script-message app-frame-step-back": { label: "Previous frame", group: "Playback" },
        "script-message app-quit": { label: "Quit", group: "File" },
        "frame-step": { label: "Next frame", group: "Playback" },
        "frame-back-step": { label: "Previous frame", group: "Playback" },
        "script-message app-escape": { label: "Close menu or leave fullscreen", group: "View" },
    })

    // Playback holds over thirty actions, so the list splits it the way
    // people look for them: seeking, speed, and moving between videos.
    readonly property var playlistCommands: [
        "script-message app-toggle-playlist", "playlist-prev", "playlist-next",
        "add chapter -1", "add chapter 1", "script-message app-chapters"
    ]
    function shortcutCategory(group, segments, command) {
        if (group !== "Playback")
            return group === "" ? "Other" : group
        if (segments.length > 2 && segments[1] === "Seek")
            return "Seeking"
        if ((segments.length > 2 && segments[1] === "Speed")
                || command === "script-message app-speed-menu")
            return "Speed"
        if (root.playlistCommands.indexOf(command) >= 0)
            return "Playlist and chapters"
        return "Playback"
    }

    function commandLabel(command) {
        const known = root.commandLabels[command]
        return known === undefined ? command : known
    }

    function startCapture(command) {
        root.captureCommand = command
        root.conflictOwner = ""
        root.pendingKey = ""
        captureSink.forceActiveFocus()
    }

    function endCapture() {
        root.captureCommand = ""
        root.conflictOwner = ""
        root.pendingKey = ""
        if (root.tabIndex === root.kKeyboard && root.visible)
            searchField.forceActiveFocus()
    }

    function applyCapture(name) {
        if (root.captureCommand === "")
            return
        const owner = Shortcuts.commandForKey(name)
        if (owner !== "" && owner !== root.captureCommand) {
            // Never double-bind: mpv keeps one of the two and the user has no
            // way to tell which. The warning stays up until they confirm,
            // press another key, or cancel.
            root.conflictOwner = owner
            root.pendingKey = name
            return
        }
        root.conflictOwner = ""
        root.pendingKey = ""
        root.commitCapture(name)
    }

    function commitCapture(name) {
        const command = root.captureCommand
        if (command === "")
            return
        // Two commands claiming one key in the override map would be applied
        // in QVariantMap order and only one keybind would survive, so the
        // loser's override goes rather than racing. A loser that holds the
        // key only by default keeps its entry and is marked in the list.
        const owner = root.conflictOwner
        if (owner !== "" && owner !== command && root.overrideMap[owner] === name)
            Settings.clearShortcutOverride(owner)

        Settings.setShortcutOverride(command, name)
        root.endCapture()
        root.reloadOverrides()
    }

    function disableCommand(command) {
        Settings.setShortcutOverride(command, "")
        root.reloadOverrides()
    }

    function resetCommand(command) {
        Settings.clearShortcutOverride(command)
        root.reloadOverrides()
    }

    function resetAll() {
        Settings.clearShortcutOverrides()
        root.reloadOverrides()
    }

    // One read of the stored map feeds both the applier and the list, so what
    // mpv was told and what the panel shows come from the same source.
    function reloadOverrides() {
        const map = Settings.shortcutOverrides()
        Shortcuts.applyOverrides(map)
        root.overrideMap = map
    }

    component LinkText: Text {
        id: link
        signal activated()
        color: linkArea.containsMouse || activeFocus ? Colors.text : Colors.textDim
        font.pixelSize: 12
        font.family: Fonts.ui
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.onPressAction: activated()
        Keys.onReturnPressed: activated()
        Keys.onSpacePressed: activated()
        MouseArea {
            id: linkArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: link.activated()
        }
    }

    Keys.onEscapePressed: {
        if (root.searchText !== "") root.searchText = ""
        else root.closeDialog()
    }
    // Ctrl+Tab / Ctrl+Shift+Tab and Ctrl+PgDn / Ctrl+PgUp step through the
    // categories, as in other tabbed settings windows. Text fields do not
    // consume Ctrl+Tab, so this works from the search and filter boxes too.
    // Navigation order and grouping (2026-11-05): the media categories,
    // then the interface ones, then Advanced; a divider starts each group.
    readonly property var categoryOrder: [kGeneral, kVideo, kAudio, kSubtitles, kInterface, kKeyboard, kAdvanced]
    function startsNavGroup(category) { return category === kInterface || category === kAdvanced }
    function stepCategory(step) {
        const order = root.categoryOrder
        const at = Math.max(0, order.indexOf(root.tabIndex))
        root.goToCategory(order[(at + step + order.length) % order.length])
    }
    Keys.onPressed: event => {
        if (!(event.modifiers & Qt.ControlModifier)) return
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_PageDown) {
            root.stepCategory(1); event.accepted = true
        } else if (event.key === Qt.Key_Backtab || event.key === Qt.Key_PageUp) {
            root.stepCategory(-1); event.accepted = true
        }
    }
    Rectangle { anchors.fill: parent; color: Colors.scrim }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: root.closeDialog()
    }
    Rectangle {
        id: panel
        objectName: "settingsPanel"
        anchors.centerIn: parent
        width: Math.min(980, root.width - (root.width < 700 ? 24 : 64))
        height: Math.min(700, root.height - (root.height < 500 ? 24 : 64))
        readonly property bool compact: width < 700
        readonly property int sidebarWidth: 220
        property alias settingsPopupLayer: popupLayer
        radius: 12
        color: Colors.panel
        border.color: Colors.border
        onWidthChanged: if (popupLayer) popupLayer.dismiss()
        onHeightChanged: if (popupLayer) popupLayer.dismiss()
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton }
        Rectangle {
            visible: !panel.compact
            x: 1; y: 1
            width: panel.sidebarWidth
            height: parent.height - 2
            radius: 11
            color: Colors.sidebar
            Rectangle { anchors.right: parent.right; width: 11; height: parent.height; color: parent.color }
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Colors.divider }
        }
        Text {
            x: 24
            y: 22
            visible: !panel.compact
            text: "Settings"
            color: Colors.text
            font.family: Fonts.ui
            font.pixelSize: 15
            font.weight: Font.DemiBold
            Accessible.role: Accessible.Heading
        }
        IconButton {
            x: panel.width - 46
            y: 14
            width: 32; height: 32; iconSize: 16
            z: 2
            iconSource: "qrc:/assets/icons/close.svg"
            accessibleName: "Close settings"
            tooltipEnabled: true
            tooltipHints: [{ label: "Close settings", keys: "ESC" }]
            onActivated: root.closeDialog()
        }
        Rectangle {
            id: searchBox
            x: panel.compact ? 20 : 16
            y: panel.compact ? 16 : 56
            width: panel.compact ? panel.width - 80 : panel.sidebarWidth - 32
            height: 30
            radius: 8
            color: Colors.control
            border.color: searchField.activeFocus ? Colors.fieldFocus : Colors.fieldBorder
            Image {
                x: 10; anchors.verticalCenter: parent.verticalCenter
                width: 14; height: 14
                source: "qrc:/assets/icons/search.svg"
                sourceSize: Qt.size(28, 28)
                opacity: 0.6
            }
            TextInput {
                id: searchField
                x: 32
                width: parent.width - 58
                height: parent.height
                verticalAlignment: TextInput.AlignVCenter
                font.family: Fonts.ui
                font.pixelSize: 13
                color: Colors.text
                selectByMouse: true
                selectionColor: Colors.controlSelected
                clip: true
                text: root.searchText
                onTextChanged: root.searchText = text
                Accessible.name: "Search settings"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: searchField.text === ""
                    text: root.tabIndex === root.kKeyboard ? "Find a shortcut" : "Search"
                    color: Colors.textDim
                    font: searchField.font
                }
            }
            IconButton {
                anchors.right: parent.right; anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 22; height: 22; iconSize: 14
                visible: root.searchText !== ""
                iconSource: "qrc:/assets/icons/close.svg"
                accessibleName: "Clear search"
                onActivated: { root.searchText = ""; searchField.forceActiveFocus() }
            }
        }
        ListView {
            id: navigation
            objectName: "settingsCategories"
            x: panel.compact ? 16 : 12
            y: panel.compact ? 58 : 98
            width: panel.compact ? panel.width - 62 : panel.sidebarWidth - 24
            height: panel.compact ? 38 : panel.height - y - 56
            orientation: panel.compact ? ListView.Horizontal : ListView.Vertical
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: root.categoryOrder
            function revealSelected() {
                positionViewAtIndex(model.indexOf(root.tabIndex), ListView.Contain)
            }
            function focusCategory(index) {
                index = Math.max(0, Math.min(count - 1, index))
                root.goToCategory(model[index])
                forceLayout()
                const item = itemAtIndex(index)
                if (item) item.forceActiveFocus(Qt.TabFocusReason)
            }
            onWidthChanged: Qt.callLater(revealSelected)
            // One row per category. A row that starts a group carries the
            // divider above it, so the ListView keeps a single delegate.
            delegate: Item {
                id: category
                required property int index
                required property int modelData
                readonly property bool selected: root.tabIndex === modelData && !root.settingsSearch
                readonly property bool groupStart: root.startsNavGroup(modelData) && index > 0
                readonly property int gap: groupStart ? 13 : 0
                width: panel.compact ? pill.width + gap : navigation.width
                height: panel.compact ? 34 : 32 + gap
                activeFocusOnTab: true
                Accessible.role: Accessible.PageTab
                Accessible.name: root.categoryLabels[modelData]
                Accessible.selected: selected
                Accessible.onPressAction: root.goToCategory(modelData)
                Keys.onReturnPressed: root.goToCategory(modelData)
                Keys.onSpacePressed: root.goToCategory(modelData)
                Keys.onLeftPressed: navigation.focusCategory(index - 1)
                Keys.onRightPressed: navigation.focusCategory(index + 1)
                Keys.onUpPressed: navigation.focusCategory(index - 1)
                Keys.onDownPressed: navigation.focusCategory(index + 1)
                onActiveFocusChanged: if (activeFocus) navigation.positionViewAtIndex(index, ListView.Contain)
                Rectangle {
                    visible: category.groupStart
                    x: panel.compact ? 6 : 10
                    y: panel.compact ? 9 : 6
                    width: panel.compact ? 1 : parent.width - 24
                    height: panel.compact ? 16 : 1
                    color: Colors.divider
                }
                // Soft fill only: no border or weight change for the
                // selected page. The accent ring is for keyboard focus.
                Rectangle {
                    id: pill
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    width: panel.compact ? navRow.width + 20 : parent.width
                    height: panel.compact ? 34 : 32
                    radius: 7
                    color: category.selected ? Colors.raised : (navArea.containsMouse ? Colors.hover : "transparent")
                    border.width: category.activeFocus ? 1 : 0
                    border.color: Colors.accent
                    Row {
                        id: navRow
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18; height: 18
                            source: "qrc:/assets/icons/" + root.categoryIcons[category.modelData] + ".svg"
                            sourceSize: Qt.size(36, 36)
                            opacity: category.selected ? 1 : 0.78
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.categoryLabels[category.modelData]
                            color: Colors.text
                            font.family: Fonts.ui
                            font.pixelSize: 14
                        }
                    }
                }
                MouseArea {
                    id: navArea
                    anchors.fill: pill
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.goToCategory(category.modelData)
                }
            }
        }
        IconButton {
            x: panel.width - 42; y: navigation.y + 2
            width: 26; height: 34; iconSize: 16
            visible: panel.compact && navigation.contentWidth > navigation.width
            readonly property bool atEnd: navigation.atXEnd
            iconSource: atEnd ? "qrc:/assets/icons/chevron-left.svg" : "qrc:/assets/icons/chevron-right.svg"
            accessibleName: atEnd ? "Earlier settings categories" : "More settings categories"
            tooltipEnabled: true
            onActivated: navigation.focusCategory(atEnd ? 0 : navigation.count - 1)
        }
        Row {
            x: 24
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            visible: !panel.compact
            spacing: 7
            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 14; height: 14
                source: "qrc:/assets/icons/check.svg"
                sourceSize: Qt.size(28, 28)
                opacity: 0.55
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Changes save automatically"
                color: Colors.textDim
                font.family: Fonts.ui
                font.pixelSize: 12
            }
        }
        Item {
            id: contentArea
            x: panel.compact ? 20 : panel.sidebarWidth + 32
            y: panel.compact ? 106 : 24
            width: panel.width - x - (panel.compact ? 20 : 36)
            height: panel.height - y - 2
            Column {
                id: pageHeading
                visible: !panel.compact
                width: parent.width - 30
                spacing: 4
                Text {
                    text: root.settingsSearch ? "Search results" : root.categoryLabels[root.tabIndex]
                    color: Colors.text
                    font.family: Fonts.ui
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    Accessible.role: Accessible.Heading
                }
            }
            Flickable {
                id: settingsScroll
                objectName: "settingsContent"
                y: panel.compact ? 0 : pageHeading.height + 20
                width: parent.width
                height: parent.height - y
                visible: root.searchText === "" && root.tabIndex !== root.kKeyboard
                contentWidth: width
                contentHeight: paneLoader.item ? paneLoader.item.implicitHeight + 28 : 0
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                onMovementStarted: popupLayer.dismiss()
                Loader {
                    id: paneLoader
                    objectName: "settingsPaneLoader"
                    width: settingsScroll.width
                    active: root.tabIndex !== root.kKeyboard
                    sourceComponent: {
                        switch (root.tabIndex) {
                        case root.kGeneral: return generalPaneComponent
                        case root.kVideo: return videoPaneComponent
                        case root.kAudio: return audioPaneComponent
                        case root.kSubtitles: return subtitlesPaneComponent
                        case root.kInterface: return interfacePaneComponent
                        case root.kAdvanced: return advancedPaneComponent
                        default: return null
                        }
                    }
                    onLoaded: { root.refreshSetting(""); settingsScroll.contentY = 0; Qt.callLater(root.revealSetting) }
                }
            }
            // Edge fades say "more this way" where rows are cut by the
            // scroll area (D12): strong at the bottom, a hint at the top.
            Rectangle {
                y: settingsScroll.y
                width: settingsScroll.width
                height: 18
                visible: settingsScroll.visible && settingsScroll.contentY > 1
                gradient: Gradient {
                    GradientStop { position: 0; color: Colors.panel }
                    GradientStop { position: 1; color: Qt.rgba(Colors.panel.r, Colors.panel.g, Colors.panel.b, 0) }
                }
            }
            Rectangle {
                y: settingsScroll.y + settingsScroll.height - height
                width: settingsScroll.width
                // 24 px and linear, so a row under it never looks disabled.
                height: 24
                visible: settingsScroll.visible && settingsScroll.contentHeight > settingsScroll.height
                         && settingsScroll.contentY < settingsScroll.contentHeight - settingsScroll.height - 1
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(Colors.panel.r, Colors.panel.g, Colors.panel.b, 0) }
                    GradientStop { position: 1; color: Colors.panel }
                }
            }
            Rectangle {
                // The track is inset 8 px top and bottom, clear of the
                // panel's rounded corner.
                readonly property real trackY: settingsScroll.y + 8
                readonly property real trackH: settingsScroll.height - 16
                x: parent.width + 16
                y: trackY + settingsScroll.visibleArea.yPosition * trackH
                width: 4
                height: Math.max(24, settingsScroll.visibleArea.heightRatio * trackH)
                radius: 2
                color: Colors.scrollThumb
                visible: settingsScroll.visible && settingsScroll.contentHeight > settingsScroll.height
            }
            ListView {
                id: searchResultsView
                y: panel.compact ? 0 : pageHeading.height + 16
                width: parent.width
                height: parent.height - y
                visible: root.settingsSearch
                model: root.searchResults
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                // Flat rows with hairlines, like the settings pages.
                delegate: Item {
                    id: resultRow
                    required property var modelData
                    required property int index
                    width: searchResultsView.width
                    height: 54
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.label + ", " + root.categoryLabels[modelData.pane]
                    Accessible.onPressAction: root.goToCategory(modelData.pane, modelData.label)
                    Keys.onReturnPressed: root.goToCategory(modelData.pane, modelData.label)
                    Rectangle {
                        x: -8; width: parent.width + 8
                        y: 2; height: parent.height - 4
                        radius: 8
                        color: resultArea.containsMouse ? Colors.hover : "transparent"
                        border.width: resultRow.activeFocus ? 1 : 0
                        border.color: Colors.accent
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; height: 1
                        color: Colors.divider
                        visible: resultRow.index < searchResultsView.count - 1
                    }
                    Column {
                        x: 0; anchors.verticalCenter: parent.verticalCenter
                        width: resultValue.x - 16
                        spacing: 3
                        Text {
                            width: parent.width
                            text: root.highlighted(resultRow.modelData.label)
                            textFormat: Text.StyledText
                            elide: Text.ElideRight
                            color: Colors.text; font.family: Fonts.ui; font.pixelSize: 14
                        }
                        // Where it lives: page, then group.
                        Row {
                            spacing: 6
                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14; height: 14
                                source: "qrc:/assets/icons/" + root.categoryIcons[resultRow.modelData.pane] + ".svg"
                                sourceSize: Qt.size(28, 28)
                                opacity: 0.6
                            }
                            Text {
                                text: root.highlighted(root.categoryLabels[resultRow.modelData.pane]
                                      + (resultRow.modelData.group ? " › " + resultRow.modelData.group : ""))
                                textFormat: Text.StyledText
                                color: Colors.textDim; font.family: Fonts.ui; font.pixelSize: 13
                            }
                        }
                    }
                    Text {
                        id: resultValue
                        anchors.right: resultChevron.left; anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, 200)
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                        text: root.valueText(resultRow.modelData)
                        color: Colors.textDim; font.family: Fonts.ui; font.pixelSize: 13
                        font.features: { "tnum": 1 }
                    }
                    Image {
                        id: resultChevron
                        anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
                        width: 16; height: 16
                        source: "qrc:/assets/icons/chevron-right.svg"
                        sourceSize: Qt.size(32, 32)
                        opacity: resultArea.containsMouse || resultRow.activeFocus ? 0.85 : 0.45
                    }
                    MouseArea {
                        id: resultArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.goToCategory(resultRow.modelData.pane, resultRow.modelData.label)
                    }
                }
                Text {
                    visible: searchResultsView.count === 0
                    text: "No settings found. Try a different word."
                    color: Colors.textDim
                    font.family: Fonts.ui
                    font.pixelSize: 14
                }
            }
            Item {
                id: keyboardPane
                y: panel.compact ? 0 : pageHeading.height + 20
                width: parent.width
                height: parent.height - y - 18
                visible: root.tabIndex === root.kKeyboard
                // The sidebar search feeds this (hidden) filter field.
                Rectangle {
                    id: filterBox
                    visible: false
                    width: parent.width
                    height: 0
                    radius: 8
                    color: Colors.control
                    border.color: filterField.activeFocus ? Colors.fieldFocus : Colors.fieldBorder
                    Image {
                        x: 10; anchors.verticalCenter: parent.verticalCenter
                        width: 14; height: 14
                        source: "qrc:/assets/icons/search.svg"
                        sourceSize: Qt.size(28, 28)
                        opacity: 0.6
                    }
                    TextInput {
                        id: filterField
                        objectName: "shortcutFilter"
                        anchors.fill: parent; anchors.leftMargin: 32; anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter
                        color: Colors.text; font.pixelSize: 13; font.family: Fonts.ui
                        selectByMouse: true; selectionColor: Colors.controlSelected; clip: true
                        text: root.tabIndex === root.kKeyboard ? root.searchText : ""
                        onTextChanged: root.applyFilter()
                        Accessible.name: "Filter shortcuts"
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: filterField.text === ""
                            text: "Find an action or shortcut"
                            color: Colors.textDim
                            font: filterField.font
                        }
                    }
                }
                ListView {
                    id: list
                    // 6 px wider on the left with the rows shifted back by 6, so
                    // the inset hover fill (x: -6) isn't clipped against the text.
                    x: -6; y: 0; width: parent.width + 6; height: parent.height - 44
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: keyboardPane.visible ? root.shortcutItems : []
                    delegate: Item {
                        id: actionRow
                        required property var modelData
                        required property int index
                        readonly property bool isHeader: modelData.kind === "header"
                        readonly property var row: isHeader ? null : modelData.row
                        readonly property string command: isHeader ? "" : row.command
                        readonly property string boundKey: isHeader ? "" : root.effectiveKey(command, row.key)
                        readonly property bool overridden: !isHeader && root.isOverridden(command)
                        readonly property bool capturing: !isHeader && root.captureCommand === command
                        readonly property bool conflicting: capturing && root.conflictOwner !== ""
                        readonly property bool shadowed: !isHeader && root.shadowedBy(command, boundKey) !== ""
                        // The next item is a header (or nothing): no hairline
                        // under the last row of a group.
                        readonly property bool lastInGroup: index + 1 >= root.shortcutItems.length
                                                            || root.shortcutItems[index + 1].kind === "header"
                        width: list.width
                        // The pinned first header is 24 px with its title at the
                        // top, level with the first group title on other panes.
                        height: isHeader ? (modelData.first ? 24 : 42) : (conflicting ? 80 : 38)
                        // ListView places delegates at x 0, so the row content sits
                        // in a 6 px inset; the hover fill (x: -6) then has room.
                        Item {
                        x: 6; width: parent.width - 6; height: parent.height

                        // Category header: the pinned "Most used" title, or a
                        // collapsible category with its count and a chevron.
                        Item {
                            id: groupHeader
                            visible: actionRow.isHeader
                            anchors.left: parent.left; anchors.right: parent.right
                            y: actionRow.isHeader && actionRow.modelData.first ? 0 : parent.height - 30
                            height: actionRow.isHeader && actionRow.modelData.first ? 18 : 30
                            activeFocusOnTab: actionRow.isHeader && actionRow.modelData.collapsible
                            Accessible.role: Accessible.Button
                            Accessible.name: actionRow.isHeader
                                ? actionRow.modelData.group + ", " + actionRow.modelData.count + " shortcuts"
                                  + (actionRow.modelData.collapsible ? (actionRow.modelData.expanded ? ", expanded" : ", collapsed") : "")
                                : ""
                            Accessible.onPressAction: if (actionRow.modelData.collapsible) root.toggleShortcutGroup(actionRow.modelData.group)
                            Keys.onReturnPressed: if (actionRow.modelData.collapsible) root.toggleShortcutGroup(actionRow.modelData.group)
                            Keys.onSpacePressed: if (actionRow.modelData.collapsible) root.toggleShortcutGroup(actionRow.modelData.group)
                            Rectangle {
                                x: -6; width: parent.width + 6; height: parent.height; radius: 8
                                visible: actionRow.isHeader && actionRow.modelData.collapsible
                                color: headerArea.containsMouse ? Colors.hover : "transparent"
                                border.color: groupHeader.activeFocus ? Colors.accent : "transparent"
                            }
                            Image {
                                visible: actionRow.isHeader && actionRow.modelData.collapsible
                                x: 2; anchors.verticalCenter: parent.verticalCenter
                                width: 14; height: 14
                                source: "qrc:/assets/icons/chevron-right.svg"
                                sourceSize: Qt.size(28, 28)
                                rotation: actionRow.isHeader && actionRow.modelData.expanded ? 90 : 0
                                opacity: 0.7
                            }
                            Text {
                                x: actionRow.isHeader && actionRow.modelData.collapsible ? 24 : 0
                                anchors.verticalCenter: parent.verticalCenter
                                text: actionRow.isHeader ? actionRow.modelData.group : ""
                                color: Colors.text; font.family: Fonts.ui
                                font.pixelSize: actionRow.isHeader && !actionRow.modelData.collapsible ? 16 : 14
                                font.weight: actionRow.isHeader && !actionRow.modelData.collapsible ? Font.DemiBold : Font.Medium
                            }
                            Text {
                                anchors.right: parent.right; anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                visible: actionRow.isHeader && actionRow.modelData.collapsible
                                text: actionRow.isHeader ? actionRow.modelData.count : ""
                                color: Colors.textDim; font.family: Fonts.ui; font.pixelSize: 12
                            }
                            MouseArea {
                                id: headerArea
                                anchors.fill: parent
                                enabled: actionRow.isHeader && actionRow.modelData.collapsible
                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggleShortcutGroup(actionRow.modelData.group)
                            }
                        }

                        Item {
                            anchors.fill: parent
                            visible: !actionRow.isHeader
                            HoverHandler { id: rowHover }
                            // The whole row rebinds; the links above it
                            // (Disable, Reset) take their own clicks.
                            MouseArea {
                                width: parent.width; height: 38
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.startCapture(actionRow.command)
                            }
                            Rectangle {
                                x: -6; width: parent.width + 6; height: 38; radius: 7
                                color: rowHover.hovered || actionRow.capturing ? Colors.hover : "transparent"
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom
                                x: actionLabel.x; width: parent.width - x; height: 1
                                color: Colors.divider
                                visible: !actionRow.lastInGroup && !actionRow.conflicting
                            }
                            Text {
                                id: actionLabel
                                // Rows inside a category line up with the
                                // header text, past the chevron.
                                x: !actionRow.isHeader && actionRow.modelData.nested ? 24 : 0
                                y: 0; height: 38
                                width: Math.max(0, Math.min(implicitWidth, keyChip.x - x - linkRow.width - 24))
                                verticalAlignment: Text.AlignVCenter
                                text: actionRow.isHeader ? "" : actionRow.row.label
                                color: Colors.text; font.family: Fonts.ui; font.pixelSize: 14
                                elide: Text.ElideRight
                            }
                            Text {
                                // In "Most used" the category is a quiet suffix,
                                // so "Back 5 s" still says where it lives.
                                visible: !actionRow.isHeader && actionRow.modelData.showGroup
                                x: actionLabel.x + actionLabel.width + 8
                                y: 0; height: 38
                                width: Math.max(0, keyChip.x - x - linkRow.width - 16)
                                verticalAlignment: Text.AlignVCenter
                                text: actionRow.isHeader ? "" : actionRow.row.category
                                color: Colors.textDim; font.family: Fonts.ui; font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                            Row {
                                id: linkRow
                                anchors.right: keyChip.left; anchors.rightMargin: 14
                                y: 0; height: 38; spacing: 14
                                LinkText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Disable"
                                    visible: (rowHover.hovered || activeFocus) && actionRow.boundKey !== ""
                                    onActivated: root.disableCommand(actionRow.command)
                                }
                                LinkText {
                                    objectName: "shortcutRowReset"
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Reset"
                                    visible: actionRow.overridden
                                    onActivated: root.resetCommand(actionRow.command)
                                }
                            }
                            // The key as the same chips every menu uses,
                            // inside a quiet button that says "click to change".
                            Rectangle {
                                id: keyChip
                                readonly property bool showCaps: !actionRow.capturing && actionRow.boundKey !== "" && !actionRow.shadowed
                                x: parent.width - width; y: 6
                                width: (showCaps ? keyCaps.width : keyText.implicitWidth) + 16
                                height: 26; radius: 6
                                // Bare caps at rest; a frame only while
                                // capturing or focused.
                                color: actionRow.capturing ? Colors.fieldHover : "transparent"
                                border.width: actionRow.capturing || activeFocus ? 1 : 0
                                border.color: Colors.accent
                                // A changed binding carries a small accent mark so
                                // the rows the user has touched stand out while
                                // scrolling; the Reset link restores it.
                                Rectangle {
                                    visible: actionRow.overridden && !actionRow.capturing
                                    x: 5; y: 5; width: 5; height: 5; radius: 2.5
                                    color: Colors.accent
                                    Accessible.ignored: true
                                }
                                activeFocusOnTab: !actionRow.isHeader
                                Accessible.role: Accessible.Button
                                Accessible.name: actionRow.isHeader ? "" : "Rebind " + actionRow.row.label
                                Accessible.description: actionRow.boundKey === "" ? "Disabled" : KeyNames.humanize(actionRow.boundKey)
                                Accessible.onPressAction: root.startCapture(actionRow.command)
                                Keys.onReturnPressed: root.startCapture(actionRow.command)
                                Keys.onSpacePressed: root.startCapture(actionRow.command)
                                KeyCaps {
                                    id: keyCaps
                                    anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                                    visible: keyChip.showCaps
                                    sequence: actionRow.isHeader ? "" : KeyNames.humanize(actionRow.boundKey)
                                    textColor: Colors.text
                                }
                                Text {
                                    id: keyText
                                    anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                                    visible: !keyChip.showCaps
                                    text: actionRow.capturing ? "Press a key…" : actionRow.boundKey === "" ? "Disabled" : KeyNames.humanize(actionRow.boundKey)
                                    font.strikeout: actionRow.shadowed && !actionRow.capturing
                                    color: actionRow.capturing ? Colors.text : Colors.textDim
                                    font.family: Fonts.ui; font.pixelSize: 12
                                }
                                MouseArea {
                                    id: keyArea
                                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: root.startCapture(actionRow.command)
                                }
                            }
                            Item {
                                x: actionLabel.x; y: 40; width: parent.width - x; height: 36
                                visible: actionRow.conflicting
                                Text {
                                    width: parent.width - 145
                                    text: KeyNames.humanize(root.pendingKey) + " is used by " + root.commandLabel(root.conflictOwner)
                                    color: Colors.warning; font.family: Fonts.ui; font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                                Row {
                                    anchors.right: parent.right; spacing: 14
                                    LinkText { text: "Reassign"; color: Colors.warning; onActivated: root.commitCapture(root.pendingKey) }
                                    LinkText { text: "Cancel"; onActivated: root.endCapture() }
                                }
                            }
                        }
                        }
                    }
                    Text {
                        visible: list.count === 0
                        text: "No matching shortcuts."
                        color: Colors.textDim; font.family: Fonts.ui; font.pixelSize: 13
                    }
                }
                Rectangle {
                    x: parent.width + 16
                    y: list.y + 8 + list.visibleArea.yPosition * (list.height - 16)
                    width: 4; radius: 2
                    height: Math.max(24, list.visibleArea.heightRatio * (list.height - 16))
                    color: Colors.scrollThumb
                    visible: list.contentHeight > list.height
                }
                Rectangle {
                    x: list.x
                    y: list.y + list.height - height
                    width: list.width
                    height: 24
                    visible: list.contentHeight > list.height && !list.atYEnd
                    gradient: Gradient {
                        GradientStop { position: 0; color: Qt.rgba(Colors.panel.r, Colors.panel.g, Colors.panel.b, 0) }
                        GradientStop { position: 1; color: Colors.panel }
                    }
                }
                Rectangle {
                    anchors.bottom: resetAllButton.top; anchors.bottomMargin: 10
                    width: parent.width; height: 1
                    color: Colors.divider
                }
                FlatButton {
                    id: resetAllButton
                    objectName: "resetAllShortcuts"
                    anchors.bottom: parent.bottom
                    readonly property int changed: Object.keys(root.overrideMap).length
                    label: changed > 0 ? "Reset all shortcuts (" + changed + " changed)" : "Reset all shortcuts"
                    enabled: changed > 0
                    onActivated: if (changed > 0) root.resetAll()
                }
                Text {
                    anchors.verticalCenter: resetAllButton.verticalCenter; anchors.right: parent.right
                    visible: keyboardPane.width > 500
                    text: "Select a shortcut to change its key."
                    color: Colors.textDim; font.family: Fonts.ui; font.pixelSize: 13
                }
            }
        }
        Item {
            id: captureSink
            Keys.onPressed: event => {
                event.accepted = true
                if (event.key === Qt.Key_Escape) { root.endCapture(); return }
                const name = Shortcuts.toMpvKeyName(event.key, event.modifiers)
                if (name !== "") root.applyCapture(name)
            }
        }
        Component { id: generalPaneComponent; SettingsGeneralPane {} }
        Component { id: videoPaneComponent; SettingsVideoPane { onApplyShaderRequested: root.openShaderRequested() } }
        Component { id: audioPaneComponent; SettingsAudioPane {} }
        Component { id: subtitlesPaneComponent; SettingsSubtitlesPane {} }
        Component { id: interfacePaneComponent; SettingsInterfacePane {} }
        Component { id: advancedPaneComponent; SettingsAdvancedPane {} }
        Item { id: popupLayer; anchors.fill: parent; z: 100; signal dismiss() }
    }
}
