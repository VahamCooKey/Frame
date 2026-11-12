import QtQuick
import QtQuick.Effects
import QtQuick.Window

Rectangle {
    id: root
    objectName: "playlistOverlay"
    readonly property bool playlistControl: true
    signal interaction()
    signal showHudRequested(string text)
    property bool shown: false
    property bool sidebar: false
    readonly property bool interacting: shown && (shelfHover.hovered || actions.open || list.moving || draggingCard || searchInput.activeFocus)
    property bool playlistRepeat: Settings.flag("general/playlistRepeat", false)
    property var titleTipOwner: null
    property bool titleTipReady: false
    readonly property bool cleanupActive: MediaNames.cleanupEnabled
    property string previousKey: ""
    property string nextKey: ""
    property bool searchShown: false
    property string focusedId: ""
    property bool restoreCardFocus: false
    property bool draggingCard: false
    property string draggedId: ""
    property string dropTargetId: ""
    property bool dropAfter: false
    function showSearch() {
        actions.open = false
        searchShown = true
        Qt.callLater(function() { searchInput.forceActiveFocus(); searchInput.selectAll() })
        interaction()
    }
    function clearSearch() {
        searchInput.text = ""
        Playlist.query = ""
        searchShown = false
        Qt.callLater(function() { focusCard(Math.max(0, Playlist.selectedRow)) })
    }
    function moveSelected(direction) {
        actions.open = false
        restoreCardFocus = true
        if (Playlist.moveBy(Playlist.selectedId, direction)) interaction()
        else { restoreCardFocus = false; focusCard(Playlist.selectedRow) }
    }
    function endCardDrag() {
        draggingCard = false
        draggedId = ""
        dropTargetId = ""
        interaction()
    }
    function updateDropTarget() {
        const point = root.mapToItem(list.contentItem, dragToken.x, dragToken.y)
        const row = list.indexAt(point.x, point.y)
        const card = row >= 0 ? list.itemAtIndex(row) : null
        if (!card || card.entryId === draggedId) { dropTargetId = ""; return }
        const local = root.mapToItem(card, dragToken.x, dragToken.y)
        dropTargetId = card.entryId
        dropAfter = sidebar ? local.y > card.height / 2 : local.x > card.width / 2
    }
    function dropCard() {
        if (draggedId && dropTargetId) {
            restoreCardFocus = true
            if (!Playlist.moveRelative(draggedId, dropTargetId, dropAfter)) restoreCardFocus = false
        }
        endCardDrag()
    }
    function refreshShortcutHints() {
        previousKey = Shortcuts.keyForCommand("playlist-prev")
        nextKey = Shortcuts.keyForCommand("playlist-next")
    }
    function toggleTitles() {
        MediaNames.cleanupEnabled = !MediaNames.cleanupEnabled
        Settings.setValue("interface/cleanMediaNames", MediaNames.cleanupEnabled)
        interaction()
    }
    readonly property bool compact: width < 680 || height < 208
    readonly property bool hintsInHeader: !sidebar && width >= 760 && headerNav.width < width - 300
    // Theater dock: one 16 px inset for header, cards, divider and footer.
    readonly property int sidePadding: sidebar ? 16 : (compact ? 16 : 20)
    readonly property int titleHeight: sidebar ? 20 : (compact ? 18 : 20)
    // Dock cards sit inside a slot 4 px larger on every side, so the hover
    // backdrop is not clipped by the list. The floating shelf has no slot.
    readonly property int slotPad: sidebar ? 4 : 0
    // Dock rhythm, fixed: thumbnail, 6 px, 20 px title row, then 22 px to
    // the next thumbnail (4 px slot pad + 14 px spacing + 4 px slot pad).
    readonly property int dockSpacing: 22 - 2 * slotPad
    // Dock edge fades: 8 px of solid panel colour, then a 20 px gradient.
    readonly property int dockFade: 28
    // Dock lead: an empty band at the top of the list content, so every
    // whole-card stop leaves the fade over the gap above the card (never
    // over the card itself) and the first slot sits 12 px under the header.
    readonly property int dockLead: sidebar ? dockFade - slotPad : 0
    readonly property real headerBottom: header.height
    // Horizontal shelf: as many cards as fit at the height-derived size,
    // then widened to fill the row exactly, so both gutters match.
    readonly property real availWidth: width - sidePadding * 2
    readonly property real fitWidth: Math.max(128, Math.min(240, Math.floor((list.height - titleHeight - 8) * 16 / 9)))
    readonly property int perRow: Math.max(1, Math.min(Math.max(1, list.count),
        Math.ceil((availWidth + 14) / (fitWidth + 14))))
    // Theater dock: full-width cards at a fixed 16 px pitch gap; the list
    // fills the dock and ends flush with the last card. Wheel and page
    // stops move by whole pitches; only the clamped end stop may leave the
    // top card partly scrolled.
    readonly property real cardWidth: sidebar ? availWidth
        : Math.max(128, Math.min(fitWidth, Math.floor((availWidth - (perRow - 1) * 14) / perRow)))
    // Dock: the title row sits 6 px under its thumbnail.
    readonly property real cardHeight: sidebar ? cardWidth * 9 / 16 + titleHeight + 6 : list.height
    readonly property real pitch: itemExtent + list.spacing
    // Dock: the list fills the dock from header to footer. Scroll stops
    // put a whole card at the top (its thumbnail 20 px under the header);
    // whatever part of the next card fits shows as a natural peek under
    // the bottom fade. The end stop clamps to the content end, so the last
    // title row sits on the list bottom.
    // Whole cards (title row included) that fit below a top stop.
    readonly property int dockCards: Math.max(1, Math.floor((list.room - dockLead - stopLift + list.spacing) / pitch))
    // Stops sit on card tops. A 20 px lift that let the previous title row
    // peek under the top fade read as clipped letters (theater review
    // round 9); the resting scrollbar is the "more above" cue instead.
    readonly property int stopLift: 0
    function stopOffset(index) {
        return scrollOrigin + (index <= 0 ? 0 : index * pitch - stopLift)
    }
    function stopIndex(offset) {
        return Math.round((offset - scrollOrigin + stopLift) / pitch)
    }
    // Dock edge fades follow what the viewport edge actually cuts. Within a
    // card slot: slotPad, thumbnail, 6 px, then the title row, whose glyphs
    // end 18 px down (the last 2 px are empty).
    readonly property real dockThumbHeight: cardWidth * 9 / 16
    readonly property real dockTitleClear: slotPad + dockThumbHeight + 6 + 18
    // Position of a content y (from the top of the lead band) inside its
    // card slot, or -1 in the lead band or past the last card.
    function dockSlotOffset(y) {
        const r = y - dockLead
        if (r < 0) return -1
        const i = Math.floor(r / pitch)
        return i >= list.count ? -1 : r - i * pitch
    }
    // Bottom fade: 0 when the viewport ends in a gap, after a title's
    // glyphs, or at the content end; it ramps to 1 as more than 8 px of a
    // card (thumbnail or title) falls below the edge.
    readonly property real dockBottomFade: {
        if (!sidebar || !overflowing) return 0
        const bottom = scrollOffset - scrollOrigin + viewExtent
        if (bottom >= contentExtent - 1) return 0
        const local = dockSlotOffset(bottom)
        if (local < slotPad || local >= dockTitleClear) return 0
        return Math.max(0, Math.min(1, (dockTitleClear - local) / 8))
    }
    // Top fade: the same rule for a card cut by the top edge.
    readonly property real dockTopFade: {
        if (!sidebar || !overflowing) return 0
        const top = scrollOffset - scrollOrigin
        if (top <= 1) return 0
        const local = dockSlotOffset(top)
        if (local < slotPad || local >= dockTitleClear) return 0
        return Math.max(0, Math.min(1, (local - slotPad) / 8))
    }
    readonly property bool overflowing: contentExtent > viewExtent + 1
    // Whole cards in view and the first of them, for the "· 2–5" cue.
    readonly property int visibleCount: Math.max(1, Math.floor((viewExtent + list.spacing) / pitch))
    readonly property int firstVisible: Math.max(0, Math.min(list.count - visibleCount,
                                                             Math.round((scrollOffset - scrollOrigin) / pitch)))
    readonly property bool overflowingRow: list.count * cardWidth + (list.count - 1) * 14 > availWidth
    readonly property real itemExtent: (sidebar ? cardHeight : cardWidth) + 2 * slotPad
    readonly property real viewExtent: sidebar ? list.height : list.width
    readonly property real scrollOffset: sidebar ? list.contentY : list.contentX
    readonly property real scrollOrigin: sidebar ? list.originY : list.originX
    readonly property real contentExtent: sidebar ? list.contentHeight : list.contentWidth
    function setScrollOffset(value) {
        if (sidebar) list.contentY = value
        else list.contentX = value
    }
    onSidebarChanged: { pageScroll.stop(); Qt.callLater(revealCurrent) }
    readonly property int animMs: Native.animationsEnabled() ? 150 : 0
    property string pagingMotion: Settings.value("interface/playlistMotion", "system")
    readonly property bool animatePaging: pagingMotion === "on"
        || (pagingMotion === "system" && Native.animationsEnabled())
    // Floating shelf: the shared panel radius. Docked in theater: flush
    // with the window colour and a single hairline on its left edge.
    // One opaque surface for the floating shelf and the theater dock.
    readonly property color shelfColor: Colors.panel
    color: shelfColor
    radius: sidebar ? 0 : 12
    // Floating shelf matches the other popovers; the docked variant keeps
    // its single left hairline (a full border would double at the window edge).
    border.color: Colors.menuBorder
    border.width: sidebar ? 0 : 1
    opacity: shown ? 1 : 0
    visible: shown || opacity > 0.01
    enabled: shown
    Behavior on opacity { NumberAnimation { duration: Native.animationsEnabled() ? (root.shown ? 140 : 180) : 0; easing.type: Easing.OutQuad } }
    function baseName(path) {
        const parts = String(path || "").split(/[\\/]/)
        return parts[parts.length - 1]
    }
    function hideTitleTip() {
        titleTipTimer.stop()
        titleTipReady = false
        titleTipOwner = null
    }
    function scheduleTitleTip(card) {
        hideTitleTip()
        if (card.titleTruncated) {
            titleTipOwner = card
            titleTipTimer.restart()
        }
    }
    function revealCurrent() {
        pageScroll.stop()
        list.forceLayout()
        const currentRow = Playlist.rowForId(Playlist.currentId || Playlist.selectedId)
        list.currentIndex = Playlist.selectedRow
        if (currentRow >= 0 && currentRow < list.count) {
            const count = sidebar ? dockCards
                : Math.max(1, Math.floor((viewExtent + list.spacing) / (itemExtent + list.spacing)))
            // Keep one card after the current one ("Up next") in view.
            const first = Math.min(Math.max(0, currentRow - count + 2), Math.max(0, list.count - count))
            if (sidebar) {
                // Past the start, open on a peek stop (previous title row
                // under the top fade) rather than the clamped end, as long
                // as the current card stays whole.
                let stop = first
                const maxOffset = scrollOrigin + Math.max(0, contentExtent - viewExtent)
                if (stop > 0 && stopOffset(stop) > maxOffset + 0.5) {
                    const alt = Math.floor((maxOffset - scrollOrigin + stopLift) / pitch)
                    const currentBottom = dockLead + currentRow * pitch + itemExtent
                                          - (stopOffset(alt) - scrollOrigin)
                    if (alt > 0 && currentBottom <= viewExtent + 0.5) stop = alt
                }
                scrollTo(stopOffset(stop), 0)
            } else list.positionViewAtIndex(first, ListView.Beginning)
        }
        updatePosterPriority()
    }
    function focusCard(index) {
        if (!list.count) return
        pageScroll.stop()
        list.cancelFlick()
        list.currentIndex = Math.max(0, Math.min(list.count - 1, index))
        Playlist.selectedId = Playlist.idAt(list.currentIndex)
        ensureVisible(list.currentIndex)
        list.forceLayout()
        const card = list.itemAtIndex(list.currentIndex)
        if (card) card.forceActiveFocus(Qt.TabFocusReason)
        root.interaction()
    }
    // Bring a card fully into view. The dock lands on whole-card stops and
    // keeps the card clear of both edge fades.
    function ensureVisible(index) {
        if (!sidebar) { list.positionViewAtIndex(index, ListView.Contain); return }
        if (!overflowing) return
        // Card slot edges in list coordinates; the slot must clear the top
        // fade band (dockLead) and end inside the list.
        const offset = scrollOffset - scrollOrigin
        const top = index * pitch
        const bottom = dockLead + index * pitch + itemExtent
        if (top < offset - 0.5) scrollTo(stopOffset(index), 0)
        else if (bottom > offset + viewExtent + 0.5)
            scrollTo(stopOffset(Math.ceil((bottom - viewExtent + stopLift) / pitch - 0.001)), 0)
    }
    onCardWidthChanged: if (shown) Qt.callLater(revealCurrent)
    function updatePosterPriority() {
        const first = Math.max(0, Math.floor((scrollOffset - scrollOrigin) / (itemExtent + list.spacing)))
        const files = []
        for (let i = first; i < Math.min(Playlist.count, first + Math.ceil(viewExtent / (itemExtent + list.spacing)) + 1); ++i)
            files.push(Playlist.entryForId(Playlist.idAt(i)).filename)
        // Scrolling inside the same card does not change artwork priority.
        // Avoid restarting background work on every pixel of a flick.
        if (files.length !== posters.visibleFiles.length
                || files.some((file, index) => file !== posters.visibleFiles[index]))
            posters.visibleFiles = files
    }
    function page(direction) {
        // Repeated clicks advance from the pending destination, without
        // snapping the shelf to it or competing with a pointer flick.
        const from = pageScroll.running ? pageScroll.to : scrollOffset
        const first = Math.max(0, stopIndex(from))
        const step = Math.max(1, Math.floor((viewExtent + list.spacing) / (itemExtent + list.spacing)))
        const index = Math.max(0, Math.min(list.count - 1, first + direction * step))
        const destination = Math.min(scrollOrigin + Math.max(0, contentExtent - viewExtent), stopOffset(index))
        list.cancelFlick()
        scrollTo(destination, 260)
        interaction()
    }
    NumberAnimation {
        id: pageScroll
        target: list
        property: root.sidebar ? "contentY" : "contentX"
        duration: 260
        easing.type: Easing.OutCubic
    }
    // One wheel path for the whole shelf. The ListView's own wheel handling
    // flicked past a card and SnapToItem then pulled it back, which read as
    // a spring at both ends. Mouse wheels now glide one card per notch to a
    // clamped card position; touchpads follow the fingers and settle on the
    // nearest card once they stop.
    property real wheelAccum: 0
    function scrollTo(destination, ms) {
        const lo = scrollOrigin
        const hi = scrollOrigin + Math.max(0, contentExtent - viewExtent)
        destination = Math.max(lo, Math.min(hi, destination))
        pageScroll.stop()
        if (ms > 0 && animatePaging && Math.abs(destination - scrollOffset) > 1) {
            pageScroll.duration = ms
            pageScroll.from = scrollOffset
            pageScroll.to = destination
            pageScroll.start()
        } else setScrollOffset(destination)
    }
    function wheelScroll(wheel) {
        list.cancelFlick()
        const pixel = sidebar ? wheel.pixelDelta.y : (wheel.pixelDelta.x || wheel.pixelDelta.y)
        if (pixel !== 0) {
            wheelAccum = 0
            scrollTo(scrollOffset - pixel, 0)
            touchpadSettle.restart()
        } else {
            wheelAccum += wheel.angleDelta.y || wheel.angleDelta.x
            const steps = wheelAccum > 0 ? Math.floor(wheelAccum / 100) : Math.ceil(wheelAccum / 100)
            if (steps !== 0) {
                wheelAccum -= steps * 100
                // Consecutive notches advance from the pending destination.
                const from = pageScroll.running ? pageScroll.to : scrollOffset
                scrollTo(stopOffset(stopIndex(from) - steps), 160)
            }
        }
        interaction()
    }
    Timer {
        id: touchpadSettle
        interval: 160
        onTriggered: root.scrollTo(root.stopOffset(root.stopIndex(root.scrollOffset)), 140)
    }
    onShownChanged: {
        if (shown) { refreshShortcutHints(); Qt.callLater(revealCurrent) }
        else { pageScroll.stop(); Shortcuts.wheelOverScroller = false; actions.open = false; hideTitleTip(); endCardDrag() }
    }
    Keys.onPressed: event => {
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_F) {
            showSearch()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape && searchShown) {
            clearSearch()
            event.accepted = true
        }
    }
    PlaylistPosterCache { id: posters; shelfShown: root.shown }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => root.wheelScroll(wheel)
    }
    HoverHandler {
        id: shelfHover
        onHoveredChanged: { Shortcuts.wheelOverScroller = hovered && root.shown; root.interaction() }
    }
    component ShelfButton: IconButton {
        width: 34
        height: 34
        iconSize: 20
        idleIconOpacity: 0.8
        tooltipEnabled: true
        onInteraction: root.interaction()
    }
    component PosterTag: Rectangle {
        property alias text: tagText.text
        property alias textColor: tagText.color
        width: tagText.implicitWidth + 12
        height: 19
        radius: 4
        color: "#DF151515"
        Text {
            id: tagText
            anchors.centerIn: parent
            color: Colors.text
            font.family: Fonts.ui
            font.pixelSize: 12
            font.weight: Font.Medium
        }
    }
    // Previous / next video in the header. Real buttons (D19): the old
    // "Previous PgUp" hint sat beside the header buttons and looked
    // clickable without being so. Same controller path as PgUp/PgDn.
    component HeaderNavButton: Rectangle {
        id: navButton
        property string label: ""
        property string keys: ""
        property string command: ""
        readonly property bool playerControl: true
        signal activated()
        width: navContent.width + 18
        height: 28
        radius: 6
        // Borderless at rest, a fill on hover: lighter than the cards.
        color: navArea.containsMouse && enabled ? Colors.hover : "transparent"
        border.width: activeFocus ? 1 : 0
        border.color: Colors.accent
        opacity: enabled ? 1 : 0.4
        activeFocusOnTab: enabled
        Accessible.role: Accessible.Button
        Accessible.name: label + " video"
        Accessible.description: KeyNames.humanize(keys)
        Accessible.onPressAction: trigger()
        Keys.onReturnPressed: trigger()
        Keys.onSpacePressed: trigger()
        function trigger() {
            if (!enabled) return
            Player.command([command])
            root.interaction()
        }
        Row {
            id: navContent
            anchors.centerIn: parent
            spacing: 8
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: navButton.label
                color: Colors.text
                font.family: Fonts.ui
                font.pixelSize: 12
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: navButton.keys !== ""
                text: KeyNames.humanize(navButton.keys)
                color: Colors.textDim
                font.family: Fonts.ui
                font.pixelSize: 12
            }
        }
        MouseArea {
            id: navArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: navButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: navButton.trigger()
        }
    }
    Item {
        id: header
        width: parent.width
        // One row in every layout: title, count, then the header buttons.
        height: root.sidebar ? 52 : (root.compact ? 40 : 44)
        Text {
            id: title
            visible: !root.searchShown || root.sidebar
            x: root.sidePadding
            anchors.verticalCenter: parent.verticalCenter
            text: "Playlist"
            color: Colors.text
            font.family: Fonts.ui
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        // The shelf shows the key as dim text, as the menu does. The dock
        // shows none; the key stays in the close tooltip.
        Text {
            id: playlistKey
            visible: !root.searchShown && !root.sidebar
            anchors.left: title.right; anchors.leftMargin: 10
            anchors.verticalCenter: title.verticalCenter
            text: KeyNames.forCommand("script-message app-toggle-playlist")
            color: Colors.textDim
            font.family: Fonts.ui
            font.pixelSize: 12
        }
        Text {
            anchors.left: playlistKey.visible ? playlistKey.right : title.right
            anchors.leftMargin: root.sidebar ? 8 : 10
            // One vertical anchor in both modes (a baseline anchor next to
            // a centre anchor warns while the mode flips).
            anchors.verticalCenter: title.verticalCenter
            // The dock shows the position ("5 of 6") beside the title; the
            // shelf shows the count and the visible range.
            visible: (root.sidebar || root.width > 620) && !root.searchShown
            width: Math.max(0, Math.min(implicitWidth, headerActions.x - x - 10))
            elide: Text.ElideRight
            text: root.sidebar
                ? (Player.playlistPos >= 0 && Player.playlistPos < Playlist.totalCount
                   ? (Player.playlistPos + 1) + " of " + Playlist.totalCount
                   : Playlist.totalCount + (Playlist.totalCount === 1 ? " video" : " videos"))
                : (Playlist.query.trim() ? Playlist.count + " of " : "")
                  + Playlist.totalCount + (Playlist.totalCount === 1 ? " video" : " videos")
                  + (root.overflowing && root.visibleCount < list.count
                     ? "  ·  " + (root.firstVisible + 1) + "–" + (root.firstVisible + root.visibleCount) : "")
            color: Colors.textDim
            font.family: Fonts.ui
            font.pixelSize: 12
        }
        Item {
            id: headerHints
            anchors.right: headerActions.left; anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            width: headerNav.width; height: 30
            visible: root.hintsInHeader && !root.searchShown
            Row {
                id: headerNav
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                HeaderNavButton {
                    label: "Previous"
                    keys: root.previousKey
                    command: "playlist-prev"
                    enabled: Playlist.totalCount > 1
                }
                HeaderNavButton {
                    label: "Next"
                    keys: root.nextKey
                    command: "playlist-next"
                    enabled: Playlist.totalCount > 1
                }
            }
        }
        Row {
            id: headerActions
            // Dock: the ··· glyph's right edge (21.2 of its 24 px box, so
            // 14.1 px into the 16 px icon) lands on the card edge, x 284.
            anchors.right: parent.right; anchors.rightMargin: root.sidebar ? 6 : 12; anchors.verticalCenter: parent.verticalCenter
            spacing: root.sidebar ? 4 : 6
            // Page by whole cards. In the header, never over the artwork.
            // Not in the dock: its list scrolls vertically, and a second
            // "previous/next" pair there would mean something different
            // from the Previous/Next video buttons below it.
            Row {
                visible: root.overflowing && !root.sidebar
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                ShelfButton {
                    width: 28; height: 28; iconSize: 16
                    rotation: root.sidebar ? 90 : 0
                    iconSource: "qrc:/assets/icons/chevron-left.svg"
                    accessibleName: "Previous playlist items"
                    enabled: root.scrollOffset > root.scrollOrigin + 1
                    onActivated: root.page(-1)
                }
                ShelfButton {
                    width: 28; height: 28; iconSize: 16
                    rotation: root.sidebar ? 90 : 0
                    iconSource: "qrc:/assets/icons/chevron-right.svg"
                    accessibleName: "Next playlist items"
                    enabled: root.scrollOffset - root.scrollOrigin + root.viewExtent < root.contentExtent - 1
                    onActivated: root.page(1)
                }
            }
            ShelfButton {
                id: optionsButton
                width: root.sidebar ? 32 : 28; height: width; iconSize: 16
                hoverColor: root.sidebar ? Colors.hover : "#22FFFFFF"
                tooltipEnabled: !root.sidebar
                iconSource: "qrc:/assets/icons/more.svg"
                accessibleName: "Playlist options"
                Timer {
                    id: optionsTipDelay
                    interval: 550
                    running: root.sidebar && optionsButton.hovered && !actions.open
                    onTriggered: optionsTip.visible = true
                }
                onHoveredChanged: if (!hovered) optionsTip.visible = false
                Rectangle {
                    id: optionsTip
                    visible: false
                    z: 50
                    x: optionsButton.width - width
                    anchors.top: parent.bottom
                    anchors.topMargin: 6
                    width: optionsTipText.implicitWidth + 20
                    height: 30
                    radius: 6
                    color: Colors.raised
                    border.color: Colors.border
                    Text {
                        id: optionsTipText
                        anchors.centerIn: parent
                        text: "Playlist options"
                        color: Colors.text
                        font.family: Fonts.ui
                        font.pixelSize: 12
                    }
                }
                active: actions.open
                onActivated: {
                    optionsTip.visible = false
                    actions.open = !actions.open
                    if (actions.open) Qt.callLater(actions.focusFirst)
                }
            }
            // Close: floating shelf only. The theater dock sits right under
            // the window's own close button, so it closes with P or the
            // band's playlist button instead, and ··· takes the edge.
            ShelfButton {
                visible: !root.sidebar
                width: 28; height: 28; iconSize: 14
                iconSource: "qrc:/assets/icons/close.svg"
                accessibleName: "Close playlist"
                shortcutCommand: "script-message app-toggle-playlist"
                onActivated: root.shown = false
            }
        }
    }
    Rectangle {
        id: searchBar
        x: root.sidePadding
        y: root.sidebar ? header.height : 6
        width: parent.width - root.sidePadding * 2 - (root.sidebar ? 0 : 88)
        height: root.searchShown ? 32 : 0
        visible: root.searchShown
        radius: 5
        color: Colors.raised
        border.color: searchInput.activeFocus ? Colors.fieldFocus : Colors.border
        TextInput {
            id: searchInput
            objectName: "playlistSearch"
            readonly property bool playlistControl: true
            x: 10; width: parent.width - 44; anchors.verticalCenter: parent.verticalCenter
            font.family: Fonts.ui; font.pixelSize: 12; color: Colors.text
            selectionColor: Colors.controlSelected
            selectByMouse: true
            clip: true
            Accessible.name: "Find in playlist"
            onTextEdited: { Playlist.query = text; root.interaction() }
            onActiveFocusChanged: {
                Shortcuts.textInputActive = activeFocus
                if (activeFocus) {
                    root.focusedId = ""
                    root.restoreCardFocus = false
                }
            }
            Keys.onEscapePressed: root.clearSearch()
            Keys.onReturnPressed: root.focusCard(Math.max(0, Playlist.selectedRow))
            Keys.onDownPressed: root.focusCard(Math.max(0, Playlist.selectedRow))
            Text {
                anchors.fill: parent
                visible: !searchInput.text
                text: "Find in playlist"
                color: Colors.textDim; font: searchInput.font
            }
        }
        ShelfButton {
            width: 28; height: 28; iconSize: 14
            anchors.right: parent.right; anchors.rightMargin: 2; anchors.verticalCenter: parent.verticalCenter
            iconSource: "qrc:/assets/icons/close.svg"
            accessibleName: "Clear playlist search"
            onActivated: root.clearSearch()
        }
    }
    ListView {
        id: list
        objectName: "playlistShelf"
        // Dock: the card slots start 4 px outside the 16 px inset.
        x: root.sidebar ? root.sidePadding - root.slotPad : root.sidePadding + (root.availWidth - width) / 2
        // Dock: the first thumbnail sits 16 px below the header hairline
        // (its hover tile 12 px),
        // below an empty lead band that the top fade covers when scrolled.
        y: root.sidebar ? root.headerBottom + searchBar.height + 15 - root.slotPad - root.dockLead
                        : root.headerBottom + 4
        // Room left for the list: the floating Prev/Next row needs
        // 12 + 1 + 10 + row; the dock its 40 px hint line (a title row cut
        // at the list bottom goes under the bottom fade).
        readonly property real room: Math.max(0, root.height - y
            - (root.sidebar ? dockNav.height : root.hintsInHeader ? 16 : bottomNav.height + 33))
        width: root.sidebar ? root.cardWidth + 2 * root.slotPad
               : !root.overflowingRow ? root.availWidth
               : root.perRow * root.cardWidth + (root.perRow - 1) * 14
        height: room
        snapMode: ListView.SnapToItem
        // Drag flicks snap a card to the same whole-card stop as the wheel.
        preferredHighlightBegin: root.dockLead + root.stopLift
        header: root.sidebar ? dockLeadBand : null
        orientation: root.sidebar ? ListView.Vertical : ListView.Horizontal
        // A full row is justified by cardWidth. A queue that fits on one row
        // spreads its gaps (14-28 px) so the row still ends at the right
        // gutter; only a very short queue stays left-aligned.
        spacing: root.sidebar ? root.dockSpacing
                 : count > 1 && !root.overflowingRow
                 ? Math.min(28, Math.floor((root.availWidth - count * root.cardWidth) / (count - 1))) : 14
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        cacheBuffer: root.cardWidth
        // Keep the cards through the exit fade; release hidden delegates
        // only when it finishes, so an empty shelf never flashes on close.
        model: root.visible ? Playlist : null
        reuseItems: true
        onDraggingChanged: if (dragging) pageScroll.stop()
        onCountChanged: pageScroll.stop()
        onContentXChanged: { root.updatePosterPriority(); root.hideTitleTip() }
        onContentYChanged: { root.updatePosterPriority(); root.hideTitleTip() }
        onHeightChanged: if (root.shown) Qt.callLater(root.revealCurrent)
        onWidthChanged: if (root.shown) Qt.callLater(root.revealCurrent)
        delegate: Item {
            id: card
            objectName: "playlistCard_" + entryId
            required property int index
            required property var entry
            readonly property string entryId: entry.entryId
            readonly property int queueIndex: entry.queueIndex
            readonly property string file: entry.filename || ""
            readonly property bool isCurrent: entry.isCurrent === true
            readonly property bool unavailable: entry.unavailable === true
            readonly property bool isNext: Player.playlistPos >= 0 && queueIndex === Player.playlistPos + 1 && !unavailable
            readonly property string label: MediaNames.display(file, root.cleanupActive)
            readonly property bool titleTruncated: cardTitle.truncated
            readonly property string extension: {
                const path = file.indexOf("://") >= 0 ? file.split(/[?#]/)[0] : file
                const name = root.baseName(path)
                const suffix = name.substring(name.lastIndexOf(".") + 1)
                return name.indexOf(".") >= 0 && /^[a-zA-Z0-9]{1,8}$/.test(suffix) ? suffix.toUpperCase() : "VIDEO"
            }
            width: root.cardWidth + 2 * root.slotPad
            height: root.cardHeight + 2 * root.slotPad
            // Pointer over the card or its remove button.
            readonly property bool hot: cardArea.containsMouse || removeButton.hovered
            activeFocusOnTab: true
            readonly property bool playerControl: true
            readonly property bool playlistControl: true
            Keys.onReturnPressed: Playlist.play(entryId)
            Keys.onSpacePressed: Playlist.play(entryId)
            Keys.onPressed: event => {
                const earlier = event.key === Qt.Key_Left || event.key === Qt.Key_Up
                const later = event.key === Qt.Key_Right || event.key === Qt.Key_Down
                if ((earlier || later) && event.modifiers === Qt.ControlModifier) {
                    Playlist.selectedId = entryId
                    root.moveSelected(earlier ? -1 : 1)
                    event.accepted = true
                } else if ((earlier || later) && event.modifiers === Qt.NoModifier) {
                    root.focusCard(index + (earlier ? -1 : 1))
                    event.accepted = true
                } else if (event.key === Qt.Key_Menu
                           || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                    Playlist.selectedId = entryId
                    actions.open = true
                    Qt.callLater(actions.focusFirst)
                    event.accepted = true
                } else if (event.key === Qt.Key_Delete) {
                    Playlist.remove(entryId)
                    event.accepted = true
                }
            }
            onActiveFocusChanged: {
                if (activeFocus) {
                    root.focusedId = entryId
                    Playlist.selectedId = entryId
                    pageScroll.stop()
                    root.ensureVisible(index)
                    root.scheduleTitleTip(card)
                } else {
                    if (root.focusedId === entryId) root.focusedId = ""
                    if (root.titleTipOwner === card) root.hideTitleTip()
                }
            }
            onFileChanged: if (root.titleTipOwner === card) root.hideTitleTip()
            Component.onDestruction: if (root.titleTipOwner === card) root.hideTitleTip()
            Accessible.role: Accessible.Button
            Accessible.name: (unavailable ? "Unavailable: " : isCurrent ? "Now playing: " : "Play: ") + label
            Accessible.onPressAction: Playlist.play(entryId)
            opacity: root.draggingCard && root.draggedId === entryId ? 0.5 : 1
            // Dock hover: a soft backdrop 4 px outside the card (ending at
            // x 288, 3 px clear of the scrollbar), no border.
            Rectangle {
                anchors.fill: parent
                visible: root.sidebar && (card.hot || card.activeFocus)
                radius: 10
                color: Colors.hover
            }
            Rectangle {
                id: artwork
                x: root.slotPad
                y: root.slotPad
                width: root.cardWidth
                height: width * 9 / 16
                // Floating shelf: a flat tile until artwork arrives, no
                // glyph. The dock shows a faint play mark (below).
                color: Colors.tile
                radius: root.sidebar ? 8 : 6
                // Round the picture itself, not only the backing rectangle.
                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: artworkMask
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1.0
                }
                // Dock placeholder: a faint play mark that cross-fades with
                // the arriving poster.
                Image {
                    anchors.centerIn: parent
                    visible: root.sidebar && poster.opacity < 1 && !hoverCue.visible
                    width: 24; height: 24
                    source: "qrc:/assets/icons/play-outline.svg"
                    sourceSize: Qt.size(Math.ceil(24 * Screen.devicePixelRatio), Math.ceil(24 * Screen.devicePixelRatio))
                    opacity: 0.35 * (1 - poster.opacity)
                }
                // Two separate opacities: the poster's arrival fade, and the
                // dock's idle level (non-current cards sit at 0.85; hover and
                // the current card are 1.0). Kept apart so a poster never
                // arrives dimmed and then brightens.
                Item {
                    anchors.fill: parent
                    opacity: root.sidebar && !card.isCurrent && !card.hot && !card.activeFocus ? 0.85 : 1
                    Behavior on opacity { NumberAnimation { duration: Native.animationsEnabled() ? 120 : 0 } }
                    Image {
                        id: poster
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        source: posters.sourceFor(card.file)
                        sourceSize: Qt.size(400, 225)
                        asynchronous: true
                        cache: false
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity {
                            NumberAnimation { duration: root.sidebar ? (Native.animationsEnabled() ? 140 : 0) : root.animMs }
                        }
                    }
                }
                // The image carries metadata; the only content below is its title.
                Rectangle {
                    visible: !root.sidebar
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Math.min(48, parent.height / 2)
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#00000000" }
                        GradientStop { position: 1; color: "#B3000000" }
                    }
                }
                PosterTag {
                    x: 8; y: 8
                    visible: card.unavailable || card.isCurrent || card.isNext
                    text: card.unavailable ? "Unavailable" : card.isCurrent ? "Now playing" : "Up next"
                    color: "#DF151515"
                    textColor: Colors.text
                }
                // Index and format badges: floating shelf only. The dock
                // puts the format on the title line.
                PosterTag {
                    visible: !root.sidebar
                    anchors.left: parent.left; anchors.leftMargin: 8
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                    text: String(card.queueIndex + 1)
                }
                PosterTag {
                    visible: !root.sidebar
                    anchors.right: parent.right; anchors.rightMargin: 8
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                    // Quieter than the number and state: full on hover/focus.
                    opacity: cardArea.containsMouse || card.activeFocus ? 1 : 0.7
                    text: card.extension
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 3
                    color: "#40FFFFFF"
                    visible: card.isCurrent
                    Rectangle {
                        width: parent.width * (Player.duration > 0 ? Math.max(0, Math.min(1, Player.position / Player.duration)) : 0)
                        height: parent.height
                        color: Colors.accent
                    }
                }
                // Dock hover cue: a dark disc with a play mark, so the card
                // reads as "click to play". Never on the playing card.
                Rectangle {
                    id: hoverCue
                    anchors.centerIn: parent
                    width: 40; height: 40; radius: 20
                    color: "#CC000000"
                    opacity: root.sidebar && card.hot && !card.isCurrent && !root.draggingCard ? 1 : 0
                    visible: opacity > 0.01
                    Behavior on opacity { NumberAnimation { duration: Native.animationsEnabled() ? 120 : 0 } }
                    Image {
                        anchors.centerIn: parent
                        // The glyph's optical centre sits left of its box.
                        anchors.horizontalCenterOffset: 1
                        width: 18; height: 18
                        source: "qrc:/assets/icons/play-outline.svg"
                        sourceSize: Qt.size(Math.ceil(18 * Screen.devicePixelRatio), Math.ceil(18 * Screen.devicePixelRatio))
                    }
                }
                // Dock: only the current card draws a ring, the shelf's 1 px
                // #E0E0E0 inside the radius; hover never does. Keyboard focus
                // has its own ring outside the thumbnail (below).
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    radius: artwork.radius
                    visible: border.width > 0
                    border.width: root.sidebar ? (card.isCurrent ? 1 : 0)
                                  : card.activeFocus ? 2 : 1
                    border.color: root.sidebar ? "#E0E0E0"
                                  : card.activeFocus ? Colors.accent
                                  : card.isCurrent ? "#E0E0E0"
                                  : cardArea.containsMouse ? "#888888" : "#20FFFFFF"
                }
            }
            // Dock keyboard focus: a 2 px text-colour ring 3 px outside the
            // thumbnail, so focus and "now playing" never look the same.
            Rectangle {
                visible: root.sidebar && card.activeFocus
                x: artwork.x - 3; y: artwork.y - 3
                width: artwork.width + 6; height: artwork.height + 6
                radius: artwork.radius + 3
                color: "transparent"
                border.width: 2
                border.color: Colors.text
            }
            Text {
                id: cardTitle
                x: root.slotPad
                y: root.slotPad + artwork.height + (root.sidebar ? 6 : 8)
                width: root.cardWidth - (cardFormat.visible ? cardFormat.width + 10 : 0)
                height: root.titleHeight
                verticalAlignment: root.sidebar ? Text.AlignVCenter : Text.AlignTop
                text: card.label
                maximumLineCount: 1
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
                // The playing video's title leads; the rest stay quiet.
                color: root.sidebar || card.isCurrent || cardArea.containsMouse || card.activeFocus ? Colors.text : Colors.textDim
                font.family: Fonts.ui
                font.pixelSize: root.sidebar ? 14 : root.compact ? 12 : 13
                font.weight: card.isCurrent ? Font.DemiBold : root.sidebar ? Font.Normal : Font.Medium
            }
            Text {
                id: cardFormat
                visible: root.sidebar
                x: root.slotPad + root.cardWidth - width
                anchors.baseline: cardTitle.baseline
                text: card.extension
                color: Colors.textDim
                font.family: Fonts.ui
                font.pixelSize: 12
            }
            MouseArea {
                id: cardArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                preventStealing: true
                property bool wasDragged: false
                property point pressedPoint
                property string pressedEntryId: ""
                onPressed: mouse => {
                    wasDragged = false
                    pressedPoint = Qt.point(mouse.x, mouse.y)
                    pressedEntryId = card.entryId
                    Playlist.selectedId = card.entryId
                    const point = mapToItem(root, mouse.x, mouse.y)
                    dragToken.x = point.x
                    dragToken.y = point.y
                }
                onReleased: {
                    if (root.draggingCard) root.dropCard()
                    else root.endCardDrag()
                }
                onCanceled: root.endCardDrag()
                onClicked: mouse => {
                    if (wasDragged) return
                    if (mouse.button === Qt.RightButton) {
                        actions.open = true
                        Qt.callLater(actions.focusFirst)
                    } else Playlist.play(card.entryId)
                    root.interaction()
                }
                onPositionChanged: mouse => {
                    root.interaction()
                    if (!(pressedButtons & Qt.LeftButton)) return
                    if (!wasDragged && Math.hypot(mouse.x - pressedPoint.x, mouse.y - pressedPoint.y)
                        >= Qt.styleHints.startDragDistance) {
                        wasDragged = true
                        pageScroll.stop()
                        root.hideTitleTip()
                        root.draggedId = pressedEntryId
                        root.draggingCard = true
                    }
                    if (root.draggingCard) {
                        const point = mapToItem(root, mouse.x, mouse.y)
                        dragToken.x = point.x
                        dragToken.y = point.y
                        root.updateDropTarget()
                    }
                }
                onContainsMouseChanged: {
                    root.interaction()
                    if (containsMouse) root.scheduleTitleTip(card)
                    else if (root.titleTipOwner === card) root.hideTitleTip()
                }
            }
            Rectangle {
                visible: root.draggingCard && root.dropTargetId === card.entryId
                x: root.sidebar ? 0 : (root.dropAfter ? parent.width - 2 : 0)
                y: root.sidebar && root.dropAfter ? parent.height - 2 : 0
                width: root.sidebar ? parent.width : 2
                height: root.sidebar ? 2 : parent.height
                color: Colors.text
            }
            ShelfButton {
                id: removeButton
                // Dock: a 28 px target around a 24 px dark disc (6 px inside
                // the thumbnail corner), glyph 14 px at 0.9 (1.0 on hover).
                anchors.right: parent.right; anchors.rightMargin: 4 + root.slotPad
                y: 4 + root.slotPad
                width: root.sidebar ? 28 : 30; height: width
                iconSize: root.sidebar ? 14 : 20
                radius: root.sidebar ? 14 : 6
                idleIconOpacity: root.sidebar ? 0.9 : 0.8
                color: root.sidebar ? "transparent"
                       : hovered || activeFocus ? "#3B3B3B" : "#E0151515"
                Rectangle {
                    z: -1
                    visible: root.sidebar
                    anchors.centerIn: parent
                    width: 24; height: 24; radius: 12
                    color: "#CC000000"
                }
                iconSource: "qrc:/assets/icons/close.svg"
                accessibleName: "Remove " + card.label + " from playlist"
                visible: cardArea.containsMouse || hovered || card.activeFocus || activeFocus
                onActivated: Playlist.remove(card.entryId)
            }
        }
    }
    Component {
        id: dockLeadBand
        Item { width: 1; height: root.dockLead }
    }
    MouseArea {
        objectName: "playlistWheel"
        x: list.x; y: list.y; width: list.width; height: list.height
        acceptedButtons: Qt.NoButton
        onWheel: wheel => root.wheelScroll(wheel)
    }
    // Rounded-corner mask shared by every card thumbnail.
    Rectangle {
        id: artworkMask
        width: root.cardWidth; height: root.cardWidth * 9 / 16
        radius: root.sidebar ? 8 : 6
        visible: false
        layer.enabled: true
    }
    // Edge fades in the shelf colour. Floating shelf: only while the list
    // moves (at rest the cue is the "· 2–5" range). Dock: a 28 px fade at
    // each edge that has more content beyond it. Never take input.
    Rectangle {
        id: fadeStart
        // Dock: as strong as a card is cut by the top edge (dockTopFade).
        // Floating shelf: only while moving, as before.
        readonly property bool wanted: root.sidebar
            ? root.scrollOffset > root.scrollOrigin + 1
            : root.scrollOffset > root.scrollOrigin + 1
              && (list.moving || pageScroll.running
                  || (Math.abs((root.scrollOffset - root.scrollOrigin) % root.pitch) > 1
                      && !list.atYEnd && !list.atXEnd))
        enabled: false
        x: root.sidebar ? list.x : list.x - 1
        y: list.y
        width: root.sidebar ? list.width : 28
        height: root.sidebar ? root.dockFade : root.cardWidth * 9 / 16
        opacity: root.sidebar ? root.dockTopFade : wanted ? 1 : 0
        visible: root.sidebar ? opacity > 0.01 : wanted
        Behavior on opacity {
            enabled: root.sidebar
            NumberAnimation { duration: Native.animationsEnabled() ? 120 : 0 }
        }
        gradient: Gradient {
            orientation: root.sidebar ? Gradient.Vertical : Gradient.Horizontal
            GradientStop { position: 0; color: Qt.rgba(root.shelfColor.r, root.shelfColor.g, root.shelfColor.b, 1) }
            GradientStop { position: root.sidebar ? 16 / root.dockFade : 0; color: Qt.rgba(root.shelfColor.r, root.shelfColor.g, root.shelfColor.b, 1) }
            GradientStop { position: 1; color: Qt.rgba(root.shelfColor.r, root.shelfColor.g, root.shelfColor.b, 0) }
        }
    }
    Rectangle {
        id: fadeEnd
        // Dock: as strong as a card is cut by the bottom edge (dockBottomFade).
        readonly property bool wanted: (root.sidebar || list.moving || pageScroll.running)
            && root.scrollOffset - root.scrollOrigin + root.viewExtent < root.contentExtent - 1
        enabled: false
        x: root.sidebar ? list.x : list.x + list.width - width + 1
        y: root.sidebar ? list.y + list.height - height : list.y
        width: root.sidebar ? list.width : 28
        height: root.sidebar ? root.dockFade : root.cardWidth * 9 / 16
        opacity: root.sidebar ? root.dockBottomFade : wanted ? 1 : 0
        visible: root.sidebar ? opacity > 0.01 : wanted
        Behavior on opacity {
            enabled: root.sidebar
            NumberAnimation { duration: Native.animationsEnabled() ? 120 : 0 }
        }
        gradient: Gradient {
            orientation: root.sidebar ? Gradient.Vertical : Gradient.Horizontal
            GradientStop { position: 0; color: Qt.rgba(root.shelfColor.r, root.shelfColor.g, root.shelfColor.b, 0) }
            GradientStop { position: root.sidebar ? 1 - 16 / root.dockFade : 1; color: Qt.rgba(root.shelfColor.r, root.shelfColor.g, root.shelfColor.b, 1) }
            GradientStop { position: 1; color: Qt.rgba(root.shelfColor.r, root.shelfColor.g, root.shelfColor.b, 1) }
        }
    }
    // The one hairline between the dock and the video column and band.
    Rectangle {
        visible: root.sidebar
        width: 1; height: parent.height
        color: Colors.divider
    }
    // Dock scroll position: a 3 px thumb whose right edge is 6 px from the
    // dock edge, confined to the list viewport (never under the header or
    // the footer). 0.6 at rest while the list overflows; full while the
    // pointer is over the dock or the list is scrolling, easing back
    // 800 ms later.
    Rectangle {
        id: dockScrollThumb
        readonly property bool awake: root.shown && (shelfHover.hovered || list.moving
            || pageScroll.running || touchpadSettle.running || root.draggingCard)
        onAwakeChanged: if (!awake) scrollLinger.restart()
        readonly property real trackTop: Math.max(list.y, header.height + searchBar.height)
        readonly property real trackHeight: Math.max(0, list.y + list.height - trackTop)
        readonly property real range: Math.max(1, root.contentExtent - root.viewExtent)
        readonly property real progress: Math.max(0, Math.min(1, (root.scrollOffset - root.scrollOrigin) / range))
        visible: root.sidebar && root.overflowing && opacity > 0.01
        opacity: awake || scrollLinger.running ? 1 : 0.6
        Behavior on opacity { NumberAnimation { duration: Native.animationsEnabled() ? 200 : 0 } }
        x: parent.width - 6 - width
        width: 3; radius: 1.5
        height: Math.min(trackHeight, Math.max(24, root.viewExtent / Math.max(1, root.contentExtent) * trackHeight))
        y: trackTop + (trackHeight - height) * progress
        color: Colors.scrollThumb
    }
    Timer { id: scrollLinger; interval: 800 }
    // Dock footer: one dim line of key hints, the way the right-click
    // menu shows its shortcuts. No buttons (the keys and the band's skip
    // buttons already do this) and no divider line.
    Item {
        id: dockNav
        objectName: "playlistDockStatus"
        visible: root.sidebar
        width: parent.width
        height: 40
        anchors.bottom: parent.bottom
        component DockHint: Row {
            property string label: ""
            property string keys: ""
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text {
                text: parent.label
                color: Colors.textDim
                font.family: Fonts.ui
                font.pixelSize: 12
            }
            Text {
                text: KeyNames.humanize(parent.keys)
                visible: parent.keys !== ""
                color: Colors.textDim
                font.family: Fonts.ui
                font.pixelSize: 12
            }
        }
        DockHint { x: root.sidePadding; label: "Previous"; keys: root.previousKey }
        DockHint {
            id: nextHint
            x: root.sidePadding + root.availWidth - width
            label: "Next"; keys: root.nextKey
        }
    }
    // Previous/next video buttons below the floating shelf when the header
    // has no room (narrow windows): the same buttons as headerNav.
    Row {
        id: bottomNav
        visible: !root.hintsInHeader && !root.sidebar
        x: root.sidePadding
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        spacing: 8
        readonly property real half: (root.availWidth - spacing) / 2
        HeaderNavButton {
            width: bottomNav.half
            label: "Previous"
            keys: root.previousKey
            command: "playlist-prev"
            enabled: Playlist.totalCount > 1
        }
        HeaderNavButton {
            width: bottomNav.half
            label: "Next"
            keys: root.nextKey
            command: "playlist-next"
            enabled: Playlist.totalCount > 1
        }
    }
    Item {
        id: actions
        property bool open: false
        onOpenChanged: Shortcuts.menuOpen = open
        function focusFirst() {
            optionScroll.contentY = 0
            const item = options.itemAt(0)
            if (item) item.forceActiveFocus(Qt.MenuBarFocusReason)
        }
        function focusOption(index) {
            const item = options.itemAt((index + options.count) % options.count)
            if (item) {
                item.forceActiveFocus(Qt.TabFocusReason)
                optionScroll.contentY = Math.max(0, Math.min(optionScroll.contentHeight - optionScroll.height,
                    item.y < optionScroll.contentY ? item.y : Math.max(optionScroll.contentY, item.y + item.height - optionScroll.height)))
            }
        }
        anchors.fill: parent
        visible: open
        z: 10
        MouseArea { anchors.fill: parent; onClicked: actions.open = false }
        // Shared compact menu spec (see ContextMenu.qml): 28 px rows,
        // 13 px labels, 12 px dim keys; switches flip in place.
        Rectangle {
            objectName: "playlistOptions"
            x: parent.width - width - 48
            y: Math.max(8 - root.mapToItem(null, 0, 0).y, Math.min(header.height - 5, root.height - height - 8))
            width: Math.min(236, root.width - 24)
            height: Math.min(optionColumn.height + 8,
                Math.max(120, (root.Window.window ? root.Window.window.height : root.height + 160) - 16))
            radius: 8
            color: Colors.surface
            border.color: Colors.border
            MouseArea { anchors.fill: parent }
            Flickable {
                id: optionScroll
                anchors.fill: parent; anchors.margins: 4
                contentHeight: optionColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                id: optionColumn
                width: parent.width
                Repeater {
                    id: options
                    model: [
                        { id: "search", label: "Find in playlist", hint: "Ctrl+F" },
                        { id: "earlier", label: "Move selected earlier", hint: "Ctrl+Up" },
                        { id: "later", label: "Move selected later", hint: "Ctrl+Down" },
                        { id: "titles", label: "Short titles", toggle: true },
                        { id: "repeat", label: "Repeat playlist", toggle: true },
                        { id: "restore", label: "Restore queue on launch", toggle: true },
                        { id: "shuffle", label: "Shuffle playlist" },
                        { id: "clear", label: "Remove other videos" }
                    ]
                    delegate: Rectangle {
                        id: menuRow
                        required property int index
                        required property var modelData
                        readonly property bool checked: modelData.id === "titles" ? root.cleanupActive
                                                      : modelData.id === "restore" ? Playlist.restoreEnabled
                                                      : modelData.id === "repeat" && root.playlistRepeat
                        readonly property bool canRun: modelData.id === "earlier" ? Playlist.selectedRow > 0
                            : modelData.id === "later" ? Playlist.selectedRow >= 0 && Playlist.selectedRow < Playlist.count - 1 : true
                        width: parent.width; height: 26; radius: 5
                        color: menuArea.containsMouse || activeFocus ? Colors.hover : "transparent"
                        activeFocusOnTab: true
                        readonly property bool playerControl: true
                        Accessible.role: modelData.toggle ? Accessible.CheckBox : Accessible.MenuItem
                        Accessible.name: modelData.label
                        Accessible.checkable: modelData.toggle === true
                        Accessible.checked: checked
                        Accessible.description: modelData.id === "restore" ? "Restore local files without starting playback" : ""
                        Accessible.onPressAction: run()
                        Keys.onReturnPressed: run()
                        Keys.onSpacePressed: run()
                        Keys.onUpPressed: actions.focusOption(index - 1)
                        Keys.onDownPressed: actions.focusOption(index + 1)
                        Keys.onEscapePressed: { actions.open = false; root.focusCard(Playlist.selectedRow) }
                        function run() {
                            if (!canRun) return
                            if (!modelData.toggle) actions.open = false
                            if (modelData.id === "search") root.showSearch()
                            else if (modelData.id === "earlier") root.moveSelected(-1)
                            else if (modelData.id === "later") root.moveSelected(1)
                            else if (modelData.id === "restore") Playlist.restoreEnabled = !Playlist.restoreEnabled
                            else if (modelData.id === "titles") {
                                root.toggleTitles()
                            } else if (modelData.id === "repeat") {
                                Player.command(["script-message", "app-toggle-playlist-repeat"])
                            } else if (modelData.id === "shuffle") {
                                Player.playlistShuffle()
                                root.showHudRequested("Playlist shuffled")
                            } else Player.playlistClear()
                        }
                        Text {
                            x: 22; anchors.verticalCenter: parent.verticalCenter
                            text: menuRow.modelData.label
                            color: Colors.text
                            opacity: menuRow.canRun ? 1 : 0.4
                            font.family: Fonts.ui; font.pixelSize: 13
                        }
                        Text {
                            anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                            visible: !!menuRow.modelData.hint
                            text: menuRow.modelData.hint || ""
                            color: Colors.textDim; font.family: Fonts.ui; font.pixelSize: 12
                        }
                        ToggleSwitch {
                            anchors.right: parent.right; anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            visible: menuRow.modelData.toggle === true
                            checked: menuRow.checked
                        }
                        MouseArea { id: menuArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: menuRow.run() }
                    }
                }
                }
            }
        }
    }
    Text {
        anchors.centerIn: list
        visible: Playlist.count === 0
        text: Playlist.totalCount > 0 ? "No matching videos.\nClear the search to see your queue."
            : "Your playlist starts here.\nOpen a video or drop files into the player."
        horizontalAlignment: Text.AlignHCenter
        lineHeight: 1.6
        color: Colors.textDim
        font.family: Fonts.ui
        font.pixelSize: 14
    }
    // A small internal drag token leaves card geometry and scrolling intact.
    Item {
        id: dragToken
        objectName: "playlistDragToken"
        width: 1; height: 1; z: 30
        Rectangle {
            x: 10; y: -32; width: dragLabel.implicitWidth + 20; height: 28; radius: 5
            visible: root.draggingCard
            color: Colors.raised; border.color: Colors.border
            Text {
                id: dragLabel
                anchors.centerIn: parent
                text: "Move video"; color: Colors.text; font.family: Fonts.ui; font.pixelSize: 12
            }
        }
    }
    Timer {
        interval: 40; repeat: true; running: root.draggingCard
        onTriggered: {
            const point = root.sidebar ? dragToken.y - list.y : dragToken.x - list.x
            const direction = point < 30 ? -1 : point > root.viewExtent - 30 ? 1 : 0
            if (!direction) return
            root.setScrollOffset(Math.max(root.scrollOrigin,
                Math.min(root.scrollOrigin + Math.max(0, root.contentExtent - root.viewExtent), root.scrollOffset + direction * 18)))
            root.updateDropTarget()
        }
    }
    Timer {
        id: titleTipTimer
        interval: 550
        onTriggered: root.titleTipReady = true
    }
    Rectangle {
        // Outside the clipped list, so a long title is readable without
        // increasing the shelf height or covering neighboring titles.
        visible: root.shown && root.titleTipOwner !== null && root.titleTipReady
        z: 20
        width: Math.min(400, root.width - 32)
        height: titleTipText.implicitHeight + 20
        x: Math.max(16, Math.min(root.width - width - 16,
            root.titleTipOwner ? root.titleTipOwner.mapToItem(root, 0, 0).x : 16))
        y: -height - 8
        radius: 8
        color: Colors.raised
        border.color: Colors.border
        Text {
            id: titleTipText
            x: 12; y: 10; width: parent.width - 24
            text: root.titleTipOwner ? root.titleTipOwner.label : ""
            color: Colors.text
            font.family: Fonts.ui
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }
    Connections {
        target: Shortcuts
        function onOverridesChanged() { root.refreshShortcutHints() }
        function onDismissMenusRequested() { actions.open = false }
    }
    Connections {
        target: Settings
        function onSettingChanged(key, value) {
            if (key === "general/playlistRepeat") root.playlistRepeat = Boolean(value)
            if (key === "interface/playlistMotion") {
                root.pagingMotion = String(value)
                if (!root.animatePaging && pageScroll.running) {
                    const destination = pageScroll.to
                    pageScroll.stop()
                    root.setScrollOffset(destination)
                }
            }
        }
    }
    Connections {
        target: Playlist
        function onModelAboutToUpdate() {
            if (root.focusedId) root.restoreCardFocus = true
        }
        function onModelUpdated() {
            Qt.callLater(function() {
                root.updatePosterPriority()
                list.currentIndex = Playlist.selectedRow
                if (root.restoreCardFocus && root.shown && Playlist.selectedRow >= 0
                    && !searchInput.activeFocus && !actions.open && !root.draggingCard)
                    root.focusCard(Playlist.selectedRow)
                root.restoreCardFocus = false
            })
        }
        function onCurrentIdChanged() { if (root.shown && !root.draggingCard) Qt.callLater(root.revealCurrent) }
        function onErrorOccurred(message) { root.showHudRequested(message) }
    }
}
