import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Nell

// An open book. The page fills the window in the reading theme's color; the
// controls stay out of the way until the pointer moves, as in Apple Books.
Item {
    id: view
    signal closeRequested()
    signal message(string text)

    readonly property bool pdf: reader.format === "pdf"
    readonly property color ink: prefs.textColor
    readonly property color paper: prefs.pageColor
    readonly property bool panelOpen: contents.opened || searchPanel.opened || appearance.opened
                                      || selectionMenu.opened || noteEditor.opened || definition.opened

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

    // The Kindle-style corner: click it to cycle what it tells you.
    readonly property string info: {
        translator.language
        if (!reader.pageCount)
            return ""
        switch (prefs.infoMode % 4) {
        case 0: return qsTr("%1 left in chapter").arg(formatMinutes(reader.minutesLeftInChapter))
        case 1: return qsTr("%1 left in book").arg(formatMinutes(reader.minutesLeftInBook))
        case 2: return reader.pagesLeftInChapter === 0 ? qsTr("Last page in chapter")
                                                       : qsTr("%n pages left in chapter", "", reader.pagesLeftInChapter)
        default: return pdf ? qsTr("Page %1 of %2").arg(reader.location.toLocaleString(Qt.locale(), 'f', 0)).arg(reader.locationCount.toLocaleString(Qt.locale(), 'f', 0))
                            : qsTr("Location %1 of %2").arg(reader.location.toLocaleString(Qt.locale(), 'f', 0)).arg(reader.locationCount.toLocaleString(Qt.locale(), 'f', 0))
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
        selectionMenu.keepSelection = false
        selectionMenu.close()
        reader.clearSelection()
    }

    function noteSelection() {
        if (!reader.hasSelection)
            return
        const id = reader.selectedHighlight.length ? reader.selectedHighlight : reader.highlight("yellow")
        selectionMenu.keepSelection = true
        selectionMenu.close()
        noteEditor.edit(id)
    }

    function defineSelection() {
        if (!reader.hasSelection)
            return
        selectionMenu.keepSelection = true
        selectionMenu.close()
        definition.lookUp(reader.selectionText)
    }

    function closeTopmost() {
        for (const popup of [definition, noteEditor, selectionMenu, appearance, searchPanel, contents])
            if (popup.opened) {
                popup.close()
                return
            }
        if (reader.hasSelection) {
            reader.clearSelection()
            return
        }
        view.closeRequested()
    }

    Component.onCompleted: { lastPage = reader.page; hideChrome.restart(); pages.forceActiveFocus() }

    Connections {
        target: reader
        function onPositionChanged() {
            if (reader.page === view.lastPage)
                return
            view.turnDirection = reader.page > view.lastPage ? 1 : -1
            view.lastPage = reader.page
            turn.restart()
        }
    }

    Timer {
        id: hideChrome
        interval: 2600
        onTriggered: if (!view.panelOpen && !topHover.hovered && !bottomHover.hovered) view.chrome = false
    }

    // ---- Keys -----------------------------------------------------------
    readonly property bool keysFree: !noteEditor.opened && !searchPanel.opened
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["Right", "PgDown", "Space", "J", "Down"]; enabled: view.keysFree; onActivated: reader.next() }
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["Left", "PgUp", "Shift+Space", "K", "Up"]; enabled: view.keysFree; onActivated: reader.previous() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "]"; enabled: view.keysFree; onActivated: reader.goToChapterStart(1) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "["; enabled: view.keysFree; onActivated: reader.goToChapterStart(-1) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Home"; enabled: view.keysFree; onActivated: reader.goToPage(0) }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "End"; enabled: view.keysFree; onActivated: reader.goToEnd() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "T"; enabled: view.keysFree; onActivated: contents.opened ? contents.close() : contents.open() }
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["/", StandardKey.Find]; enabled: !noteEditor.opened; onActivated: searchPanel.open() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "A"; enabled: view.keysFree; onActivated: appearance.opened ? appearance.close() : appearance.open() }
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["Ctrl+=", "Ctrl++"]; enabled: !view.pdf || reader.fitText; onActivated: prefs.fontSize += 1 }
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["Ctrl+-"]; enabled: !view.pdf || reader.fitText; onActivated: prefs.fontSize -= 1 }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "B"; enabled: view.keysFree; onActivated: reader.toggleBookmark() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "H"; enabled: view.keysFree && reader.hasSelection; onActivated: view.highlightSelection("yellow") }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "N"; enabled: view.keysFree && reader.hasSelection; onActivated: view.noteSelection() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "D"; enabled: view.keysFree && reader.hasSelection; onActivated: view.defineSelection() }
    Shortcut { context: Qt.ApplicationShortcut; sequences: [StandardKey.Copy]; enabled: view.keysFree && reader.hasSelection; onActivated: { reader.copySelection(); view.message(qsTr("Copied")) } }
    Shortcut { context: Qt.ApplicationShortcut; sequences: [StandardKey.Undo]; enabled: view.keysFree; onActivated: reader.undo() }
    Shortcut { context: Qt.ApplicationShortcut; sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; enabled: view.keysFree; onActivated: reader.redo() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Esc"; enabled: !win.helpVisible; onActivated: view.closeTopmost() }

    // ---- Page -----------------------------------------------------------
    Rectangle { anchors.fill: parent; color: view.paper; Behavior on color { ColorAnimation { duration: 200 } } }

    // Clicks in the margins turn pages too.
    MouseArea {
        anchors.fill: parent
        onClicked: mouse => mouse.x < view.width / 2 ? reader.previous() : reader.next()
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
        source: reader
        columns: view.columns
        gap: view.gap
        width: Math.floor(view.columnWidth * view.columns + view.gap * (view.columns - 1))
        height: Math.floor(view.height - view.topMargin - view.bottomMargin)
        x: Math.round((view.width - width) / 2)
        y: view.topMargin
        focus: true
        transform: Translate { id: shift }

        onTapped: (x, y) => {
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
        onSelectionFinished: { selectionMenu.keepSelection = false; selectionMenu.open() }
        onHighlightClicked: { selectionMenu.keepSelection = false; selectionMenu.open() }
        onExternalLink: url => Qt.openUrlExternally(url)
    }

    ParallelAnimation {
        id: turn
        NumberAnimation { target: shift; property: "x"; from: view.turnDirection * 26; to: 0; duration: 200; easing.type: Easing.OutCubic }
        NumberAnimation { target: pages; property: "opacity"; from: 0.25; to: 1; duration: 200; easing.type: Easing.OutQuad }
    }

    // A ribbon on a bookmarked page, as in Apple Books.
    Icon {
        visible: reader.bookmarked
        name: "bookmark"
        filled: true
        color: "#e5484d"
        size: 26
        x: pages.x + pages.width - 8
        y: 0
    }

    // ---- Quiet header and footer ----------------------------------------
    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        y: (view.topMargin - height) / 2
        width: Math.min(implicitWidth, view.width - 360)
        visible: !view.chrome
        text: reader.chapterTitle
        color: view.ink
        opacity: 0.45
        font.pixelSize: 12
        font.letterSpacing: 0.6
        elide: Text.ElideRight
    }

    RowLayout {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Math.max(24, pages.x)
        anchors.rightMargin: Math.max(24, view.width - pages.x - pages.width)
        height: view.bottomMargin
        visible: !view.chrome
        Label {
            text: view.info
            color: view.ink
            opacity: infoHover.hovered ? 0.8 : 0.45
            font.pixelSize: 12
            HoverHandler { id: infoHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: prefs.infoMode = (prefs.infoMode + 1) % 4 }
        }
        Item { Layout.fillWidth: true }
        Label {
            text: Math.round(reader.progress * 100).toLocaleString(Qt.locale(), 'f', 0) + "%"
            color: view.ink
            opacity: 0.45
            font.pixelSize: 12
        }
    }

    // ---- Controls -------------------------------------------------------
    HoverHandler {
        id: pointer
        onPointChanged: if (!view.panelOpen) view.showChrome()
    }

    Item {
        id: chromeLayer
        anchors.fill: parent
        opacity: view.chrome || view.panelOpen ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 180 } }

        Rectangle {
            id: topBar
            width: parent.width
            height: 56
            color: view.paper
            HoverHandler { id: topHover }

            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: view.ink; opacity: 0.08 }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 2

                IconButton { iconName: "back"; iconColor: view.ink; tip: qsTr("Library (Esc)"); onClicked: view.closeRequested() }
                IconButton { iconName: "contents"; iconColor: view.ink; tip: qsTr("Contents (T)"); onClicked: contents.open() }

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
                        color: view.ink
                        opacity: 0.6
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }

                IconButton { iconName: "search"; iconColor: view.ink; tip: qsTr("Search (/)"); onClicked: searchPanel.open() }
                IconButton { id: aaButton; text: "Aa"; iconColor: view.ink; tip: qsTr("Appearance (A)"); onClicked: appearance.opened ? appearance.close() : appearance.open() }
                IconButton { iconName: "bookmark"; iconColor: reader.bookmarked ? "#e5484d" : view.ink; iconFilled: reader.bookmarked; tip: qsTr("Bookmark (B)"); onClicked: reader.toggleBookmark() }
            }
        }

        Rectangle {
            id: bottomBar
            anchors.bottom: parent.bottom
            width: parent.width
            height: 64
            color: view.paper
            HoverHandler { id: bottomHover }

            Rectangle { width: parent.width; height: 1; color: view.ink; opacity: 0.08 }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 16

                Label {
                    Layout.preferredWidth: 200
                    text: view.info
                    color: view.ink
                    opacity: 0.7
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    TapHandler { onTapped: prefs.infoMode = (prefs.infoMode + 1) % 4 }
                }

                Slider {
                    id: scrubber
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, reader.pageCount - 1)
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    value: reader.page
                    focusPolicy: Qt.NoFocus
                    Material.accent: view.ink
                    onMoved: reader.goToPage(Math.round(value))

                    ToolTip {
                        parent: scrubber.handle
                        visible: scrubber.pressed
                        text: qsTr("Page %1").arg((Math.round(scrubber.value) + 1).toLocaleString(Qt.locale(), 'f', 0))
                    }
                }

                Label {
                    Layout.preferredWidth: 200
                    horizontalAlignment: Text.AlignRight
                    text: view.pageLabel()
                    color: view.ink
                    opacity: 0.7
                    font.pixelSize: 12
                }
            }
        }
    }

    // ---- Panels and popups ------------------------------------------------
    ContentsPanel {
        id: contents
        height: view.height
        ink: view.ink
        paper: view.paper
        onExportRequested: picker.saveNotes(reader.title + " — " + qsTr("Notes") + ".md")
    }

    SearchPanel {
        id: searchPanel
        height: view.height
        ink: view.ink
        paper: view.paper
        onClosed: pages.forceActiveFocus()
    }

    AppearancePanel {
        id: appearance
        y: topBar.height + 6
        // Placed when shown: a binding to mapToItem() wouldn't follow the button.
        onAboutToShow: x = Math.max(12, Math.min(view.width - width - 12, aaButton.mapToItem(view, 0, 0).x - width / 2 + aaButton.width / 2))
        pdf: view.pdf
    }

    SelectionMenu {
        id: selectionMenu
        property bool keepSelection: false
        readonly property rect anchorRect: Qt.rect(pages.x + pages.selectionRect.x, pages.y + pages.selectionRect.y,
                                                   pages.selectionRect.width, pages.selectionRect.height)
        x: Math.max(12, Math.min(view.width - width - 12, anchorRect.x + anchorRect.width / 2 - width / 2))
        y: anchorRect.y - height - 10 > 8 ? anchorRect.y - height - 10 : anchorRect.y + anchorRect.height + 10
        onHighlight: color => view.highlightSelection(color)
        onNote: view.noteSelection()
        onCopy: { reader.copySelection(); keepSelection = false; close(); reader.clearSelection(); view.message(qsTr("Copied")) }
        onDefine: view.defineSelection()
        onSearch: { const text = reader.selectionText; keepSelection = false; close(); reader.clearSelection(); searchPanel.searchFor(text) }
        onRemove: { reader.removeHighlight(reader.selectedHighlight); keepSelection = false; close() }
        onClosed: if (!keepSelection) reader.clearSelection()
    }

    NoteEditor {
        id: noteEditor
        anchors.centerIn: parent
        onClosed: { reader.clearSelection(); pages.forceActiveFocus() }
    }

    DefinitionCard {
        id: definition
        x: Math.max(12, Math.min(view.width - width - 12, selectionMenu.anchorRect.x + selectionMenu.anchorRect.width / 2 - width / 2))
        y: selectionMenu.anchorRect.y + selectionMenu.anchorRect.height + 12 + height < view.height
           ? selectionMenu.anchorRect.y + selectionMenu.anchorRect.height + 12
           : Math.max(12, selectionMenu.anchorRect.y - height - 12)
        onClosed: reader.clearSelection()
        onSearchRequested: word => { close(); searchPanel.searchFor(word) }
    }
}
