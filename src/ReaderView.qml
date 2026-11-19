import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Nell

// An open book. The page fills the window in the reading theme's color; the
// controls stay out of the way until they're wanted, as in Apple Books.
Item {
    id: view
    signal closeRequested()
    signal message(string text)
    signal failure(string text)
    // Something was deleted that Ctrl+Z (or the toast's Undo) brings back.
    signal undoable(string text)

    readonly property bool pdf: reader.format === "pdf"
    readonly property color ink: prefs.textColor
    readonly property color paper: prefs.pageColor

    // Two pages side by side when the window is wide enough to hold them.
    // (Fit text uses one, decided by the reader: a PDF's columns span the
    // same width either way.)
    readonly property int columns: prefs.spread && width >= 1000 && width > height * 1.2 && !(pdf && reader.pageCount < 2) ? 2 : 1
    readonly property real sideMargin: pdf ? 28 : [0.03, 0.07, 0.12][prefs.margins] * width + 28
    readonly property real gap: pdf ? 12 : Math.max(64, sideMargin)
    // Fit text runs a page edge to edge, so it keeps clear of the bars like a book.
    readonly property real topMargin: pdf && !reader.fitText ? 48 : 72
    readonly property real bottomMargin: pdf && !reader.fitText ? 48 : 72
    // A comfortable line is about 65 characters; past that the eye loses its place.
    readonly property real maxColumn: pdf ? 1e6 : prefs.fontSize * 34
    readonly property real columnWidth: Math.min(maxColumn, (width - 2 * sideMargin - gap * (columns - 1)) / columns)

    property bool chrome: true
    property int lastPage: 0
    property int turnDirection: 1

    // ---- Panels: one at a time, closed newest first --------------------
    // Contents, Search, Appearance, and Go To are primary: opening one
    // closes the others. Everything that can be open sits on the stack in the
    // order it opened, so Esc closes what the reader opened last.
    readonly property var primaries: [contents, searchPanel, appearance, goTo]
    property var stack: []
    readonly property bool panelOpen: stack.length > 0
    readonly property bool primaryOpen: contents.visible || searchPanel.visible || appearance.visible || goTo.visible
    // The page's own keys work only while the page has the keyboard and
    // nothing is open over it.
    readonly property bool pageKeys: pages.activeFocus && !panelOpen && !primaryOpen && !win.helpVisible
    // Selection keys also work with the selection bar up.
    readonly property bool selectionKeys: pages.activeFocus && reader.hasSelection && !win.helpVisible
                                          && !primaryOpen && !noteEditor.opened && !definition.opened
    readonly property bool chromeFocused: {
        for (let item = Window.activeFocusItem; item; item = item.parent)
            if (item === chromeLayer)
                return true
        return false
    }
    onChromeFocusedChanged: if (chromeFocused) showChrome()

    // Where focus goes back to when a panel closes: the control that opened
    // it, if it had the keyboard, or the page.
    function focusOwner() {
        const item = Window.activeFocusItem
        return item && chromeFocused ? item : pages
    }

    function openPanel(panel) {
        if (panel.opened)
            return
        panel.returnFocus = focusOwner()
        panel.open()
    }

    function togglePanel(panel) {
        panel.opened ? panel.close() : openPanel(panel)
    }

    function panelOpened(panel) {
        // On the stack first, so the panels it closes see it there and
        // leave the keyboard with it.
        stack = stack.filter(p => p !== panel).concat([panel])
        // The bars stay up with a panel, and for the usual moment after it
        // closes (panelClosed restarts the timer) rather than vanishing with it.
        chrome = true
        if (primaries.indexOf(panel) >= 0)
            for (const other of primaries)
                if (other !== panel && other.opened) {
                    // It hands its return target on, so closing the new one
                    // lands where the first one came from.
                    if (!panel.returnFocus || panel.returnFocus === pages)
                        panel.returnFocus = other.returnFocus
                    other.returnFocus = null
                    other.close()
                }
    }

    function panelClosed(panel) {
        stack = stack.filter(p => p !== panel)
        // Another panel that holds the keyboard is still open: leave it be.
        if (!stack.some(p => p.focus)) {
            const target = panel.returnFocus && panel.returnFocus.visible ? panel.returnFocus : pages
            target.forceActiveFocus()
        }
        panel.returnFocus = null
        hideChrome.restart()
    }

    function closeTopmost() {
        if (stack.length) {
            stack[stack.length - 1].close()
            return
        }
        if (reader.hasSelection) {
            reader.clearSelection()
            return
        }
        if (autoTurn) {
            autoTurn = false
            return
        }
        // From the toolbar, Esc goes back to the page before the library.
        if (chromeFocused) {
            pages.forceActiveFocus()
            return
        }
        view.closeRequested()
    }

    function showChrome() {
        chrome = true
        hideChrome.restart()
    }

    function formatMinutes(minutes) {
        if (minutes < 1)
            return qsTr("Less than a minute")
        if (minutes < 60)
            return qsTr("%n min", "", minutes)
        const hours = Math.floor(minutes / 60)
        const rest = minutes % 60
        return rest === 0 ? qsTr("%n hr", "", hours) : qsTr("%1 hr %2 min").arg(hours.toLocaleString(Qt.locale(), 'f', 0)).arg(rest.toLocaleString(Qt.locale(), 'f', 0))
    }

    // The progress corner. Five things it can say; the reader picks one from
    // its menu, or cycles them with P, and the same one shows with the
    // controls up or down.
    readonly property int infoMode: ((prefs.infoMode % 5) + 5) % 5
    readonly property var infoModes: {
        translator.language
        return [qsTr("Time left in chapter"), qsTr("Time left in book"), qsTr("Pages left in chapter"),
                pdf ? qsTr("Page number") : qsTr("Location"), qsTr("Percent read")]
    }
    readonly property string percentText: Math.round(reader.progress * 100).toLocaleString(Qt.locale(), 'f', 0) + "%"
    readonly property string info: {
        translator.language
        if (!reader.pageCount)
            return ""
        switch (infoMode) {
        case 0: return qsTr("%1 left in chapter").arg(formatMinutes(reader.minutesLeftInChapter))
        case 1: return qsTr("%1 left in book").arg(formatMinutes(reader.minutesLeftInBook))
        case 2: return reader.pagesLeftInChapter === 0 ? qsTr("Last page in chapter")
                                                       : qsTr("%n pages left in chapter", "", reader.pagesLeftInChapter)
        case 3: return pdf ? qsTr("Page %1 of %2").arg(reader.location.toLocaleString(Qt.locale(), 'f', 0)).arg(reader.locationCount.toLocaleString(Qt.locale(), 'f', 0))
                           : qsTr("Location %1 of %2").arg(reader.location.toLocaleString(Qt.locale(), 'f', 0)).arg(reader.locationCount.toLocaleString(Qt.locale(), 'f', 0))
        default: return qsTr("%1% read").arg(Math.round(reader.progress * 100).toLocaleString(Qt.locale(), 'f', 0))
        }
    }

    function pageLabel() {
        translator.language
        if (!reader.pageCount)
            return ""
        const first = reader.page + 1
        const last = Math.min(reader.page + reader.columns, reader.pageCount)
        const pages = last > first ? first.toLocaleString(Qt.locale(), 'f', 0) + "–" + last.toLocaleString(Qt.locale(), 'f', 0)
                                   : first.toLocaleString(Qt.locale(), 'f', 0)
        return qsTr("%1 of %2").arg(pages).arg(reader.pageCount.toLocaleString(Qt.locale(), 'f', 0))
    }

    function highlightSelection(color) {
        if (!reader.hasSelection)
            return
        reader.highlight(color)
        // H uses the color picked last.
        if (color !== "underline")
            prefs.highlightColor = color
        selectionMenu.keepSelection = false
        selectionMenu.close()
        reader.clearSelection()
    }

    function noteSelection() {
        if (!reader.hasSelection)
            return
        const existing = reader.selectedHighlight
        const id = existing.length ? existing : reader.highlight(prefs.highlightColor)
        selectionMenu.keepSelection = true
        selectionMenu.close()
        noteEditor.returnFocus = pages
        // A highlight made only to hang this note on goes if the note is cancelled.
        noteEditor.edit(id, existing.length === 0)
    }

    // ---- Reading ruler ----------------------------------------------------
    // A band across the line being read; the rest of the page is dimmed.
    // The pointer picks the line it's over; Down and Up step through them.
    property int rulerIndex: 0
    readonly property var lines: reader.lines
    readonly property var rulerLine: prefs.ruler && lines.length ? lines[Math.max(0, Math.min(lines.length - 1, rulerIndex))] : null
    onLinesChanged: rulerIndex = Math.max(0, Math.min(lines.length - 1, rulerIndex))

    function stepLine(direction) {
        if (!prefs.ruler || !lines.length) {
            direction > 0 ? reader.next() : reader.previous()
            return
        }
        const next = rulerIndex + direction
        if (next >= lines.length) {
            if (!reader.atEnd) {
                reader.next()
                rulerIndex = 0
            }
        } else if (next < 0) {
            if (reader.page > 0 || reader.slice > 0) {
                reader.previous()
                rulerIndex = Math.max(0, reader.lines.length - 1)
            }
        } else {
            rulerIndex = next
            rulerMoved()
        }
    }

    function lineAt(y) {
        let best = -1
        for (let i = 0; i < lines.length; ++i)
            if (y >= lines[i].y && y < lines[i].y + lines[i].height)
                return i
            else if (y >= lines[i].y)
                best = i
        return best
    }

    // ---- Auto page turn ---------------------------------------------------
    // Pages turn themselves, each after the time what's in view takes this
    // reader (the pace Nell learned from their own page turns, times a
    // factor they can nudge). Never on when a book opens. It waits while
    // anything is open over the page, text is selected, or a page is
    // pinned; not for the window losing focus, which on a tiling desktop
    // where focus follows the pointer is a nudge of the mouse. With the
    // ruler up it paces line by line instead, each line for its share of
    // the page's characters.
    property bool autoTurn: false
    readonly property bool autoTurnWaiting: !pageKeys || reader.hasSelection || reader.pinned
    // A picture or a chapter's title page still gets a moment.
    readonly property real autoTurnSeconds: Math.max(4, reader.viewSeconds / prefs.autoTurnPace)
    property real autoTurnElapsed: 0
    readonly property real autoTurnFraction: Math.min(1, autoTurnElapsed / (autoTurnSeconds * 1000))
    // About 5.3 characters to a word, spaces included; to the nearest five.
    readonly property int wordsPerMinute: Math.round(prefs.readingSpeed * prefs.autoTurnPace * 60 / 5.3 / 5) * 5
    onAutoTurnChanged: {
        autoTurnElapsed = 0
        if (autoTurn) {
            rulerMoved()
            autoTurnShown.restart()
        }
    }

    function toggleAutoTurn() {
        if (!autoTurn && reader.atEnd)
            return
        autoTurn = !autoTurn
    }

    function nudgePace(direction) {
        prefs.autoTurnPace = prefs.autoTurnPace + direction * 0.1
        autoTurnShown.restart()
    }

    // The share of the view's characters above line `index`.
    function lineStart(index) {
        let before = 0, total = 0
        for (let i = 0; i < lines.length; ++i) {
            if (i < index)
                before += lines[i].characters
            total += lines[i].characters
        }
        return total > 0 ? before / total : 0
    }

    // The ruler was put on a line by hand: the pacer carries on from there.
    function rulerMoved() {
        if (autoTurn && prefs.ruler && lines.length)
            autoTurnElapsed = lineStart(rulerIndex) * autoTurnSeconds * 1000
    }

    function autoTurnTick(elapsed) {
        autoTurnElapsed += elapsed
        if (autoTurnElapsed >= autoTurnSeconds * 1000) {
            if (reader.atEnd) {
                autoTurn = false
                return
            }
            reader.next(true)
            return
        }
        if (prefs.ruler && lines.length) {
            let index = rulerIndex
            while (index + 1 < lines.length && lineStart(index + 1) <= autoTurnFraction)
                ++index
            if (index !== rulerIndex)
                rulerIndex = index
        }
    }

    Timer {
        id: autoTurnTimer
        property real last: 0
        interval: 100
        repeat: true
        running: view.autoTurn && !view.autoTurnWaiting
        onRunningChanged: last = Date.now()
        onTriggered: {
            const now = Date.now()
            // A machine that slept doesn't wake to a run of page turns.
            view.autoTurnTick(Math.min(250, now - last))
            last = now
        }
    }
    // Its chip shows for a moment when it starts or the pace changes, and
    // whenever the controls are up.
    Timer { id: autoTurnShown; interval: 4000 }

    // ---- Page peek --------------------------------------------------------
    // ` pins the page; ` again comes back. Held down, it comes back on release.
    property real pinPressed: 0
    function pinKeyDown() {
        pinPressed = Date.now()
        if (reader.pinned)
            reader.returnToPin()
        else
            reader.pin()
    }
    function pinKeyUp() {
        if (reader.pinned && Date.now() - pinPressed > 450)
            reader.returnToPin()
        pinPressed = 0
    }

    function defineSelection() {
        if (!reader.hasSelection)
            return
        selectionMenu.keepSelection = true
        selectionMenu.close()
        definition.returnFocus = pages
        definition.lookUp(reader.selectionText)
    }

    Component.onCompleted: { lastPage = reader.page; hideChrome.restart(); pages.forceActiveFocus() }

    Connections {
        target: reader
        function onPositionChanged() {
            // A new page or a new slice of one: its time starts over.
            view.autoTurnElapsed = 0
            if (reader.page === view.lastPage)
                return
            view.turnDirection = reader.page > view.lastPage ? 1 : -1
            view.lastPage = reader.page
            // Paced, a page gone back to is read from its top again.
            view.rulerIndex = view.turnDirection > 0 || view.autoTurn ? 0 : Math.max(0, reader.lines.length - 1)
            if (!Tokens.reduceMotion)
                turn.restart()
        }
    }

    // Hides the controls after a quiet moment, but never out from under the
    // pointer, an open panel, or a control that has the keyboard: it looks
    // again later instead.
    Timer {
        id: hideChrome
        interval: 2600
        onTriggered: {
            if (view.panelOpen || topHover.hovered || bottomHover.hovered || view.chromeFocused)
                restart()
            else
                view.chrome = false
        }
    }

    // ---- Keys -----------------------------------------------------------
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["Right", "PgDown", "Space"]; enabled: view.pageKeys; onActivated: reader.next() }
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["Left", "PgUp", "Shift+Space"]; enabled: view.pageKeys; onActivated: reader.previous() }
    // With the ruler up, Down and Up move it a line at a time and turn the
    // page at its ends; without it they turn pages.
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["J", "Down"]; enabled: view.pageKeys; onActivated: view.stepLine(1) }
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["K", "Up"]; enabled: view.pageKeys; onActivated: view.stepLine(-1) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "R"; enabled: view.pageKeys; onActivated: prefs.ruler = !prefs.ruler }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "]"; enabled: view.pageKeys; onActivated: reader.goToChapterStart(1) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "["; enabled: view.pageKeys; onActivated: reader.goToChapterStart(-1) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Home"; enabled: view.pageKeys; onActivated: reader.goToPage(0) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "End"; enabled: view.pageKeys; onActivated: reader.goToEnd() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "T"; enabled: view.pageKeys; onActivated: view.openPanel(contents) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "/"; enabled: view.pageKeys; onActivated: view.openPanel(searchPanel) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: StandardKey.Find; enabled: !noteEditor.opened && !win.helpVisible; onActivated: view.openPanel(searchPanel) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "A"; enabled: view.pageKeys; onActivated: view.openPanel(appearance) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Ctrl+G"; enabled: !noteEditor.opened && !win.helpVisible; onActivated: view.openPanel(goTo) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "P"; enabled: view.pageKeys; onActivated: prefs.infoMode = (view.infoMode + 1) % 5 }
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["Ctrl+=", "Ctrl++"]; enabled: (!view.pdf || reader.fitText) && !win.textEntry; onActivated: prefs.fontSize += 1 }
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["Ctrl+-"]; enabled: (!view.pdf || reader.fitText) && !win.textEntry; onActivated: prefs.fontSize -= 1 }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "B"; enabled: view.pageKeys; onActivated: reader.toggleBookmark() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "G"; enabled: view.pageKeys; onActivated: view.toggleAutoTurn() }
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["+", "="]; enabled: view.pageKeys && view.autoTurn; onActivated: view.nudgePace(1) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "-"; enabled: view.pageKeys && view.autoTurn; onActivated: view.nudgePace(-1) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "H"; enabled: view.selectionKeys; onActivated: view.highlightSelection(prefs.highlightColor) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "N"; enabled: view.selectionKeys; onActivated: view.noteSelection() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "D"; enabled: view.selectionKeys; onActivated: view.defineSelection() }
    Shortcut { context: Qt.ApplicationShortcut; sequences: [StandardKey.Copy]; enabled: view.selectionKeys; onActivated: { reader.copySelection(); view.message(qsTr("Copied")) } }
    Shortcut { context: Qt.ApplicationShortcut; sequences: [StandardKey.Undo]; enabled: pages.activeFocus && !view.primaryOpen; onActivated: reader.undo() }
    Shortcut { context: Qt.ApplicationShortcut; sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; enabled: pages.activeFocus && !view.primaryOpen; onActivated: reader.redo() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Esc"; enabled: !win.helpVisible; onActivated: view.closeTopmost() }

    // ---- Page -----------------------------------------------------------
    Rectangle { anchors.fill: parent; color: view.paper; Behavior on color { ColorAnimation { duration: Tokens.duration(200) } } }

    // Clicks in the margins turn pages too.
    MouseArea {
        anchors.fill: parent
        onClicked: mouse => {
            pages.forceActiveFocus()
            mouse.x < view.width / 2 ? reader.previous() : reader.next()
        }
    }

    // The fold between two pages, barely there.
    Rectangle {
        visible: view.columns === 2 && !view.pdf
        anchors.horizontalCenter: pages.horizontalCenter
        y: pages.y - 24
        width: view.gap * 0.9
        height: pages.height + 48
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Qt.rgba(0, 0, 0, prefs.darkPage ? 0.22 : 0.06) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    WheelHandler {
        property real pending: 0
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            pending += event.angleDelta.y + event.angleDelta.x
            if (Math.abs(pending) >= 120) {
                pending < 0 ? reader.next() : reader.previous()
                pending = 0
            }
        }
    }

    PageView {
        id: pages
        // Main.qml's bare-letter shortcuts (Q, F) look for this name.
        objectName: "page"
        source: reader
        columns: view.columns
        gap: view.gap
        width: Math.floor(view.columnWidth * view.columns + view.gap * (view.columns - 1))
        height: Math.floor(view.height - view.topMargin - view.bottomMargin)
        x: Math.round((view.width - width) / 2)
        y: view.topMargin
        focus: true
        activeFocusOnTab: true
        transform: Translate { id: shift }
        Accessible.role: Accessible.Document
        Accessible.name: reader.title

        // Tab from the page brings the controls up and walks into them.
        Keys.onTabPressed: { view.showChrome(); backButton.forceActiveFocus(Qt.TabFocusReason) }
        Keys.onBacktabPressed: { view.showChrome(); scrubber.forceActiveFocus(Qt.BacktabFocusReason) }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_QuoteLeft && !event.isAutoRepeat && view.pageKeys) {
                view.pinKeyDown()
                event.accepted = true
            }
        }
        Keys.onReleased: event => {
            if (event.key === Qt.Key_QuoteLeft && !event.isAutoRepeat && view.pinPressed > 0) {
                view.pinKeyUp()
                event.accepted = true
            }
        }
        // The ruler follows the pointer to the line under it.
        HoverHandler {
            enabled: prefs.ruler
            onPointChanged: {
                const at = view.lineAt(point.position.y)
                if (at >= 0 && at !== view.rulerIndex) {
                    view.rulerIndex = at
                    view.rulerMoved()
                }
            }
        }

        onTapped: (x, y) => {
            pages.forceActiveFocus()
            if (reader.hasSelection) {
                reader.clearSelection()
                return
            }
            if (x < width * 0.28)
                reader.previous()
            else if (x > width * 0.72)
                reader.next()
            else
                view.chrome ? view.chrome = false : view.showChrome()
        }
        onSelectionFinished: { pages.forceActiveFocus(); selectionMenu.keepSelection = false; selectionMenu.open() }
        onHighlightClicked: { pages.forceActiveFocus(); selectionMenu.keepSelection = false; selectionMenu.open() }
        onExternalLink: url => Qt.openUrlExternally(url)
        onNoteClicked: (href, x, y) => { footnote.returnFocus = pages; footnote.show(href, pages.x + x, pages.y + y) }
        onImageClicked: path => { lightbox.returnFocus = pages; lightbox.show(path) }
    }

    // The ruler: the page dimmed above and below the line being read.
    Item {
        visible: view.rulerLine !== null
        x: pages.x
        y: pages.y
        width: pages.width
        height: pages.height
        readonly property real bandTop: view.rulerLine ? view.rulerLine.y - 2 : 0
        readonly property real bandBottom: view.rulerLine ? view.rulerLine.y + view.rulerLine.height + 2 : 0
        Rectangle { x: 0; y: -24; width: parent.width; height: parent.bandTop + 24; color: view.paper; opacity: 0.55 }
        Rectangle { x: 0; y: parent.bandBottom; width: parent.width; height: parent.height - parent.bandBottom + 24; color: view.paper; opacity: 0.55 }
        Rectangle {
            x: view.rulerLine ? view.rulerLine.x - 6 : 0
            y: parent.bandTop
            width: view.rulerLine ? view.rulerLine.width + 12 : 0
            height: parent.bandBottom - parent.bandTop
            color: "transparent"
            radius: 3
            border.width: 1
            border.color: Tokens.accent
            opacity: 0.7
        }
    }

    // A pinned page: the way back, until it's taken or the pin is dropped.
    AbstractButton {
        id: pinChip
        visible: reader.pinned
        anchors.horizontalCenter: parent.horizontalCenter
        y: view.height - view.bottomMargin - height - 4
        z: 1
        implicitHeight: 30
        implicitWidth: pinLabel.implicitWidth + 24
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        text: qsTr("Back to page %1").arg(reader.pinPage.toLocaleString(Qt.locale(), 'f', 0))
        Accessible.role: Accessible.Button
        Accessible.name: text
        ToolTip.visible: hovered
        ToolTip.delay: 600
        ToolTip.text: qsTr("Return to the pinned page (`)")
        onClicked: reader.returnToPin()
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        background: Rectangle {
            radius: 15
            color: pinChip.hovered ? Tokens.hover : Tokens.surfaceRaised
            border.width: 1
            border.color: Tokens.border
        }
        contentItem: Row {
            spacing: 6
            Icon { name: "bookmark"; filled: true; color: Tokens.accentText; size: 14; anchors.verticalCenter: parent.verticalCenter }
            Label { id: pinLabel; text: pinChip.text; color: Tokens.text; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
        }
    }

    // Auto page turn's chip: the pace, a nudge either way, and the way out.
    Rectangle {
        id: autoChip
        visible: view.autoTurn && (view.chrome || autoTurnShown.running || autoChipHover.hovered
                                   || (view.autoTurnWaiting && !view.panelOpen && !view.primaryOpen))
        anchors.horizontalCenter: parent.horizontalCenter
        // In the foot margin, clear of the text; above the bottom bar when
        // the controls are up, and above the pin's chip when that is.
        y: pinChip.visible ? pinChip.y - height - 6
           : view.chrome || view.panelOpen ? view.height - bottomBar.height - height - 8
           : view.height - (view.bottomMargin + height) / 2
        z: 1
        implicitHeight: 36
        implicitWidth: autoRow.implicitWidth + 12
        radius: 18
        color: Tokens.surfaceRaised
        border.width: 1
        border.color: Tokens.border
        HoverHandler { id: autoChipHover }
        Accessible.role: Accessible.Grouping
        Accessible.name: qsTr("Auto page turn")
        Row {
            id: autoRow
            anchors.centerIn: parent
            spacing: 2
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 30; implicitHeight: 30
                iconName: "minus"
                tip: qsTr("Slower (−)")
                enabled: prefs.autoTurnPace > 0.5
                opacity: enabled ? 1 : 0.4
                focusPolicy: Qt.NoFocus
                onClicked: view.nudgePace(-1)
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(implicitWidth, 132)
                horizontalAlignment: Text.AlignHCenter
                text: view.autoTurnWaiting ? qsTr("Paused") : qsTr("%1 words a minute").arg(view.wordsPerMinute.toLocaleString(Qt.locale(), 'f', 0))
                color: Tokens.text
                font.pixelSize: 12
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 30; implicitHeight: 30
                iconName: "plus"
                tip: qsTr("Faster (+)")
                enabled: prefs.autoTurnPace < 2
                opacity: enabled ? 1 : 0.4
                focusPolicy: Qt.NoFocus
                onClicked: view.nudgePace(1)
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 30; implicitHeight: 30
                iconName: "close"
                tip: qsTr("Stop turning pages (G)")
                focusPolicy: Qt.NoFocus
                onClicked: view.autoTurn = false
            }
        }
    }

    // How far through this page's time it is: a hairline along the foot of
    // the window. Left out with motion reduced; the pages still turn.
    Rectangle {
        visible: view.autoTurn && !Tokens.reduceMotion
        x: 0
        y: view.height - height
        z: 1
        height: 2
        width: view.width * view.autoTurnFraction
        color: Tokens.accent
        opacity: view.autoTurnWaiting ? 0.25 : 0.55
        // The timer ticks ten times a second; this joins the steps up.
        Behavior on width { enabled: view.autoTurnElapsed > 0; NumberAnimation { duration: 100 } }
    }

    ParallelAnimation {
        id: turn
        NumberAnimation { target: shift; property: "x"; from: view.turnDirection * 26; to: 0; duration: 200; easing.type: Easing.OutCubic }
        NumberAnimation { target: pages; property: "opacity"; from: 0.25; to: 1; duration: 200; easing.type: Easing.OutQuad }
    }

    // The outer margins turn pages on a click; a chevron shows it on hover.
    // The page itself keeps its I-beam: text can be selected right to its edge.
    component TurnZone: Item {
        id: zone
        property int direction: 1
        y: pages.y
        width: Math.max(40, direction < 0 ? pages.x : view.width - pages.x - pages.width)
        height: pages.height
        HoverHandler { id: zoneHover; cursorShape: Qt.ArrowCursor }
        IconButton {
            anchors.centerIn: parent
            width: 40
            height: 40
            iconName: zone.direction < 0 ? "back" : "forward"
            iconColor: Tokens.textSecondary
            tip: zone.direction < 0 ? qsTr("Previous page") : qsTr("Next page")
            focusPolicy: Qt.NoFocus
            opacity: zoneHover.hovered && !view.panelOpen ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Tokens.duration(120) } }
            onClicked: { pages.forceActiveFocus(); zone.direction < 0 ? reader.previous() : reader.next() }
        }
    }
    TurnZone { direction: -1; x: 0 }
    TurnZone { direction: 1; x: view.width - width }

    // A ribbon on a bookmarked page, as in Apple Books.
    Icon {
        visible: reader.bookmarked
        name: "bookmark"
        filled: true
        color: Tokens.bookmark
        size: 26
        x: pages.x + pages.width - 8
        y: 0
    }

    // ---- Progress corner -----------------------------------------------
    // A button that looks like status text until it's pointed at or focused.
    component ProgressButton: AbstractButton {
        id: progress
        implicitHeight: Math.max(32, label.implicitHeight + 8)
        implicitWidth: label.implicitWidth + leftPadding + rightPadding
        leftPadding: 8
        rightPadding: 8
        hoverEnabled: true
        text: view.info
        ToolTip.visible: hovered
        ToolTip.delay: 600
        ToolTip.text: qsTr("Reading progress — click to change")
        Accessible.role: Accessible.ButtonMenu
        Accessible.name: qsTr("Reading progress: %1").arg(view.info)
        onClicked: {
            progressMenu.returnFocus = activeFocus ? progress : pages
            progressMenu.popup(progress, 0, -progressMenu.implicitHeight - 4)
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        background: Rectangle {
            radius: Math.min(6, theme.radius)
            color: progress.hovered ? Tokens.surfaceSunken : "transparent"
            FocusRing { control: progress; controlRadius: parent.radius }
        }
        contentItem: Label {
            id: label
            text: progress.text
            color: progress.hovered ? Tokens.text : Tokens.textSecondary
            font.pixelSize: 12
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    Menu {
        id: progressMenu
        property Item returnFocus: null
        width: 248
        Material.roundedScale: theme.radius
        background: Rectangle {
            implicitWidth: 248
            radius: theme.radius
            color: Tokens.surfaceRaised
            border.width: 1
            border.color: Tokens.border
        }
        onOpened: view.panelOpened(progressMenu)
        onClosed: view.panelClosed(progressMenu)
        Repeater {
            model: view.infoModes
            MenuItem {
                required property string modelData
                required property int index
                text: modelData
                checkable: true
                checked: view.infoMode === index
                onTriggered: prefs.infoMode = index
            }
        }
    }

    // ---- Quiet header and footer ----------------------------------------
    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        y: (view.topMargin - height) / 2
        width: Math.min(implicitWidth, view.width - 360)
        visible: !view.chrome
        text: reader.chapterTitle
        color: Tokens.textSecondary
        font.pixelSize: 12
        font.letterSpacing: 0.6
        elide: Text.ElideRight
    }

    RowLayout {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Math.max(16, pages.x - 8)
        anchors.rightMargin: Math.max(24, view.width - pages.x - pages.width)
        height: view.bottomMargin
        visible: !view.chrome
        spacing: 16
        ProgressButton {
            Layout.maximumWidth: footer.width * 0.6
            // The controls come up when focus arrives, so this one stays
            // out of the Tab order; its twin in the bottom bar takes it.
            focusPolicy: Qt.NoFocus
        }
        Item { Layout.fillWidth: true }
        Label {
            visible: view.infoMode !== 4
            text: view.percentText
            color: Tokens.textSecondary
            font.pixelSize: 12
        }
    }

    // ---- Controls -------------------------------------------------------
    // The pointer near the top or bottom edge brings the controls up; moving
    // it over the page doesn't.
    HoverHandler {
        id: pointer
        onPointChanged: {
            if (view.panelOpen)
                return
            const y = point.position.y
            if (y <= 48 || y >= view.height - 48)
                view.showChrome()
        }
    }

    Item {
        id: chromeLayer
        anchors.fill: parent
        opacity: view.chrome || view.panelOpen ? 1 : 0
        // Visible as soon as it's wanted, so focus can move in before the fade.
        visible: view.chrome || view.panelOpen || opacity > 0
        Behavior on opacity { NumberAnimation { duration: Tokens.duration(180) } }

        Rectangle {
            id: topBar
            width: parent.width
            height: 56
            color: view.paper
            HoverHandler { id: topHover; onHoveredChanged: if (!hovered) hideChrome.restart() }

            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Tokens.border }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4

                IconButton { id: backButton; iconName: "back"; iconColor: view.ink; tip: qsTr("Library (Esc)"); onClicked: view.closeRequested() }
                IconButton { id: contentsButton; iconName: "contents"; iconColor: view.ink; tip: qsTr("Contents (T)"); onClicked: view.togglePanel(contents) }

                Column {
                    Layout.fillWidth: true
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: reader.title
                        color: view.ink
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: reader.chapterTitle !== reader.title ? reader.chapterTitle : reader.author
                        color: Tokens.textSecondary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }

                IconButton { id: searchButton; iconName: "search"; iconColor: view.ink; tip: qsTr("Search (/)"); onClicked: view.togglePanel(searchPanel) }
                IconButton { id: aaButton; text: "Aa"; iconColor: view.ink; tip: qsTr("Appearance (A)"); accessibleName: qsTr("Appearance"); onClicked: view.togglePanel(appearance) }
                IconButton {
                    iconName: "play"
                    iconColor: view.autoTurn ? Tokens.accentText : view.ink
                    iconFilled: view.autoTurn
                    tip: view.autoTurn ? qsTr("Stop turning pages (G)") : qsTr("Turn pages for me (G)")
                    enabled: view.autoTurn || !reader.atEnd
                    opacity: enabled ? 1 : 0.4
                    Accessible.checkable: true
                    Accessible.checked: view.autoTurn
                    onClicked: { pages.forceActiveFocus(); view.toggleAutoTurn() }
                }
                IconButton {
                    iconName: "bookmark"
                    iconColor: reader.bookmarked ? Tokens.bookmark : view.ink
                    iconFilled: reader.bookmarked
                    tip: reader.bookmarked ? qsTr("Remove Bookmark (B)") : qsTr("Bookmark (B)")
                    checkable: false
                    Accessible.checkable: true
                    Accessible.checked: reader.bookmarked
                    onClicked: reader.toggleBookmark()
                }
            }
        }

        Rectangle {
            id: bottomBar
            anchors.bottom: parent.bottom
            width: parent.width
            height: 64
            color: view.paper
            HoverHandler { id: bottomHover; onHoveredChanged: if (!hovered) hideChrome.restart() }

            Rectangle { width: parent.width; height: 1; color: Tokens.border }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 24
                spacing: 16

                ProgressButton {
                    id: progressButton
                    Layout.maximumWidth: bottomBar.width * 0.4
                    focusPolicy: Qt.TabFocus
                }

                Slider {
                    id: scrubber
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, reader.pageCount - 1)
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    value: reader.page
                    // Tab reaches it and the arrows then move a page at a
                    // time; a click leaves the keyboard with the page.
                    focusPolicy: Qt.TabFocus
                    Material.accent: view.ink
                    Accessible.name: qsTr("Position in book")
                    onMoved: reader.goToPage(Math.round(value))
                    // Arrows turn a page (or a spread) rather than nudging the
                    // value by one, which a two-page spread would snap back.
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Right || event.key === Qt.Key_Up) {
                            reader.next()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Down) {
                            reader.previous()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Home) {
                            reader.goToPage(0)
                            event.accepted = true
                        } else if (event.key === Qt.Key_End) {
                            reader.goToEnd()
                            event.accepted = true
                        }
                    }

                    ToolTip {
                        parent: scrubber.handle
                        visible: scrubber.pressed || scrubber.visualFocus
                        text: qsTr("Page %1").arg((Math.round(scrubber.value) + 1).toLocaleString(Qt.locale(), 'f', 0))
                    }
                }

                Label {
                    text: view.pageLabel()
                    color: Tokens.textSecondary
                    font.pixelSize: 12
                }
            }
        }
    }

    // ---- Panels and popups ------------------------------------------------
    ContentsPanel {
        id: contents
        property Item returnFocus: null
        height: view.height
        onOpened: view.panelOpened(contents)
        onClosed: view.panelClosed(contents)
        onExportRequested: picker.saveNotes(reader.title + " — " + qsTr("Notes") + ".md")
        onEditNoteRequested: id => {
            contents.close()
            noteEditor.returnFocus = pages
            noteEditor.edit(id, false)
        }
        onRemoved: text => view.undoable(text)
    }

    SearchPanel {
        id: searchPanel
        property Item returnFocus: null
        height: view.height
        onOpened: view.panelOpened(searchPanel)
        onClosed: view.panelClosed(searchPanel)
    }

    AppearancePanel {
        id: appearance
        property Item returnFocus: null
        y: topBar.height + 8
        maxHeight: view.height - 88
        // Placed when shown: a binding to mapToItem() wouldn't follow the button.
        onAboutToShow: x = Math.max(12, Math.min(view.width - width - 12, aaButton.mapToItem(view, 0, 0).x - width / 2 + aaButton.width / 2))
        onOpened: view.panelOpened(appearance)
        onClosed: view.panelClosed(appearance)
        pdf: view.pdf
    }

    GoToDialog {
        id: goTo
        property Item returnFocus: null
        anchors.centerIn: parent
        pdf: view.pdf
        onOpened: view.panelOpened(goTo)
        onClosed: view.panelClosed(goTo)
    }

    SelectionMenu {
        id: selectionMenu
        property bool keepSelection: false
        property Item returnFocus: null
        readonly property rect anchorRect: Qt.rect(pages.x + pages.selectionRect.x, pages.y + pages.selectionRect.y,
                                                   pages.selectionRect.width, pages.selectionRect.height)
        x: Math.max(12, Math.min(view.width - width - 12, anchorRect.x + anchorRect.width / 2 - width / 2))
        y: anchorRect.y - height - 10 > 8 ? anchorRect.y - height - 10 : anchorRect.y + anchorRect.height + 10
        onOpened: view.panelOpened(selectionMenu)
        onHighlight: color => view.highlightSelection(color)
        onNote: view.noteSelection()
        onCopy: { reader.copySelection(); keepSelection = false; close(); reader.clearSelection(); view.message(qsTr("Copied")) }
        onDefine: view.defineSelection()
        onSearch: { const text = reader.selectionText; keepSelection = false; close(); reader.clearSelection(); searchPanel.returnFocus = pages; searchPanel.searchFor(text) }
        onRemove: {
            reader.removeHighlight(reader.selectedHighlight)
            keepSelection = false
            close()
            view.undoable(qsTr("Highlight removed"))
        }
        onClosed: { view.panelClosed(selectionMenu); if (!keepSelection) reader.clearSelection() }
    }

    NoteEditor {
        id: noteEditor
        property Item returnFocus: null
        anchors.centerIn: parent
        onOpened: view.panelOpened(noteEditor)
        onClosed: { reader.clearSelection(); view.panelClosed(noteEditor) }
    }

    FootnotePopup {
        id: footnote
        property Item returnFocus: null
        onOpened: view.panelOpened(footnote)
        onClosed: view.panelClosed(footnote)
    }

    Lightbox {
        id: lightbox
        property Item returnFocus: null
        onOpened: view.panelOpened(lightbox)
        onClosed: view.panelClosed(lightbox)
    }

    DefinitionCard {
        id: definition
        property Item returnFocus: null
        x: Math.max(12, Math.min(view.width - width - 12, selectionMenu.anchorRect.x + selectionMenu.anchorRect.width / 2 - width / 2))
        y: selectionMenu.anchorRect.y + selectionMenu.anchorRect.height + 12 + height < view.height
           ? selectionMenu.anchorRect.y + selectionMenu.anchorRect.height + 12
           : Math.max(12, selectionMenu.anchorRect.y - height - 12)
        onOpened: view.panelOpened(definition)
        onClosed: { reader.clearSelection(); view.panelClosed(definition) }
        onSearchRequested: word => { close(); searchPanel.returnFocus = pages; searchPanel.searchFor(word) }
    }
}
