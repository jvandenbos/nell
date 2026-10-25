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
            if (reader.page === view.lastPage)
                return
            view.turnDirection = reader.page > view.lastPage ? 1 : -1
            view.lastPage = reader.page
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
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["Right", "PgDown", "Space", "J", "Down"]; enabled: view.pageKeys; onActivated: reader.next() }
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["Left", "PgUp", "Shift+Space", "K", "Up"]; enabled: view.pageKeys; onActivated: reader.previous() }
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
