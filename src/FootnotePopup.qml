import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A footnote where it's referenced, as Apple Books shows one: the note's
// text in a card beside the reference, with a way to the note itself.
Popup {
    id: card
    property string href
    property string noteText

    // Shows the note for a reference clicked at (x, y) in the view.
    function show(target, x, y) {
        const note = reader.footnote(target)
        if (!note.text)
            return
        href = target
        noteText = note.text
        card.x = Math.max(12, Math.min(parent.width - width - 12, x - width / 2))
        card.y = y + 16 + height < parent.height ? y + 16 : Math.max(12, y - height - 16)
        open()
    }

    width: Math.min(440, parent.width - 24)
    height: Math.min(implicitHeight, parent.height - 24)
    padding: 16
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Footnote")
    background: Rectangle {
        radius: theme.radius
        color: Tokens.surfaceRaised
        border.width: 1
        border.color: Tokens.border
    }

    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("Footnote")
                font.weight: Font.DemiBold
                color: Tokens.text
            }
            IconButton { iconName: "close"; tip: qsTr("Close (Esc)"); implicitWidth: 32; implicitHeight: 32; onClicked: card.close() }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.min(body.implicitHeight, 320)
            clip: true
            contentWidth: availableWidth
            TextEdit {
                id: body
                width: parent.width
                text: card.noteText
                readOnly: true
                selectByMouse: true
                wrapMode: Text.Wrap
                color: Tokens.text
                selectionColor: Tokens.accent
                selectedTextColor: Tokens.accentForeground
                font.family: "serif"
                font.pixelSize: 15
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                Keys.onEscapePressed: card.close()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                flat: true
                text: qsTr("Go to Note")
                onClicked: { card.close(); reader.follow(card.href) }
            }
        }
    }
}
