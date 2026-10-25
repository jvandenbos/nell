import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: reader.open ? reader.title : "Nell"
    color: reader.open ? prefs.pageColor : "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    readonly property bool reading: reader.open

    function openBook(id) {
        if (!reader.openBook(id))
            toast.show(reader.error)
    }

    function closeBook() {
        reader.close()
        libraryView.focusGrid()
    }

    function addBooks(urls) {
        const before = library.books.length
        const added = library.addUrls(urls)
        if (library.error.length)
            toast.show(library.error)
        // One new book dropped or picked is one the reader wants to read.
        if (urls.length === 1 && added === 1 && library.books.length > before)
            openBook(library.books[0].id)
    }

    // Global keys; the reader and library each add their own while shown.
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequences: [StandardKey.Open, "Ctrl+O"]; context: Qt.ApplicationShortcut; onActivated: picker.openBooks() }
    Shortcut {
        sequences: ["F11", "F"]
        context: Qt.ApplicationShortcut
        onActivated: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: win.helpVisible
        onActivated: win.helpVisible = false
    }

    Connections {
        target: picker
        function onBooksSelected(urls) { win.addBooks(urls) }
        function onNotesTargetSelected(url) {
            if (reader.exportNotes(url))
                toast.show(qsTr("Notes exported to %1").arg(decodeURIComponent(url.toString().replace("file://", ""))))
            else
                toast.show(reader.error)
        }
        function onFailed(message) { toast.show(message) }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        LibraryView {
            id: libraryView
            anchors.fill: parent
            visible: !win.reading
            enabled: visible
            onOpenRequested: id => win.openBook(id)
        }

        Loader {
            anchors.fill: parent
            active: win.reading
            sourceComponent: ReaderView {
                onCloseRequested: win.closeBook()
                onMessage: text => toast.show(text)
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
                border.color: theme.accent
                Rectangle {
                    anchors.fill: parent
                    radius: theme.radius
                    color: theme.accent
                    opacity: 0.08
                }
                Label {
                    anchors.centerIn: parent
                    text: qsTr("Drop books to add them to your library")
                    font.pixelSize: 20
                    color: theme.accent
                }
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            reading: win.reading
            onDismissed: win.helpVisible = false
        }

        Toast { id: toast }
    }
}
