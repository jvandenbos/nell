import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A book in progress: cover, where you are, and one click (or Enter, or
// Space) back into it.
AbstractButton {
    id: card
    property var book

    implicitHeight: 176
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    text: book.title
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Continue reading %1").arg(book.title)
    Accessible.description: qsTr("%1% read").arg(Math.round(book.progress * 100).toLocaleString(Qt.locale(), 'f', 0))
    Keys.onReturnPressed: card.clicked()
    Keys.onEnterPressed: card.clicked()

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    background: Rectangle {
        radius: theme.radius
        color: card.hovered ? Tokens.hover : Tokens.surfaceRaised
        border.width: 1
        border.color: Tokens.border
        Behavior on color { ColorAnimation { duration: Tokens.duration(120) } }
        FocusRing { control: card; controlRadius: theme.radius }
    }

    contentItem: RowLayout {
        spacing: 16

        BookCover {
            Layout.preferredWidth: card.width < 360 ? 72 : 96
            Layout.preferredHeight: card.width < 360 ? 108 : 144
            Layout.alignment: Qt.AlignVCenter
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
                color: Tokens.text
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: card.book.author
                color: Tokens.textSecondary
                elide: Text.ElideRight
            }
            Item { Layout.fillHeight: true }
            Label {
                text: qsTr("%1% read").arg(Math.round(card.book.progress * 100).toLocaleString(Qt.locale(), 'f', 0))
                color: Tokens.textSecondary
                font.pixelSize: 12
            }
            Rectangle {
                Layout.fillWidth: true
                height: 4
                radius: 2
                color: Tokens.surfaceSunken
                Rectangle {
                    width: parent.width * card.book.progress
                    height: parent.height
                    radius: 2
                    color: Tokens.accent
                }
            }
        }
    }
    padding: 16
}
