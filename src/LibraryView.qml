import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The shelf: what you were reading up top, everything below, newest first.
// Finding and sorting stay pinned above the shelf, however far it scrolls.
Item {
    id: view
    signal openRequested(string id)
    signal locateRequested(string id)

    property string filter: ""
    // "all", "reading", or "finished".
    property string status: "all"
    readonly property bool narrow: width < 800
    readonly property var books: {
        library.books  // Re-sort whenever the library changes.
        const list = library.sorted(prefs.librarySort, filter)
        if (status === "reading")
            return list.filter(b => !b.finished && b.progress > 0)
        if (status === "finished")
            return list.filter(b => b.finished)
        return list
    }
    readonly property var continuing: {
        library.books  // Re-evaluate when progress or opening times change.
        if (filter.length || status !== "all")
            return []
        const list = library.sorted("recent", "")
        return list.filter(b => b.opened && b.opened.length && !b.finished && !b.missing).slice(0, 3)
    }

    // The shelf's geometry. Each cover sits 6 px into its cell so the focus
    // ring around it isn't cut off by the grid's edge, and the header lines
    // up with the covers rather than with the cells.
    readonly property int cellWidth: 184
    readonly property int coverWidth: 156
    readonly property int coverHeight: 234
    readonly property int ringRoom: 6
    readonly property int shelfColumns: Math.max(1, Math.floor((width - 48) / cellWidth))
    readonly property real shelfLeft: Math.round((width - shelfColumns * cellWidth) / 2)
    readonly property real contentLeft: shelfLeft + ringRoom
    readonly property real contentWidth: (shelfColumns - 1) * cellWidth + coverWidth

    function focusGrid() { grid.forceActiveFocus() }
    function openCurrent() {
        if (grid.currentIndex >= 0 && grid.currentIndex < books.length)
            view.openRequested(books[grid.currentIndex].id)
    }

    Component.onCompleted: grid.forceActiveFocus()

    Shortcut { context: Qt.ApplicationShortcut; sequence: "Space"; enabled: view.visible && grid.activeFocus; onActivated: view.openCurrent() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "/"; enabled: view.visible && !win.textEntry; onActivated: search.forceActiveFocus() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: StandardKey.Find; enabled: view.visible; onActivated: search.forceActiveFocus() }
    Shortcut {
        context: Qt.ApplicationShortcut
        sequences: [StandardKey.Delete, "Backspace"]
        enabled: view.visible && grid.activeFocus && grid.currentIndex >= 0
        onActivated: removeDialog.ask(view.books[grid.currentIndex])
    }

    Rectangle { anchors.fill: parent; color: Tokens.surface }

    // ---- Pinned header ----------------------------------------------------
    Rectangle {
        id: header
        width: parent.width
        height: headerColumn.implicitHeight + 40
        color: Tokens.surface
        z: 2

        ColumnLayout {
            id: headerColumn
            x: view.contentLeft
            y: 24
            width: view.contentWidth
            spacing: 16

            // Below 800 px the find field gets its own row, so the title
            // never ends up underneath it.
            GridLayout {
                Layout.fillWidth: true
                columns: view.narrow ? 2 : 4
                columnSpacing: 8
                rowSpacing: 8

                Label {
                    Layout.row: 0
                    Layout.column: 0
                    Layout.fillWidth: true
                    text: qsTr("Library")
                    font.pixelSize: 30
                    font.weight: Font.Bold
                    color: Tokens.text
                    elide: Text.ElideRight
                    Accessible.role: Accessible.Heading
                }
                TextField {
                    id: search
                    Layout.row: view.narrow ? 1 : 0
                    Layout.column: view.narrow ? 0 : 1
                    Layout.fillWidth: view.narrow
                    Layout.preferredWidth: 240
                    placeholderText: qsTr("Find a book")
                    Material.roundedScale: theme.radius
                    Accessible.name: qsTr("Find a book")
                    onTextChanged: view.filter = text
                    Keys.onEscapePressed: { text = ""; grid.forceActiveFocus() }
                    Keys.onDownPressed: grid.forceActiveFocus()
                }
                ComboBox {
                    id: sort
                    Layout.row: view.narrow ? 1 : 0
                    Layout.column: view.narrow ? 1 : 2
                    Layout.preferredWidth: view.narrow ? 128 : 152
                    Material.roundedScale: theme.radius
                    Accessible.name: qsTr("Sort by")
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
                    Layout.row: 0
                    Layout.column: view.narrow ? 1 : 3
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    text: qsTr("Add Books")
                    onClicked: win.pickBooks()
                }
            }

            Segmented {
                // Its options fill, which would otherwise make it fill too.
                Layout.fillWidth: view.narrow
                Layout.preferredWidth: view.narrow ? view.contentWidth : 312
                spacing: 4
                accessibleName: qsTr("Show")
                options: [["all", qsTr("All")], ["reading", qsTr("Reading")], ["finished", qsTr("Finished")]]
                value: view.status
                onPicked: value => view.status = value
            }
        }

        // A hairline once the shelf has scrolled under the header.
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Tokens.border
            visible: grid.contentY > grid.originY
        }
    }

    // ---- Shelf --------------------------------------------------------------
    GridView {
        id: grid
        // Main.qml's bare-letter shortcuts (Q, F) look for this name.
        objectName: "shelf"
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: view.shelfLeft
        model: view.books
        cellWidth: view.cellWidth
        cellHeight: 328
        clip: true
        focus: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        currentIndex: count > 0 ? 0 : -1
        highlightFollowsCurrentItem: false
        ScrollBar.vertical: ScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Books")

        Keys.onReturnPressed: view.openCurrent()
        Keys.onEnterPressed: view.openCurrent()

        header: Item {
            width: grid.width
            height: continueColumn.implicitHeight + 8

            ColumnLayout {
                id: continueColumn
                x: view.ringRoom
                width: view.contentWidth
                spacing: 16

                // Continue reading, like the top of Apple Books' Home. As
                // many cards as fit at 320 px or more, at least one.
                readonly property int cards: Math.max(1, Math.min(view.continuing.length, 3, Math.floor(view.contentWidth / 320)))

                Label {
                    visible: view.continuing.length > 0
                    Layout.topMargin: 8
                    text: qsTr("Continue Reading")
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: Tokens.text
                    Accessible.role: Accessible.Heading
                }
                Row {
                    visible: view.continuing.length > 0
                    spacing: 16
                    Repeater {
                        model: view.continuing.slice(0, continueColumn.cards)
                        delegate: ContinueCard {
                            required property var modelData
                            book: modelData
                            width: Math.min(420, (view.contentWidth - 16 * (continueColumn.cards - 1)) / continueColumn.cards)
                            onClicked: view.openRequested(modelData.id)
                        }
                    }
                }

                Label {
                    visible: view.books.length > 0
                    text: view.filter.length ? qsTr("Results")
                          : view.status === "reading" ? qsTr("Reading")
                          : view.status === "finished" ? qsTr("Finished")
                          : qsTr("All Books")
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: Tokens.text
                    Layout.topMargin: 8
                    Accessible.role: Accessible.Heading
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
            Accessible.role: Accessible.ListItem
            Accessible.name: modelData.title + (modelData.author ? ", " + modelData.author : "")
            Accessible.selected: GridView.isCurrentItem

            Column {
                x: view.ringRoom
                y: view.ringRoom
                width: view.coverWidth
                spacing: 8

                Item {
                    width: view.coverWidth
                    height: view.coverHeight
                    BookCover {
                        id: coverItem
                        anchors.fill: parent
                        anchors.margins: hover.hovered ? 0 : 3
                        Behavior on anchors.margins { NumberAnimation { duration: Tokens.duration(120) } }
                        source: card.modelData.cover
                        title: card.modelData.title
                        author: card.modelData.author
                        missing: card.modelData.missing
                    }
                    // The ring follows the cover's own shape, not the box.
                    Rectangle {
                        x: coverItem.x + coverItem.frame.x - 4
                        y: coverItem.y + coverItem.frame.y - 4
                        width: coverItem.frame.width + 8
                        height: coverItem.frame.height + 8
                        visible: card.current
                        color: "transparent"
                        radius: Math.min(4, theme.radius) + 4
                        border.width: 2
                        border.color: Tokens.focus
                    }
                    Rectangle {
                        visible: card.modelData.format === "pdf"
                        x: coverItem.x + coverItem.frame.x + coverItem.frame.width - width - 8
                        y: coverItem.y + coverItem.frame.y + 8
                        width: pdfLabel.implicitWidth + 10
                        height: pdfLabel.implicitHeight + 4
                        radius: Math.min(3, theme.radius)
                        color: Tokens.coverBadge
                        Label { id: pdfLabel; anchors.centerIn: parent; text: "PDF"; color: Tokens.coverBadgeText; font.pixelSize: 10; font.weight: Font.Bold }
                    }
                }

                Label {
                    width: parent.width
                    text: card.modelData.title
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Tokens.text
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    width: parent.width
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: card.modelData.missing ? qsTr("File missing") : card.modelData.author
                        color: card.modelData.missing ? Tokens.danger : Tokens.textSecondary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Label {
                        text: card.modelData.finished ? qsTr("Finished")
                              : card.modelData.progress > 0 ? Math.max(1, Math.round(card.modelData.progress * 100)).toLocaleString(Qt.locale(), 'f', 0) + "%"
                              : qsTr("New")
                        color: card.modelData.progress > 0 || card.modelData.finished ? Tokens.textSecondary : Tokens.accentText
                        font.pixelSize: 12
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

        // Nothing on the shelf yet, or nothing that matches.
        ColumnLayout {
            visible: grid.count === 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(96, parent.height / 2 - 120)
            width: Math.min(parent.width - 32, 480)
            spacing: 16
            Icon {
                Layout.alignment: Qt.AlignHCenter
                name: "library"
                size: 56
                color: Tokens.textSecondary
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: parent.width
                text: view.filter.length ? qsTr("No books match “%1”").arg(view.filter)
                      : view.status === "reading" ? qsTr("No books in progress")
                      : view.status === "finished" ? qsTr("No finished books yet")
                      : qsTr("Your library is empty")
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: Tokens.text
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                visible: !view.filter.length && view.status === "all"
                text: qsTr("Add EPUB and PDF books, or drop them here.")
                color: Tokens.textSecondary
            }
            AccentButton {
                Layout.alignment: Qt.AlignHCenter
                visible: !view.filter.length && view.status === "all"
                text: qsTr("Add Books")
                onClicked: win.pickBooks()
            }
        }
    }

    Menu {
        id: contextMenu
        property var book
        Material.roundedScale: theme.radius
        background: Rectangle {
            implicitWidth: 200
            radius: theme.radius
            color: Tokens.surfaceRaised
            border.width: 1
            border.color: Tokens.border
        }
        MenuItem { text: qsTr("Open"); onTriggered: view.openRequested(contextMenu.book.id) }
        MenuItem {
            visible: contextMenu.book ? contextMenu.book.missing === true : false
            height: visible ? implicitHeight : 0
            text: qsTr("Locate File…")
            onTriggered: view.locateRequested(contextMenu.book.id)
        }
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
            width: Math.min(380, view.width - 96)
            wrapMode: Text.Wrap
            text: qsTr("The book file stays where it is. Your place, highlights, and notes are kept, and come back if you add the book again.")
            color: Tokens.text
        }
        Component.onCompleted: standardButton(Dialog.Ok).text = qsTr("Remove")
        onAccepted: { library.remove(book.id); grid.forceActiveFocus() }
        onRejected: grid.forceActiveFocus()
    }
}
