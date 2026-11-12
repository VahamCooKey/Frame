pragma Singleton
import QtQuick

// Humanizes mpv key names for display. A singleton because two very
// different views show the same key and must agree letter for letter: the
// context menu's shortcut column and the F11 settings list. When they
// disagree, a rebind looks like it did not take.
QtObject {
    id: root

    function forCommand(command) {
        return humanize(Shortcuts.commandKeys[command] || "")
    }
    function hint(label, command) {
        const key = forCommand(command)
        return key ? label + "  " + key : label
    }

    // One mpv key token; unknown names pass through unchanged.
    function humanizeName(name) {
        switch (name) {
            case "LEFT": return "\u2190"
            case "RIGHT": return "\u2192"
            case "UP": return "\u2191"
            case "DOWN": return "\u2193"
            case "SPACE": return "Space"
            case "BS": return "Backspace"
            case "PGUP": return "PgUp"
            case "PGDWN": return "PgDn"
            case "COMMA": return ","
            case "DOT": return "."
            case "ENTER": return "Enter"
            case "ESC": return "Esc"
            case "TAB": return "Tab"
            case "DEL": return "Delete"
            case "INS": return "Insert"
            case "PLAYPAUSE": return "Media Play/Pause"
            case "PLAY": return "Media Play"
            case "PAUSE": return "Media Pause"
            case "STOP": return "Media Stop"
            case "PREV": return "Media Previous"
            case "NEXT": return "Media Next"
            case "WHEEL_UP": return "Wheel Up"
            case "WHEEL_DOWN": return "Wheel Down"
            default: return name
        }
    }

    // A full mpv key: "Ctrl+o" -> "Ctrl+O", "Ctrl+Shift+RIGHT" -> the arrow
    // with its modifiers, "SPACE" -> "Space". Unknown tokens pass through;
    // an empty or non-string key yields "" so a caller can treat "no hint"
    // and "no binding" the same way.
    function humanize(key) {
        if (typeof key !== "string" || key === "")
            return ""
        const parts = key.split("+")
        const out = []
        for (let i = 0; i < parts.length; i++) {
            const token = parts[i]
            if (token === "")
                continue
            if (i === parts.length - 1) {
                const mapped = root.humanizeName(token)
                if (mapped !== token)
                    out.push(mapped)
                else if (token.length === 1)
                    out.push(token.toUpperCase())
                else
                    out.push(token)
            } else {
                // Modifier token: canonical capitalization (ctrl -> Ctrl).
                out.push(token.charAt(0).toUpperCase() + token.substring(1).toLowerCase())
            }
        }
        return out.join("+")
    }

    // Track naming shared by the track menus and Playback info.
    readonly property var languageNames: ({
        en: "English", eng: "English", ja: "Japanese", jpn: "Japanese", es: "Spanish", spa: "Spanish",
        fr: "French", fre: "French", fra: "French", de: "German", ger: "German", deu: "German",
        it: "Italian", ita: "Italian", pt: "Portuguese", por: "Portuguese", ru: "Russian", rus: "Russian",
        zh: "Chinese", chi: "Chinese", zho: "Chinese", ko: "Korean", kor: "Korean", hi: "Hindi", hin: "Hindi",
        ar: "Arabic", ara: "Arabic", nl: "Dutch", dut: "Dutch", nld: "Dutch", sv: "Swedish", swe: "Swedish",
        no: "Norwegian", nor: "Norwegian", da: "Danish", dan: "Danish", fi: "Finnish", fin: "Finnish",
        pl: "Polish", pol: "Polish", tr: "Turkish", tur: "Turkish", el: "Greek", gre: "Greek", ell: "Greek",
        he: "Hebrew", heb: "Hebrew", th: "Thai", tha: "Thai", vi: "Vietnamese", vie: "Vietnamese",
        id: "Indonesian", ind: "Indonesian", cs: "Czech", cze: "Czech", ces: "Czech", hu: "Hungarian",
        hun: "Hungarian", ro: "Romanian", rum: "Romanian", ron: "Romanian", uk: "Ukrainian", ukr: "Ukrainian",
        ta: "Tamil", tam: "Tamil", te: "Telugu", tel: "Telugu", bn: "Bengali", ben: "Bengali",
        ml: "Malayalam", mal: "Malayalam", mr: "Marathi", mar: "Marathi", ur: "Urdu", urd: "Urdu",
        und: ""
    })
    readonly property var codecNames: ({
        subrip: "SRT", srt: "SRT", ass: "ASS", ssa: "SSA", webvtt: "WebVTT", mov_text: "Text",
        hdmv_pgs_subtitle: "PGS", dvd_subtitle: "VobSub", dvb_subtitle: "DVB", text: "Text",
        aac: "AAC", ac3: "AC-3", eac3: "E-AC-3", dts: "DTS", truehd: "TrueHD", flac: "FLAC",
        opus: "Opus", vorbis: "Vorbis", mp3: "MP3", mp2: "MP2", alac: "ALAC"
    })
    function languageName(code) {
        if (!code) return ""
        const key = String(code).toLowerCase()
        const known = root.languageNames[key]
        return known !== undefined ? known : String(code).toUpperCase()
    }
    function codecName(codec) {
        if (!codec) return ""
        const key = String(codec).toLowerCase()
        if (key.indexOf("pcm") === 0) return "PCM"
        return root.codecNames[key] || String(codec).toUpperCase()
    }

    // The one clock format (2026-11-05): minutes are padded only when there
    // are hours, so "0:00", "2:00", "12:34" and "1:02:03".
    function clock(seconds) {
        const t = Math.max(0, Math.floor(Number(seconds) || 0))
        const s = String(t % 60).padStart(2, "0")
        if (t >= 3600)
            return Math.floor(t / 3600) + ":" + String(Math.floor(t / 60) % 60).padStart(2, "0") + ":" + s
        return Math.floor(t / 60) + ":" + s
    }

    // Action names as menus and the shortcut list show them. input.conf
    // carries Title Case labels ("Go to Time..."); every panel uses
    // sentence case, so the label is converted here once for both views.
    // State-neutral names for actions the context menu marks with a check.
    readonly property var actionNames: ({
        "script-message app-toggle-playlist": "Show playlist",
        "cycle sub-visibility": "Subtitles on/off",
        // One name everywhere: Quick settings, menus and the guide.
        // "…" marks an action that opens a panel or dialog.
        "script-message app-playback-info": "Playback info…",
        "script-message app-settings": "Settings…",
        "script-message app-subtitle-cycle": "Next subtitle",
        "script-message app-playback-menu": "Quick settings…",
        // Elapsed, remaining, timecode: a verb says it steps through formats.
        "script-message app-cycle-time-display": "Cycle time format",
        "script-message app-mini-player": "Mini player",
        "script-message app-window-scale 0.5": "Half size",
        "script-message app-window-scale 1": "Video size",
        "script-message app-window-scale 2": "Double size",
        // A true minus sign: a trailing hyphen reads as a cut-off label.
        "add audio-delay -0.1": "Audio delay −",
        "add audio-delay 0.1": "Audio delay +",
        "add sub-delay -0.1": "Subtitle delay −",
        "add sub-delay 0.1": "Subtitle delay +"
    })
    function sentenceCase(text) {
        if (typeof text !== "string" || text === "")
            return ""
        // Keep acronyms (URL, A-B) and units (5s); lower the rest after
        // the first word, including the half after a slash ("Play/Pause").
        const tokens = text.replace(/\.\.\.$/, "…").split(/([ \/])/)
        for (let i = 1; i < tokens.length; ++i) {
            const word = tokens[i]
            if (word === " " || word === "/" || word === "")
                continue
            if ((word.match(/[A-Z]/g) || []).length >= 2)
                continue
            tokens[i] = word.charAt(0).toLowerCase() + word.substring(1)
        }
        return tokens.join("")
    }
    function actionLabel(command, text) {
        return root.actionNames[command] || root.sentenceCase(text)
    }

    // A humanized sequence as separate keys for KeyCaps: "Ctrl+PgUp" ->
    // ["Ctrl", "PgUp"]. A literal plus key ("Ctrl++" or "+") stays a key.
    function split(sequence) {
        if (typeof sequence !== "string" || sequence === "")
            return []
        if (sequence === "+")
            return ["+"]
        const plusKey = sequence.endsWith("++")
        const body = plusKey ? sequence.slice(0, -2) : sequence
        const keys = body.split("+").filter(k => k !== "")
        if (plusKey)
            keys.push("+")
        return keys
    }
}
