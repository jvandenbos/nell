import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Everything to jump to: the chapters, your bookmarks, your highlights and
// notes, and the words you looked up.
Drawer {
    id: panel
    property color ink
    property color paper
    signal exportRequested()

    edge: Qt.LeftEdge
    width: Math.min(400, parent ? parent.width - 48 : 400)
    Material.roundedScale: Material.NotRounded
    background: Rectangle { color: "#18181b"; Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#2c2c32" } }

    onOpened: tocList.positionViewAtIndex(Math.max(0, reader.tocIndex), ListView.Center)

    function go(action) {
        action()
        close()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 8
            spacing: 12
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
                Label { Layout.fillWidth: true; text: reader.title; font.weight: Font.DemiBold; elide: Text.ElideRight }
                Label { Layout.fillWidth: true; text: reader.author; color: "#a0a0a8"; elide: Text.ElideRight }
                Label {
                    text: qsTr("%1% read").arg(Math.round(reader.progress * 100).toLocaleString(Qt.locale(), 'f', 0))
                    color: "#a0a0a8"
                    font.pixelSize: 12
                }
            }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            Material.background: "#18181b"
            TabButton { text: qsTr("Contents"); font.pixelSize: 13; leftPadding: 4; rightPadding: 4 }
            TabButton { text: qsTr("Bookmarks"); font.pixelSize: 13; leftPadding: 4; rightPadding: 4 }
            TabButton { text: qsTr("Notes"); font.pixelSize: 13; leftPadding: 4; rightPadding: 4 }
            TabButton { text: qsTr("Words"); font.pixelSize: 13; leftPadding: 4; rightPadding: 4 }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // Contents
            ListView {
                id: tocList
                clip: true
                model: reader.toc
                currentIndex: reader.tocIndex
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    highlighted: index === reader.tocIndex
                    onClicked: panel.go(() => reader.goToToc(index))
                    contentItem: RowLayout {
                        spacing: 12
                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: modelData.level * 16
                            text: modelData.title
                            elide: Text.ElideRight
                            font.weight: index === reader.tocIndex ? Font.DemiBold : Font.Normal
                            color: index === reader.tocIndex ? theme.accent : "#e6e6ea"
                        }
                        Label {
                            text: modelData.page.toLocaleString(Qt.locale(), 'f', 0)
                            color: "#8a8a92"
                            font.pixelSize: 12
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: tocList.count === 0
                    text: qsTr("This book has no table of contents.")
                    color: "#8a8a92"
                }
            }

            // Bookmarks
            ListView {
                id: bookmarkList
                clip: true
                model: reader.bookmarks
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    onClicked: panel.go(() => reader.goTo(modelData.chapter, modelData.offset))
                    contentItem: RowLayout {
                        spacing: 10
                        Icon { name: "bookmark"; filled: true; color: "#e5484d"; size: 16 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { Layout.fillWidth: true; text: modelData.title; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.snippet
                                color: "#a0a0a8"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                        Label { text: modelData.page.toLocaleString(Qt.locale(), 'f', 0); color: "#8a8a92"; font.pixelSize: 12 }
                        IconButton { iconName: "close"; tip: qsTr("Remove"); implicitWidth: 32; implicitHeight: 32; onClicked: reader.removeBookmark(modelData.index) }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: bookmarkList.count === 0
                    text: qsTr("No bookmarks yet. Press B to mark a page.")
                    color: "#8a8a92"
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
                    ScrollBar.vertical: ScrollBar {}
                    section.property: "section"
                    section.delegate: Label {
                        required property string section
                        width: ListView.view.width
                        leftPadding: 16
                        topPadding: 14
                        bottomPadding: 4
                        text: section
                        color: "#8a8a92"
                        font.pixelSize: 11
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                        elide: Text.ElideRight
                    }
                    delegate: ItemDelegate {
                        required property var modelData
                        width: ListView.view.width
                        onClicked: panel.go(() => reader.goToHighlight(modelData.id))
                        contentItem: RowLayout {
                            spacing: 10
                            Rectangle {
                                Layout.fillHeight: true
                                width: 3
                                radius: 1.5
                                color: ({ yellow: "#ffd60a", green: "#5fd068", blue: "#5ab4fa", pink: "#ff6fa5",
                                          purple: "#b38cff", underline: "#ff453a" })[modelData.color] || "#ffd60a"
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.text
                                    font.family: "serif"
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 4
                                    elide: Text.ElideRight
                                }
                                Label {
                                    visible: modelData.note.length > 0
                                    Layout.fillWidth: true
                                    text: modelData.note
                                    color: theme.accent
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                    font.pixelSize: 13
                                }
                                Label {
                                    text: qsTr("Page %1").arg(modelData.page.toLocaleString(Qt.locale(), 'f', 0))
                                    color: "#8a8a92"
                                    font.pixelSize: 11
                                }
                            }
                            IconButton { iconName: "trash"; tip: qsTr("Remove"); implicitWidth: 32; implicitHeight: 32; onClicked: reader.removeHighlight(modelData.id) }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 48
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        visible: highlightList.count === 0
                        text: qsTr("Select text to highlight it or add a note.")
                        color: "#8a8a92"
                    }
                }
                Button {
                    Layout.fillWidth: true
                    Layout.margins: 12
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
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    onClicked: panel.go(() => reader.goTo(modelData.chapter, modelData.offset))
                    contentItem: RowLayout {
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Label { text: modelData.word; font.pixelSize: 16; font.family: "serif"; font.weight: Font.DemiBold }
                            Label {
                                Layout.fillWidth: true
                                visible: modelData.definition.length > 0
                                text: modelData.definition
                                color: "#c8c8ce"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.context
                                color: "#8a8a92"
                                font.pixelSize: 12
                                font.italic: true
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                        IconButton { iconName: "trash"; tip: qsTr("Remove"); implicitWidth: 32; implicitHeight: 32; onClicked: reader.removeWord(index) }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: wordList.count === 0
                    text: qsTr("Words you look up are kept here, with the sentence you found them in.")
                    color: "#8a8a92"
                }
            }
        }
    }
}
