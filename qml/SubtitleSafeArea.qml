import QtQuick

// mpv's transient margin leaves the saved subtitle position and authored
// ASS layout alone. Unknown and image codecs are deliberately excluded.
QtObject {
    property var player
    property real viewportHeight: 0
    property real coveredHeight: 0
    readonly property bool plainTextSelected: {
        if (!player) return false
        const plainCodecs = ["subrip", "text", "mov_text", "webvtt",
                             "microdvd", "mpl2", "subviewer", "subviewer1",
                             "sami", "realtext", "jacosub", "vplayer", "pjs"]
        return player.trackList.some(function(track) {
            return track.type === "sub" && track.selected
                    && track["main-selection"] === 0
                    && plainCodecs.indexOf(track.codec) !== -1
        })
    }
    // mpv margins use a 720-high reference canvas. Logical-pixel ratios
    // remain the same at high DPI; no physical-pixel conversion is needed.
    readonly property int marginOffset: plainTextSelected && viewportHeight > 0
        ? Math.ceil(Math.max(0, coveredHeight) * 720 / viewportHeight) : 0
    // sub-margin-y-offset is newer than some system libmpv builds (Ubuntu
    // 26.04 ships 0.41, which lacks it, so the set failed silently and Linux
    // got no clearance). There, raise the normal margin from the value it had
    // before Frame touched it, and put that value back at zero. Frame never
    // saves sub-margin-y, so the user's own setting is the baseline.
    property bool offsetSupported: true
    property var baseMargin: null
    function detectOffsetSupport() {
        if (!offsetSupported || typeof player.mpvProperty !== "function")
            return
        // A live core answers for sub-margin-y; only then is a missing
        // offset property a real absence rather than mpv not running yet.
        // An absent property arrives as null or undefined.
        if (player.mpvProperty("sub-margin-y") != null
                && player.mpvProperty("sub-margin-y-offset") == null)
            offsetSupported = false
    }
    function updateMargin() {
        if (!player) return
        detectOffsetSupport()
        if (offsetSupported) {
            player.command(["set", "sub-margin-y-offset", String(marginOffset)])
            return
        }
        if (baseMargin == null) {
            if (marginOffset === 0) return
            baseMargin = player.mpvProperty("sub-margin-y")
        }
        player.command(["set", "sub-margin-y", String(baseMargin + marginOffset)])
        if (marginOffset === 0) baseMargin = null
    }
    onMarginOffsetChanged: updateMargin()
    Component.onCompleted: updateMargin()
}
