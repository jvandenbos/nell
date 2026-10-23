import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A book in progress: cover, where you are, and one click back into it.
Rectangle {
    id: card
    property var book
    signal open()

    height: 176
    radius: theme.radius
    color: hover.hovered ? "#1d1d22" : "#17171b"
    border.width: 1
    border.color: "#26262c"
    Behavior on color { ColorAnimation { duration: 120 } }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        BookCover {
            Layout.preferredWidth: 96
            Layout.preferredHeight: 144
            source: card.book.cover
            title: card.book.title
            author: card.book.author
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            Label {
                Layout.fillWidth: true
                text: card.book.title
                font.pixelSize: 16
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: card.book.author
                color: "#a0a0a8"
                elide: Text.ElideRight
            }
            Item { Layout.fillHeight: true }
            Label {
                text: qsTr("%1% read").arg(Math.round(card.book.progress * 100).toLocaleString(Qt.locale(), 'f', 0))
                color: "#a0a0a8"
                font.pixelSize: 12
            }
            Rectangle {
                Layout.fillWidth: true
                height: 4
                radius: 2
                color: "#2c2c33"
                Rectangle {
                    width: parent.width * card.book.progress
                    height: parent.height
                    radius: 2
                    color: theme.accent
                }
            }
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: card.open() }
}
