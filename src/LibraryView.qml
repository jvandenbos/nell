import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The shelf: what you were reading up top, everything below, newest first.
Item {
    id: view
    signal openRequested(string id)

    property string filter: ""
    // The find field lives in the grid's header, which has its own scope.
    property Item searchField: null
    readonly property bool searching: searchField !== null && searchField.activeFocus
    readonly property var books: {
        library.books  // Re-sort whenever the library changes.
        return library.sorted(prefs.librarySort, filter)
    }
    readonly property var continuing: {
        if (filter.length)
            return []
        const list = library.sorted("recent", "")
        return list.filter(b => b.opened && b.opened.length && !b.finished && !b.missing).slice(0, 3)
    }

    function focusGrid() { grid.forceActiveFocus() }
    function openCurrent() {
        if (grid.currentIndex >= 0 && grid.currentIndex < books.length)
            view.openRequested(books[grid.currentIndex].id)
    }

    Component.onCompleted: grid.forceActiveFocus()

    Shortcut { context: Qt.ApplicationShortcut; sequence: "Space"; enabled: view.visible && !view.searching; onActivated: view.openCurrent() }
    Shortcut { context: Qt.ApplicationShortcut; sequences: ["/", StandardKey.Find]; enabled: view.visible && view.searchField !== null; onActivated: view.searchField.forceActiveFocus() }
    Shortcut {
        context: Qt.ApplicationShortcut
        sequences: [StandardKey.Delete, "Backspace"]
        enabled: view.visible && !view.searching && grid.currentIndex >= 0
        onActivated: removeDialog.ask(view.books[grid.currentIndex])
    }

    Rectangle { anchors.fill: parent; color: "#0e0e10" }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: Math.max(24, (width - Math.floor((width - 48) / cellWidth) * cellWidth) / 2)
        anchors.rightMargin: 0
        model: view.books
        cellWidth: 184
        cellHeight: 316
        clip: true
        focus: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        currentIndex: count > 0 ? 0 : -1
        highlightFollowsCurrentItem: false
        ScrollBar.vertical: ScrollBar {}

        Keys.onReturnPressed: view.openCurrent()
        Keys.onEnterPressed: view.openCurrent()

        header: Item {
            width: grid.width
            height: headerColumn.implicitHeight + 24

            ColumnLayout {
                id: headerColumn
                width: Math.floor((grid.width - 24) / grid.cellWidth) * grid.cellWidth - 24
                spacing: 18

                RowLayout {
                    Layout.topMargin: 28
                    Layout.fillWidth: true
                    spacing: 12

                    Label {
                        text: qsTr("Library")
                        font.pixelSize: 30
                        font.weight: Font.Bold
                        Layout.fillWidth: true
                    }
                    TextField {
                        id: search
                        placeholderText: qsTr("Find a book")
                        Layout.preferredWidth: 240
                        Material.roundedScale: theme.radius
                        onTextChanged: view.filter = text
                        Component.onCompleted: view.searchField = search
                        Keys.onEscapePressed: { text = ""; grid.forceActiveFocus() }
                        Keys.onDownPressed: grid.forceActiveFocus()
                    }
                    ComboBox {
                        id: sort
                        Layout.preferredWidth: 150
                        Material.roundedScale: theme.radius
                        textRole: "label"
                        valueRole: "key"
                        model: [
                            { key: "recent", label: qsTr("Recent") },
                            { key: "title", label: qsTr("Title") },
                            { key: "author", label: qsTr("Author") }
                        ]
                        currentIndex: Math.max(0, ["recent", "title", "author"].indexOf(prefs.librarySort))
                        onActivated: prefs.librarySort = currentValue
                    }
                    AccentButton {
                        text: qsTr("Add Books")
                        onClicked: picker.openBooks()
                    }
                }

                // Continue reading, like the top of Apple Books' Home.
                Label {
                    visible: view.continuing.length > 0
                    text: qsTr("Continue Reading")
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    opacity: 0.9
                }
                Row {
                    visible: view.continuing.length > 0
                    spacing: 20
                    Repeater {
                        model: view.continuing
                        delegate: ContinueCard {
                            required property var modelData
                            book: modelData
                            width: Math.min(420, (headerColumn.width - 40) / 3)
                            onOpen: view.openRequested(modelData.id)
                        }
                    }
                }

                Label {
                    visible: view.books.length > 0
                    text: view.filter.length ? qsTr("Results") : qsTr("All Books")
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    opacity: 0.9
                    Layout.topMargin: view.continuing.length > 0 ? 8 : 0
                }
            }
        }

        delegate: Item {
            id: card
            required property var modelData
            required property int index
            width: grid.cellWidth
            height: grid.cellHeight
            readonly property bool current: GridView.isCurrentItem && grid.activeFocus

            Column {
                x: 0
                y: 4
                width: 156
                spacing: 8

                Item {
                    width: 156
                    height: 234
                    BookCover {
                        anchors.fill: parent
                        anchors.margins: hover.hovered ? 0 : 3
                        Behavior on anchors.margins { NumberAnimation { duration: 120 } }
                        source: card.modelData.cover
                        title: card.modelData.title
                        author: card.modelData.author
                        missing: card.modelData.missing
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        visible: card.current
                        color: "transparent"
                        radius: Math.min(4, theme.radius) + 4
                        border.width: 2
                        border.color: theme.accent
                    }
                    Rectangle {
                        visible: card.modelData.format === "pdf"
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        width: pdfLabel.implicitWidth + 10
                        height: pdfLabel.implicitHeight + 4
                        radius: Math.min(3, theme.radius)
                        color: "#cc000000"
                        Label { id: pdfLabel; anchors.centerIn: parent; text: "PDF"; font.pixelSize: 10; font.weight: Font.Bold }
                    }
                }

                Label {
                    width: parent.width
                    text: card.modelData.title
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    width: parent.width
                    spacing: 6
                    Label {
                        Layout.fillWidth: true
                        text: card.modelData.missing ? qsTr("File missing") : card.modelData.author
                        color: card.modelData.missing ? "#ff8a80" : "#a0a0a8"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Label {
                        text: card.modelData.finished ? qsTr("Finished")
                              : card.modelData.progress > 0 ? Math.max(1, Math.round(card.modelData.progress * 100)).toLocaleString(Qt.locale(), 'f', 0) + "%"
                              : qsTr("New")
                        color: card.modelData.progress > 0 || card.modelData.finished ? "#a0a0a8" : theme.accent
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
            }

            HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
            // One click opens a book, as on a shelf; the keyboard selects.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: { grid.currentIndex = card.index; view.openRequested(card.modelData.id) }
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: { grid.currentIndex = card.index; contextMenu.book = card.modelData; contextMenu.popup() }
            }
        }

        // Nothing on the shelf yet.
        ColumnLayout {
            visible: grid.count === 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(180, parent.height / 2 - 60)
            spacing: 14
            Icon {
                Layout.alignment: Qt.AlignHCenter
                name: "library"
                size: 56
                color: "#5a5a62"
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: view.filter.length ? qsTr("No books match “%1”").arg(view.filter) : qsTr("Your library is empty")
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                visible: !view.filter.length
                text: qsTr("Add EPUB and PDF books, or drop them here.")
                color: "#a0a0a8"
            }
            AccentButton {
                Layout.alignment: Qt.AlignHCenter
                visible: !view.filter.length
                text: qsTr("Add Books")
                onClicked: picker.openBooks()
            }
        }
    }

    Menu {
        id: contextMenu
        property var book
        Material.roundedScale: theme.radius
        MenuItem { text: qsTr("Open"); onTriggered: view.openRequested(contextMenu.book.id) }
        MenuItem { text: qsTr("Remove from Library…"); onTriggered: removeDialog.ask(contextMenu.book) }
    }

    Dialog {
        id: removeDialog
        property var book
        function ask(book) { removeDialog.book = book; open() }
        anchors.centerIn: parent
        modal: true
        Material.roundedScale: theme.radius
        title: qsTr("Remove “%1”?").arg(book ? book.title : "")
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            width: 380
            wrapMode: Text.Wrap
            text: qsTr("The book file stays where it is. Your place, highlights, and notes are kept, and come back if you add the book again.")
            color: "#c0c0c8"
        }
        Component.onCompleted: standardButton(Dialog.Ok).text = qsTr("Remove")
        onAccepted: { library.remove(book.id); grid.forceActiveFocus() }
        onRejected: grid.forceActiveFocus()
    }
}
