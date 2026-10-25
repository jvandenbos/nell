import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A note on a highlight. Saved with Ctrl+Enter or the button; Escape or
// Cancel leaves it as it was — and a highlight made only to carry this note
// goes away with it.
Popup {
    id: editor
    property string highlightId
    property string quote
    // The highlight was made for this note a moment ago.
    property bool provisional: false

    function edit(id, isProvisional) {
        const match = reader.highlights.find(h => h.id === id)
        if (!match)
            return
        highlightId = id
        provisional = isProvisional === true
        quote = match.text
        field.text = match.note
        field.cursorPosition = field.length
        open()
    }

    // Once it's on screen: focus given while it's still opening doesn't stick.
    onOpened: field.forceActiveFocus()

    function save() {
        provisional = false
        reader.setNote(highlightId, field.text)
        close()
    }

    // The provisional highlight was the last thing done (the editor is
    // modal), so one undo takes exactly it away.
    onClosed: {
        if (provisional && reader.highlights.some(h => h.id === highlightId))
            reader.undo()
        provisional = false
    }

    width: Math.min(520, parent.width - 32)
    modal: true
    focus: true
    padding: 16
    closePolicy: Popup.CloseOnEscape
    Material.roundedScale: theme.radius
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Note")
    Overlay.modal: Rectangle { color: Tokens.scrim }
    background: Rectangle {
        radius: theme.radius
        color: Tokens.surfaceRaised
        border.width: 1
        border.color: Tokens.border
    }

    contentItem: ColumnLayout {
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Rectangle { Layout.fillHeight: true; implicitWidth: 3; radius: 1.5; color: Tokens.accent }
            Label {
                Layout.fillWidth: true
                text: editor.quote
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
                font.family: "serif"
                font.italic: true
                color: Tokens.textSecondary
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            TextArea {
                id: field
                focus: true
                placeholderText: qsTr("Write a note")
                wrapMode: TextEdit.Wrap
                Material.roundedScale: theme.radius
                Accessible.name: qsTr("Note")
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
            spacing: 8
            Label {
                text: qsTr("Ctrl+Enter to save")
                color: Tokens.textSecondary
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
