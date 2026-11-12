import QtQuick

// Independent of shelf visibility. Only a small neighborhood is prepared;
// scrolling supplies additional priority items without building a large queue.
Item {
    id: root
    property bool shelfShown: false
    property var visibleFiles: []
    property var entries: ({})
    property var order: []
    property bool warmed: false
    property bool workPending: true
    readonly property bool playbackSteady: !Player.scrubbing && (Player.state === 3 || Player.state === 4)
    readonly property bool firstPictureReady: Player.videoWidth === 0 || Player.videoPresented
    onVisibleFilesChanged: workPending = true
    onShelfShownChanged: workPending = true
    onPlaybackSteadyChanged: workPending = true
    onFirstPictureReadyChanged: workPending = true
    function sourceFor(file) { return entries[file] && entries[file].source ? entries[file].source : "" }
    function unavailable(file) { return entries[file] && entries[file].failed === true }
    function eligible(file) {
        return file && !/\.(mp3|wav|flac|aac|m4a|ogg|opus)$/i.test(file)
            && Thumbs.canPreview(file)
    }
    function forget(file) {
        if (!entries[file]) return
        const next = Object.assign({}, entries)
        delete next[file]
        entries = next
        order = order.filter(item => item !== file)
    }
    function remember(file, entry) {
        const next = Object.assign({}, entries)
        const recent = order.filter(item => item !== file)
        recent.push(file)
        next[file] = entry
        // This bounds metadata (including failures). Native code alone owns
        // image eviction and its 32-entry/8 MiB pixel budget.
        while (recent.length > 64) delete next[recent.shift()]
        order = recent
        entries = next
    }
    function prepareNext() {
        const playlist = Player.playlist
        const candidates = shelfShown ? visibleFiles.slice() : []
        const current = Math.max(0, Player.playlistPos)
        for (let offset = 0; offset < 6; ++offset) {
            if (current + offset < playlist.length) candidates.push(playlist[current + offset].filename)
        }
        for (let offset = 1; offset <= 3; ++offset)
            if (current >= offset) candidates.push(playlist[current - offset].filename)
        let unfinished = false
        for (let i = 0; i < candidates.length; ++i) {
            const file = candidates[i]
            // Eligibility and revision getters are cached observations. Ask
            // the worker to refresh them; metadataChanged resumes this queue
            // when a previously unknown/missing/replaced file is resolved.
            if (file && !/\.(mp3|wav|flac|aac|m4a|ogg|opus)$/i.test(file))
                Thumbs.refreshMetadata(file)
            if (!eligible(file)) {
                forget(file)
                continue
            }
            const key = Thumbs.posterKey(file)
            let entry = entries[file]
            // Check the latest worker revision and native eviction, including
            // completed entries. refreshMetadata above discovers replacements.
            if (entry && (entry.key !== key || (entry.source && !Thumbs.posterCached(key)))) {
                forget(file)
                entry = undefined
            }
            if (entry && (entry.pending || entry.source)) continue
            const retryDelay = Thumbs.posterRetryDelay(file)
            if (retryDelay < 0) continue
            unfinished = true
            if (retryDelay > 0) continue
            if (Thumbs.requestPoster(file)) {
                remember(file, { key: key, pending: true })
                return
            }
        }
        workPending = unfinished
    }
    Timer {
        id: settle
        interval: 1800
        onTriggered: root.warmed = true
    }
    Component.onCompleted: settle.start()
    Connections {
        target: Player
        function onFileLoaded(path) {
            root.warmed = false
            root.workPending = true
            settle.restart()
        }
        function onStateChanged() { root.workPending = true }
        function onPlaylistChanged() { root.workPending = true }
        function onPlaylistPosChanged() { root.workPending = true }
    }
    Timer {
        interval: 350
        repeat: true
        running: root.warmed && root.workPending && root.playbackSteady && root.firstPictureReady
                 && Thumbs.backgroundWorkAllowed
        onTriggered: root.prepareNext()
    }
    Timer {
        interval: 2000
        repeat: true
        running: root.warmed && root.playbackSteady && root.firstPictureReady
        onTriggered: root.workPending = true
    }
    Connections {
        target: Thumbs
        function onMetadataChanged() { root.workPending = true }
        function onPosterCancelled(file, key) {
            if (root.entries[file] && root.entries[file].key === key)
                root.forget(file)
            root.workPending = true
        }
        function onPosterEvicted(key) {
            const files = Object.keys(root.entries)
            for (let i = 0; i < files.length; ++i) {
                if (root.entries[files[i]].key === key) root.forget(files[i])
            }
            root.workPending = true
        }
        function onPosterReady(file, key, image, success) {
            // The native store is already populated. Defer binding changes
            // out of the capture-completion signal before loading the URL.
            Qt.callLater(function() {
                // A replacement/eviction can happen between signal receipt
                // and this deferred binding update. Never resurrect its URL.
                if (Thumbs.posterKey(file) !== key || (success && !Thumbs.posterCached(key))) {
                    root.forget(file)
                    root.workPending = true
                    return
                }
                root.remember(file, { key: key, source: success ? "image://poster/" + key : "", failed: !success })
                root.workPending = true
            })
        }
    }
}
