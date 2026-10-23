import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Search the whole book; every match with the words around it.
Drawer {
    id: panel
    property color ink
    property color paper

    function searchFor(text) {
        field.text = text
        results.lastQuery = text
        reader.search(text)
        results.currentIndex = -1
        open()
    }

    edge: Qt.RightEdge
    width: Math.min(400, parent ? parent.width - 48 : 400)
    Material.roundedScale: Material.NotRounded
    background: Rectangle { color: "#18181b"; Rectangle { width: 1; height: parent.height; color: "#2c2c32" } }

    onOpened: { field.forceActiveFocus(); field.selectAll() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        TextField {
            id: field
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 0
            placeholderText: qsTr("Search this book")
            Material.roundedScale: theme.radius
            onAccepted: {
                if (results.count > 0 && text === results.lastQuery) {
                    results.show(Math.max(0, results.currentIndex))
                    return
                }
                results.lastQuery = text
                reader.search(text)
                // Nothing moves until a result is picked.
                results.currentIndex = -1
            }
            Keys.onDownPressed: results.show(Math.min(results.count - 1, results.currentIndex + 1))
            Keys.onUpPressed: results.show(Math.max(0, results.currentIndex - 1))
            Keys.onEscapePressed: panel.close()
        }

        Label {
            Layout.leftMargin: 16
            visible: results.lastQuery.length > 0
            text: reader.searchResults.length >= 500 ? qsTr("500 or more matches")
                                                     : qsTr("%n matches", "", reader.searchResults.length)
            color: "#8a8a92"
            font.pixelSize: 12
        }

        ListView {
            id: results
            property string lastQuery
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: reader.searchResults
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}
            currentIndex: -1
            function show(index) {
                if (index < 0 || index >= count)
                    return
                currentIndex = index
                reader.goToResult(index)
            }

            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                onClicked: results.show(index)
                contentItem: ColumnLayout {
                    spacing: 3
                    Label {
                        Layout.fillWidth: true
                        textFormat: Text.StyledText
                        // Escaped, so the book's text can never be read as markup.
                        text: {
                            const escape = s => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                            return escape(modelData.before) + "<b><font color='" + theme.accent + "'>" + escape(modelData.match)
                                   + "</font></b>" + escape(modelData.after)
                        }
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        font.family: "serif"
                    }
                    Label {
                        text: qsTr("Page %1").arg(modelData.page.toLocaleString(Qt.locale(), 'f', 0))
                        color: "#8a8a92"
                        font.pixelSize: 11
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: results.count === 0 && results.lastQuery.length > 0
                text: qsTr("Nothing found")
                color: "#8a8a92"
            }
        }
    }

    onClosed: reader.clearSearch()
}
