import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Everything to jump to: the chapters, your bookmarks, your highlights and
// notes, and the words you looked up. The lists take the arrow keys and
// Enter; Tab reaches each row's own buttons.
Drawer {
    id: panel
    signal exportRequested()
    signal editNoteRequested(string id)
    // Something was deleted; the text says what, for an Undo toast.
    signal removed(string text)

    edge: Qt.LeftEdge
    width: Math.min(400, parent ? parent.width - 48 : 400)
    focus: true
    Material.roundedScale: Material.NotRounded
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Contents")
    Overlay.modal: Rectangle { color: Tokens.scrim }
    background: Rectangle { color: Tokens.surfaceRaised; Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Tokens.border } }

    onOpened: {
        tocList.currentIndex = Math.max(0, reader.tocIndex)
        tocList.positionViewAtIndex(tocList.currentIndex, ListView.Center)
        panel.focusTab(tabs.currentIndex)
    }

    function go(action) {
        action()
        close()
    }

    // The keyboard starts in the open tab's list.
    function focusTab(index) {
        [tocList, bookmarkList, highlightList, wordList][index].forceActiveFocus()
    }

    // A list row's look: highlighted while the keyboard is on it.
    component ListRow: ItemDelegate {
        width: ListView.view.width
        focusPolicy: Qt.NoFocus
        highlighted: ListView.isCurrentItem && ListView.view.activeFocus
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 8
            spacing: 16
            BookCover {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 66
                readonly property var book: library.book(reader.bookId)
                source: book.cover || ""
                title: reader.title
                author: reader.author
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { Layout.fillWidth: true; text: reader.title; font.weight: Font.DemiBold; color: Tokens.text; elide: Text.ElideRight }
                Label { Layout.fillWidth: true; text: reader.author; color: Tokens.textSecondary; elide: Text.ElideRight }
                Label {
                    text: qsTr("%1% read").arg(Math.round(reader.progress * 100).toLocaleString(Qt.locale(), 'f', 0))
                    color: Tokens.textSecondary
                    font.pixelSize: 12
                }
            }
            IconButton {
                Layout.alignment: Qt.AlignTop
                iconName: "close"
                tip: qsTr("Close (Esc)")
                implicitWidth: 32
                implicitHeight: 32
                onClicked: panel.close()
            }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            Material.background: Tokens.surfaceRaised
            Material.accent: Tokens.accentText
            onCurrentIndexChanged: if (panel.opened) panel.focusTab(currentIndex)
            TabButton { text: qsTr("Contents"); font.pixelSize: 13; leftPadding: 4; rightPadding: 4 }
            TabButton { text: qsTr("Bookmarks"); font.pixelSize: 13; leftPadding: 4; rightPadding: 4 }
            TabButton { text: qsTr("Notes"); font.pixelSize: 13; leftPadding: 4; rightPadding: 4 }
            TabButton { text: qsTr("Words"); font.pixelSize: 13; leftPadding: 4; rightPadding: 4 }
        }

        StackLayout {
            id: lists
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // Contents
            ListView {
                id: tocList
                clip: true
                model: reader.toc
                keyNavigationEnabled: true
                activeFocusOnTab: true
                highlightMoveDuration: 0
                ScrollBar.vertical: ScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Contents")
                Keys.onReturnPressed: if (currentIndex >= 0) panel.go(() => reader.goToToc(tocList.currentIndex))
                Keys.onEnterPressed: if (currentIndex >= 0) panel.go(() => reader.goToToc(tocList.currentIndex))
                delegate: ListRow {
                    required property var modelData
                    required property int index
                    Accessible.name: modelData.title
                    onClicked: panel.go(() => reader.goToToc(index))
                    contentItem: RowLayout {
                        spacing: 16
                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: modelData.level * 16
                            text: modelData.title
                            elide: Text.ElideRight
                            font.weight: index === reader.tocIndex ? Font.DemiBold : Font.Normal
                            color: index === reader.tocIndex ? Tokens.accentText : Tokens.text
                        }
                        Label {
                            text: modelData.page.toLocaleString(Qt.locale(), 'f', 0)
                            color: Tokens.textSecondary
                            font.pixelSize: 12
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: tocList.count === 0
                    text: qsTr("This book has no table of contents.")
                    color: Tokens.textSecondary
                }
            }

            // Bookmarks
            ListView {
                id: bookmarkList
                clip: true
                model: reader.bookmarks
                keyNavigationEnabled: true
                activeFocusOnTab: true
                highlightMoveDuration: 0
                ScrollBar.vertical: ScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Bookmarks")
                Keys.onReturnPressed: if (currentItem) currentItem.clicked()
                Keys.onEnterPressed: if (currentItem) currentItem.clicked()
                Keys.onDeletePressed: if (currentItem) currentItem.remove()
                delegate: ListRow {
                    id: bookmarkRow
                    required property var modelData
                    Accessible.name: modelData.title + ", " + qsTr("Page %1").arg(modelData.page)
                    function remove() {
                        reader.removeBookmark(modelData.index)
                        panel.removed(qsTr("Bookmark removed"))
                    }
                    onClicked: panel.go(() => reader.goTo(modelData.chapter, modelData.offset))
                    contentItem: RowLayout {
                        spacing: 8
                        Icon { name: "bookmark"; filled: true; color: Tokens.bookmark; size: 16 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { Layout.fillWidth: true; text: modelData.title; font.weight: Font.DemiBold; color: Tokens.text; elide: Text.ElideRight }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.snippet
                                color: Tokens.textSecondary
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                        Label { text: modelData.page.toLocaleString(Qt.locale(), 'f', 0); color: Tokens.textSecondary; font.pixelSize: 12 }
                        IconButton { iconName: "close"; tip: qsTr("Remove Bookmark"); implicitWidth: 32; implicitHeight: 32; onClicked: bookmarkRow.remove() }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: bookmarkList.count === 0
                    text: qsTr("No bookmarks yet. Press B to mark a page.")
                    color: Tokens.textSecondary
                }
            }

            // Highlights and notes
            ColumnLayout {
                spacing: 0
                ListView {
                    id: highlightList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: reader.highlights
                    spacing: 2
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    highlightMoveDuration: 0
                    ScrollBar.vertical: ScrollBar {}
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Notes")
                    Keys.onReturnPressed: if (currentItem) currentItem.clicked()
                    Keys.onEnterPressed: if (currentItem) currentItem.clicked()
                    Keys.onDeletePressed: if (currentItem) currentItem.remove()
                    section.property: "section"
                    section.delegate: Label {
                        required property string section
                        width: ListView.view.width
                        leftPadding: 16
                        topPadding: 16
                        bottomPadding: 4
                        text: section
                        color: Tokens.textSecondary
                        font.pixelSize: 11
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                        elide: Text.ElideRight
                    }
                    delegate: ListRow {
                        id: highlightRow
                        required property var modelData
                        Accessible.name: modelData.text
                        Accessible.description: modelData.note
                        function remove() {
                            const hadNote = modelData.note.length > 0
                            reader.removeHighlight(modelData.id)
                            panel.removed(hadNote ? qsTr("Note removed") : qsTr("Highlight removed"))
                        }
                        onClicked: panel.go(() => reader.goToHighlight(modelData.id))
                        contentItem: RowLayout {
                            spacing: 8
                            Rectangle {
                                Layout.fillHeight: true
                                width: 3
                                radius: 1.5
                                color: Tokens.highlightColor(modelData.color)
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.text
                                    font.family: "serif"
                                    color: Tokens.text
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 4
                                    elide: Text.ElideRight
                                }
                                Label {
                                    visible: modelData.note.length > 0
                                    Layout.fillWidth: true
                                    text: modelData.note
                                    color: Tokens.accentText
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                    font.pixelSize: 13
                                }
                                Label {
                                    text: qsTr("Page %1").arg(modelData.page.toLocaleString(Qt.locale(), 'f', 0))
                                    color: Tokens.textSecondary
                                    font.pixelSize: 11
                                }
                            }
                            IconButton {
                                iconName: "note"
                                tip: modelData.note.length ? qsTr("Edit Note") : qsTr("Add Note")
                                implicitWidth: 32
                                implicitHeight: 32
                                onClicked: panel.editNoteRequested(modelData.id)
                            }
                            IconButton { iconName: "trash"; tip: qsTr("Remove Highlight"); implicitWidth: 32; implicitHeight: 32; onClicked: highlightRow.remove() }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 48
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        visible: highlightList.count === 0
                        text: qsTr("Select text to highlight it or add a note.")
                        color: Tokens.textSecondary
                    }
                }
                Button {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    enabled: highlightList.count > 0 || reader.vocabulary.length > 0
                    text: qsTr("Export Notes as Markdown…")
                    flat: true
                    Material.roundedScale: theme.radius
                    onClicked: panel.exportRequested()
                }
            }

            // Words looked up
            ListView {
                id: wordList
                clip: true
                model: reader.vocabulary
                keyNavigationEnabled: true
                activeFocusOnTab: true
                highlightMoveDuration: 0
                ScrollBar.vertical: ScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Words")
                Keys.onReturnPressed: if (currentItem) currentItem.clicked()
                Keys.onEnterPressed: if (currentItem) currentItem.clicked()
                Keys.onSpacePressed: if (currentItem) currentItem.expanded = !currentItem.expanded
                Keys.onDeletePressed: if (currentItem) currentItem.remove()
                delegate: ListRow {
                    id: wordRow
                    required property var modelData
                    required property int index
                    // The whole saved definition, instead of its first two lines.
                    property bool expanded: false
                    Accessible.name: modelData.word
                    Accessible.description: modelData.definition
                    function remove() {
                        reader.removeWord(index)
                        panel.removed(qsTr("Word removed"))
                    }
                    onClicked: panel.go(() => reader.goTo(modelData.chapter, modelData.offset))
                    contentItem: RowLayout {
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: modelData.word; font.pixelSize: 16; font.family: "serif"; font.weight: Font.DemiBold; color: Tokens.text }
                            Label {
                                Layout.fillWidth: true
                                visible: modelData.definition.length > 0
                                text: modelData.definition
                                color: Tokens.text
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                maximumLineCount: wordRow.expanded ? 1000 : 2
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.context
                                color: Tokens.textSecondary
                                font.pixelSize: 12
                                font.italic: true
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                        IconButton {
                            visible: modelData.definition.length > 0
                            iconName: "define"
                            tip: wordRow.expanded ? qsTr("Hide Definition") : qsTr("Show Definition")
                            implicitWidth: 32
                            implicitHeight: 32
                            checkable: false
                            Accessible.checkable: true
                            Accessible.checked: wordRow.expanded
                            onClicked: wordRow.expanded = !wordRow.expanded
                        }
                        IconButton { iconName: "trash"; tip: qsTr("Remove Word"); implicitWidth: 32; implicitHeight: 32; onClicked: wordRow.remove() }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: wordList.count === 0
                    text: qsTr("Words you look up are kept here, with the sentence you found them in.")
                    color: Tokens.textSecondary
                }
            }
        }
    }
}
