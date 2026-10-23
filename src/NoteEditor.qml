import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A note on a highlight. Saved with Ctrl+Enter or the button; Escape leaves
// it as it was.
Popup {
    id: editor
    property string highlightId
    property string quote

    function edit(id) {
        const match = reader.highlights.find(h => h.id === id)
        if (!match)
            return
        highlightId = id
        quote = match.text
        field.text = match.note
        open()
        field.forceActiveFocus()
        field.cursorPosition = field.length
    }

    function save() {
        reader.setNote(highlightId, field.text)
        close()
    }

    width: Math.min(520, parent.width - 48)
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape
    Material.roundedScale: theme.radius
    background: Rectangle {
        radius: theme.radius
        color: "#1f1f23"
        border.width: 1
        border.color: "#38383f"
    }

    contentItem: ColumnLayout {
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle { Layout.fillHeight: true; width: 3; radius: 1.5; color: theme.accent }
            Label {
                Layout.fillWidth: true
                text: editor.quote
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
                font.family: "serif"
                font.italic: true
                color: "#c8c8ce"
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            TextArea {
                id: field
                placeholderText: qsTr("Write a note")
                wrapMode: TextEdit.Wrap
                Material.roundedScale: theme.radius
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                        editor.save()
                        event.accepted = true
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Ctrl+Enter to save")
                color: "#8a8a92"
                font.pixelSize: 12
                Layout.fillWidth: true
            }
            Button { text: qsTr("Cancel"); flat: true; onClicked: editor.close() }
            AccentButton {
                text: qsTr("Save")
                onClicked: editor.save()
            }
        }
    }
}
