import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    // Half of a 1080p tile, or less: every layout below adapts to it.
    minimumWidth: 480
    minimumHeight: 480
    visible: true
    title: reader.open ? reader.title : "Nell"
    color: Tokens.surface

    // Light or dark with the surface: the library follows the desktop's
    // theme, the reader its reading theme.
    Material.theme: Tokens.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: Tokens.surfaceRaised
    Material.foreground: Tokens.text
    Material.roundedScale: theme.radius

    readonly property bool helpVisible: help.opened
    readonly property bool reading: reader.open
    // Typing in a field: unmodified letter shortcuts stand down so the
    // letters reach the field.
    readonly property bool textEntry: activeFocusItem !== null && activeFocusItem.cursorPosition !== undefined
    // The page or the shelf has the keyboard, not a panel, field, or button.
    readonly property bool surfaceFocused: activeFocusItem !== null
                                           && (activeFocusItem.objectName === "page" || activeFocusItem.objectName === "shelf")
    // Set while the file chooser is out to find a moved book, not add new ones.
    property string locateId: ""
    // The catalogs are a view of their own in the library's place.
    property bool catalogOpen: false
    // Room for the sidebar beside the shelf; below this the library's
    // header carries its choices instead.
    readonly property bool wide: width >= 900
    // Where we are, as the sidebar names its rows.
    readonly property string place: reading ? ""
                                    : catalogOpen ? (catalog.current >= 0 ? "catalog:" + catalog.current : "catalogs")
                                    : "library:" + libraryView.status

    function showLibrary(status) {
        if (catalogOpen) {
            catalog.leave()
            catalogOpen = false
        }
        libraryView.status = status
        libraryView.focusGrid()
    }
    // The list of catalogs, to add to, take from, or pick from.
    function openCatalogs() {
        catalog.leave()
        catalogOpen = true
        catalogView.focusList()
    }
    // Straight into one catalog's feed.
    function openCatalog(index) {
        catalogOpen = true
        catalog.open(index)
        catalogView.focusList()
    }
    function closeCatalogs() {
        catalog.leave()
        catalogOpen = false
        libraryView.focusGrid()
    }
    function showStats(from) { statsPanel.show(from) }
    function focusContent() {
        if (catalogOpen)
            catalogView.focusList()
        else
            libraryView.focusGrid()
    }

    function openBook(id) {
        const book = library.book(id)
        if (book.missing) {
            toast.show(qsTr("“%1” isn't where it was.").arg(book.title),
                       { error: true, actionText: qsTr("Locate File…"), action: () => win.locate(id) })
            return
        }
        if (!reader.openBook(id))
            toast.show(reader.error, { error: true })
    }

    function closeBook() {
        reader.close()
        if (catalogOpen)
            catalogView.focusList()
        else
            libraryView.focusGrid()
    }

    function pickBooks() {
        locateId = ""
        picker.openBooks()
    }

    function locate(id) {
        locateId = id
        picker.openBooks()
    }

    function addBooks(urls) {
        const before = library.books.length
        let added = 0
        let lastId = ""
        const errors = []
        for (const url of urls) {
            const id = library.add(url.toString())
            if (id.length) {
                ++added
                lastId = id
            } else if (library.error.length) {
                errors.push(library.error)
            }
        }
        library.clearError()
        const fresh = library.books.length - before
        if (errors.length === 0) {
            // One new book dropped or picked is one the reader wants to read.
            if (urls.length === 1 && fresh === 1)
                openBook(lastId)
            else if (fresh > 0)
                toast.show(qsTr("Added %n books", "", fresh))
            else if (added > 0)
                toast.show(added === 1 ? qsTr("That book is already in your library")
                                       : qsTr("Those books are already in your library"))
        } else if (added === 0 && errors.length === 1) {
            toast.show(errors[0], { error: true })
        } else {
            toast.show(qsTr("Added %n books", "", fresh) + "; " + qsTr("%n failed", "", errors.length),
                       { error: true, details: errors })
        }
    }

    // Global keys; the reader and library each add their own while shown.
    // Bare letters only act on the page or shelf, never while typing.
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.textEntry; onActivated: help.opened ? help.close() : help.show(win.activeFocusItem) }
    Shortcut { sequences: [StandardKey.Quit, "Ctrl+Q"]; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: win.surfaceFocused; onActivated: Qt.quit() }
    Shortcut { sequences: [StandardKey.Open, "Ctrl+O"]; context: Qt.ApplicationShortcut; onActivated: win.pickBooks() }
    Shortcut { sequence: "F11"; context: Qt.ApplicationShortcut; onActivated: win.toggleFullScreen() }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: win.surfaceFocused; onActivated: win.toggleFullScreen() }
    // Around the library and the catalogs, wherever the keyboard is there.
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: !win.reading && !win.textEntry && !win.helpVisible; onActivated: win.showStats(win.activeFocusItem) }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; enabled: !win.reading && !win.textEntry && !win.helpVisible; onActivated: win.openCatalogs() }

    function toggleFullScreen() {
        visibility = visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    Connections {
        target: picker
        function onBooksSelected(urls) {
            if (win.locateId.length && urls.length) {
                const id = win.locateId
                win.locateId = ""
                if (library.relocate(id, urls[0].toString()))
                    toast.show(qsTr("Found “%1”").arg(library.book(id).title))
                else
                    toast.show(library.error, { error: true })
                library.clearError()
                return
            }
            win.addBooks(urls)
        }
        function onNotesTargetSelected(url) {
            if (reader.exportNotes(url))
                toast.show(qsTr("Notes exported to %1").arg(decodeURIComponent(url.toString().replace("file://", ""))))
            else
                toast.show(reader.error, { error: true })
        }
        function onFailed(message) { toast.show(message, { error: true }) }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        Sidebar {
            id: sidebar
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: implicitWidth
            visible: !win.reading && win.wide
            enabled: visible
            current: win.place
            onLibraryPicked: status => win.showLibrary(status)
            onCatalogsPicked: win.openCatalogs()
            onCatalogPicked: index => win.openCatalog(index)
            onRecordPicked: from => win.showStats(from)
            onAddBooksPicked: win.pickBooks()
            onHelpPicked: from => help.show(from)
            onEscaped: win.focusContent()
        }

        // The library or the catalogs, beside the sidebar when it's there.
        Item {
            id: content
            anchors.left: sidebar.visible ? sidebar.right : parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            LibraryView {
                id: libraryView
                anchors.fill: parent
                visible: !win.reading && !win.catalogOpen
                enabled: visible
                onOpenRequested: id => win.openBook(id)
                onLocateRequested: id => win.locate(id)
            }

            CatalogView {
                id: catalogView
                anchors.fill: parent
                visible: !win.reading && win.catalogOpen
                enabled: visible
                onCloseRequested: win.closeCatalogs()
                onOpenRequested: id => win.openBook(id)
            }
        }

        Loader {
            anchors.fill: parent
            active: win.reading
            sourceComponent: ReaderView {
                onCloseRequested: win.closeBook()
                onMessage: text => toast.show(text)
                onFailure: text => toast.show(text, { error: true })
                onUndoable: text => toast.show(text, { actionText: qsTr("Undo"), action: () => reader.undo(), timeout: 6000 })
            }
        }

        DropArea {
            anchors.fill: parent
            keys: ["text/uri-list"]
            onDropped: drop => {
                if (drop.hasUrls) {
                    win.addBooks(drop.urls)
                    drop.accept()
                }
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 16
                visible: parent.containsDrag
                color: "transparent"
                radius: theme.radius
                border.width: 2
                border.color: Tokens.accent
                Rectangle {
                    anchors.fill: parent
                    radius: theme.radius
                    color: Tokens.accent
                    opacity: 0.08
                }
                Label {
                    anchors.centerIn: parent
                    text: qsTr("Drop books to add them to your library")
                    font.pixelSize: 20
                    color: Tokens.accentText
                }
            }
        }
    }

    Connections {
        target: catalog
        function onDownloaded(id, title) {
            toast.show(qsTr("Added “%1” to the library").arg(title), { actionText: qsTr("Open"), action: () => win.openBook(id), timeout: 8000 })
        }
    }

    // Milestones as they happen, in the corner, without stopping the page.
    Connections {
        target: record
        function onGoalReached() { toast.show(qsTr("Today's reading goal met")) }
        function onBadgeEarned(name) { toast.show(qsTr("Badge earned: %1").arg(name)) }
    }

    StatsPanel { id: statsPanel }
    HelpOverlay { id: help; reading: win.reading }
    Toast { id: toast }
}
