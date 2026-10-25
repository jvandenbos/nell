import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Search the whole book; every match with the words around it. Up and Down
// preview matches with the panel still open; Enter takes the reader there
// and closes it.
Drawer {
    id: panel

    function searchFor(text) {
        field.text = text
        results.lastQuery = text
        reader.search(text)
        results.currentIndex = -1
        open()
    }

    edge: Qt.RightEdge
    width: Math.min(400, parent ? parent.width - 48 : 400)
    focus: true
    Material.roundedScale: Material.NotRounded
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Search")
    Overlay.modal: Rectangle { color: Tokens.scrim }
    background: Rectangle { color: Tokens.surfaceRaised; Rectangle { width: 1; height: parent.height; color: Tokens.border } }

    onOpened: { field.forceActiveFocus(); field.selectAll() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 0
            spacing: 8
            TextField {
                id: field
                Layout.fillWidth: true
                placeholderText: qsTr("Search this book")
                Material.roundedScale: theme.radius
                Accessible.name: qsTr("Search this book")
                onAccepted: {
                    if (results.count > 0 && text === results.lastQuery) {
                        results.show(Math.max(0, results.currentIndex))
                        panel.close()
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
            IconButton { iconName: "close"; tip: qsTr("Close (Esc)"); onClicked: panel.close() }
        }

        Label {
            Layout.leftMargin: 16
            visible: results.lastQuery.length > 0
            text: reader.searchResults.length >= 500 ? qsTr("500 or more matches")
                                                     : qsTr("%n matches", "", reader.searchResults.length)
            color: Tokens.textSecondary
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
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Matches")
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
                focusPolicy: Qt.NoFocus
                highlighted: ListView.isCurrentItem
                Accessible.name: modelData.before + modelData.match + modelData.after
                onClicked: results.show(index)
                onDoubleClicked: { results.show(index); panel.close() }
                contentItem: ColumnLayout {
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        textFormat: Text.StyledText
                        // Escaped, so the book's text can never be read as markup.
                        text: {
                            const escape = s => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                            return escape(modelData.before) + "<b><font color='" + Tokens.accentText + "'>" + escape(modelData.match)
                                   + "</font></b>" + escape(modelData.after)
                        }
                        color: Tokens.text
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        font.family: "serif"
                    }
                    Label {
                        text: qsTr("Page %1").arg(modelData.page.toLocaleString(Qt.locale(), 'f', 0))
                        color: Tokens.textSecondary
                        font.pixelSize: 11
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: results.count === 0 && results.lastQuery.length > 0
                text: qsTr("Nothing found")
                color: Tokens.textSecondary
            }
        }
    }

    onClosed: reader.clearSearch()
}
