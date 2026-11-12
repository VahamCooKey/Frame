import QtQuick
import QtQuick.Controls

FocusScope {
    id: root
    objectName: "playbackInfo"
    anchors.fill: parent
    signal closed()
    // Space taken by the caption bar above the picture, if any.
    property real topInset: 40
    readonly property var rows: buildRows()
    // PQ or HLG transfer: the only case where the HDR caveat matters.
    readonly property bool hdrSource: {
        const info = Player.playbackInfo
        const video = info["video-dec-params"] || info["video-params"] || {}
        return ["pq", "hlg"].indexOf(String(video.gamma || "").toLowerCase()) >= 0
    }

    function value(text, fallback) {
        return text === undefined || text === null || String(text) === ""
            ? (fallback || "Unavailable") : String(text)
    }
    // mpv describes codecs as "H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10";
    // show the name and its best-known alias: "H.264 (AVC)".
    function codecName(text) {
        // Some codecs come as "AAC (Advanced Audio Coding)"; drop a long
        // parenthesised description the same way.
        const described = /^(.+?) \((.+)\)$/.exec(String(text || "").trim())
        if (described && described[2].indexOf(" / ") < 0)
            return described[2].length > 6 ? described[1] : String(text)
        const parts = String(text || "").split(" / ").map(p => p.trim()).filter(Boolean)
        if (parts.length < 2) return text
        // Only a short alias helps ("H.264 (AVC)"); a spelled-out name
        // ("Advanced Audio Coding") just repeats the codec at length.
        if (parts[1].indexOf(parts[0]) >= 0 || parts[1].length > 6) return parts[0]
        return parts[0] + " (" + parts[1] + ")"
    }
    // "nvdec" -> "NVIDIA"; the raw mpv token stays in the tooltip.
    function decoderVendor(token) {
        const t = String(token || "").toLowerCase()
        if (t.indexOf("nvdec") === 0 || t.indexOf("cuda") === 0) return "NVIDIA"
        if (t.indexOf("d3d11") === 0) return "Direct3D 11"
        if (t.indexOf("dxva2") === 0) return "DXVA2"
        if (t.indexOf("vaapi") === 0) return "VA-API"
        if (t.indexOf("vulkan") === 0) return "Vulkan"
        if (t.indexOf("videotoolbox") === 0) return "VideoToolbox"
        if (t.indexOf("qsv") === 0) return "Intel Quick Sync"
        if (t.indexOf("amf") === 0) return "AMD"
        return t
    }
    function capitalized(text) {
        const t = String(text || "")
        return t ? t.charAt(0).toUpperCase() + t.slice(1) : ""
    }
    // "SDR · BT.709" rather than mpv's "bt.709 · bt.1886 · nv12"; the raw
    // tokens are in the value's tooltip.
    function colorText(video) {
        const gamma = String(video.gamma || "").toLowerCase()
        const range = gamma === "pq" ? "HDR10 (PQ)" : gamma === "hlg" ? "HLG" : gamma ? "SDR" : ""
        const names = { "bt.709": "BT.709", "bt.2020": "BT.2020", "bt.601-525": "BT.601", "bt.601-625": "BT.601",
                        "bt.470m": "BT.470", "dci-p3": "DCI-P3", "display-p3": "Display P3", "srgb": "sRGB" }
        const prim = String(video.primaries || "")
        const primaries = names[prim.toLowerCase()] || prim.toUpperCase()
        return [range, primaries].filter(Boolean).join(" · ") || "Unavailable"
    }
    // "None", "7 (display)", "2 (decoder)" or both, in
    // place of mpv's "7 output · 0 decoder".
    function droppedText(output, decoder) {
        const o = Number(output) || 0, d = Number(decoder) || 0
        if (output === undefined && decoder === undefined) return "Unavailable"
        if (o === 0 && d === 0) return "None"
        const parts = []
        if (o > 0) parts.push(o + " (display)")
        if (d > 0) parts.push(d + " (decoder)")
        return parts.join(" · ")
    }
    // The active subtitle track, named like the subtitle menu.
    function subtitleText() {
        const tracks = Player.trackList || []
        for (let i = 0; i < tracks.length; ++i) {
            const t = tracks[i]
            if (t.type !== "sub" || !t.selected) continue
            // Same wording as the subtitle menu (OnScreenController.baseTrackLabel).
            const language = KeyNames.languageName(t.lang)
            const title = String(t.title || "").trim()
            const codec = KeyNames.codecName(t.codec)
            const parts = []
            if (language) parts.push(language)
            if (title && title.toLowerCase() !== language.toLowerCase()) parts.push(title)
            if (codec && title.toLowerCase().indexOf(codec.toLowerCase()) < 0) parts.push(codec)
            if (t.forced) parts.push("Forced")
            if (t.external) parts.push("External")
            return parts.length && (language || title) ? parts.join(" · ") : "Track " + t.id
        }
        return "Off"
    }
    function buildRows() {
        const info = Player.playbackInfo
        const video = info["video-dec-params"] || info["video-params"] || {}
        const audio = info["audio-params"] || {}
        const videoCodec = info["video-codec"]
        const audioCodec = info["audio-codec"]
        const outputDrops = info["frame-drop-count"]
        const decoderDrops = info["decoder-frame-drop-count"]
        const hasVideo = Player.videoWidth > 0 && Player.videoHeight > 0
        const fps = Player.containerFps > 0 ? Player.containerFps : Player.estimatedVfFps
        const rate = audio.samplerate ? (audio.samplerate / 1000) + " kHz" : ""
        return [
            { label: "Video", value: hasVideo ? value(codecName(videoCodec)) : "No video track" },
            { label: "Resolution", value: hasVideo ? Player.videoWidth + " × " + Player.videoHeight
                + (fps > 0 ? " · " + Math.round(fps * 1000) / 1000 + " fps" : "") : "—" },
            { label: "Decoding", value: !hasVideo ? "—" : Player.hwdecCurrent && Player.hwdecCurrent !== "no"
                ? "Hardware (" + decoderVendor(Player.hwdecCurrent) + ")" : "Software",
              raw: Player.hwdecCurrent && Player.hwdecCurrent !== "no" ? String(Player.hwdecCurrent) : "" },
            { label: "Source color", value: hasVideo ? colorText(video) : "—",
              raw: [video.primaries, video.gamma, video["hw-pixelformat"] || video.pixelformat].filter(Boolean).join(" · ") },
            { label: "Audio", value: audioCodec ? codecName(audioCodec) : "No audio track" },
            { label: "Audio format", value: [capitalized(audio.channels), rate].filter(Boolean).join(" · ") || "—",
              raw: audio.format ? String(audio.format) : "" },
            { label: "Dropped frames", value: !hasVideo ? "—" : droppedText(outputDrops, decoderDrops) },
            { label: "Subtitles", value: subtitleText() },
            { label: "Renderer", value: "OpenGL" }
        ]
    }
    // Not a text field, so it does not take the keyboard (D4): shortcuts
    // stay live underneath, Ctrl+I closes it again and Esc peels it via
    // app-escape like the shortcut guide. Focus goes to the panel itself,
    // not the close button, so a pointer open shows no focus ring; Tab
    // still reaches the button.
    function openDialog() {
        Player.playbackInfoActive = true
        root.forceActiveFocus()
    }
    function closeDialog() {
        Player.playbackInfoActive = false
        root.closed()
    }
    onVisibleChanged: Player.playbackInfoActive = root.visible
    Component.onCompleted: Player.playbackInfoActive = root.visible
    Component.onDestruction: Player.playbackInfoActive = false

    // A non-modal overlay in the top-left corner (2026-11-05): the picture
    // stays visible and clickable around it, so there is no scrim and no
    // click-away layer.
    Rectangle {
        id: panel
        x: 16; y: 16 + root.topInset
        width: Math.min(400, parent.width - 32)
        // Sized to its content (D6); scrolls only when the window is short.
        height: Math.min(header.height + details.height + 52, parent.height - y - 16)
        radius: 12
        color: Colors.overlay
        border.color: Colors.border
        Accessible.role: Accessible.Dialog
        Accessible.name: "Playback info"
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
        Column {
            id: header
            x: 22; y: 20
            width: parent.width - 44 - 36
            spacing: 4
            Text {
                text: "Playback info"
                color: Colors.text
                font.family: Fonts.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
            }
            Text {
                id: title
                width: parent.width
                text: Player.currentFile ? MediaNames.display(Player.currentFile, MediaNames.cleanupEnabled) : "Nothing playing"
                color: Colors.textDim; font.family: Fonts.ui; font.pixelSize: 12
                elide: Text.ElideMiddle
                bottomPadding: 12
            }
        }
        IconButton {
            id: closeButton
            anchors.right: parent.right; anchors.rightMargin: 12; y: 14
            width: 28; height: 28; iconSize: 14
            iconSource: "qrc:/assets/icons/close.svg"
            accessibleName: "Close playback info"
            tooltipEnabled: true
            tooltipHints: [{ label: "Close", command: "script-message app-playback-info" }]
            onActivated: root.closeDialog()
        }
        Rectangle {
            x: 22; y: header.y + header.height - 1
            width: parent.width - 44; height: 1
            color: Colors.divider
        }
        Flickable {
            id: scroll
            x: 22; y: header.y + header.height + 12
            width: parent.width - 44
            height: parent.height - y - 20
            contentHeight: details.height; clip: true
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: details
                width: scroll.width; spacing: 8
                Repeater {
                    model: root.rows
                    delegate: Row {
                        required property var modelData
                        width: details.width; spacing: 16
                        Text {
                            width: 112; text: modelData.label
                            color: Colors.textDim; font.family: Fonts.ui; font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            width: parent.width - 128; text: modelData.value
                            color: Colors.text; font.family: Fonts.ui; font.pixelSize: 13
                            font.features: { "tnum": 1 }
                            wrapMode: Text.WrapAnywhere
                            Accessible.role: Accessible.StaticText
                            Accessible.name: modelData.label + ": " + modelData.value
                            HoverHandler { id: valueHover }
                            ToolTip.visible: valueHover.hovered && !!modelData.raw
                            ToolTip.delay: 300
                            ToolTip.text: modelData.raw || ""
                        }
                    }
                }
                Text {
                    visible: root.hdrSource
                    width: parent.width; topPadding: 6
                    text: "Source color describes the file. HDR output on your display is not verified here."
                    color: Colors.textDim; font.family: Fonts.ui; font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
