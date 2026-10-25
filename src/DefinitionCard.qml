import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A word's meaning, from Wiktionary. Every word looked up is kept in the
// book's Words list with the sentence it came from, as Kindle's Vocabulary
// Builder does.
Popup {
    id: card
    signal searchRequested(string word)
    property bool saved: false
    // What was asked for, so Retry asks again for exactly that.
    property string lookedUp

    function lookUp(text) {
        saved = false
        lookedUp = text
        dictionary.define(text, reader.language)
        open()
    }

    width: Math.min(420, parent.width - 24)
    height: Math.min(implicitHeight, 420)
    padding: 16
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    Accessible.role: Accessible.Dialog
    Accessible.name: dictionary.word
    background: Rectangle {
        radius: theme.radius
        color: Tokens.surfaceRaised
        border.width: 1
        border.color: Tokens.border
    }

    Connections {
        target: dictionary
        function onChanged() {
            if (card.opened && !card.saved && !dictionary.busy && dictionary.entries.length > 0) {
                card.saved = true
                reader.addWord(dictionary.word, dictionary.summary())
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: dictionary.word
                font.pixelSize: 22
                font.family: "serif"
                font.weight: Font.DemiBold
                color: Tokens.text
                elide: Text.ElideRight
            }
            BusyIndicator { running: dictionary.busy; visible: running; implicitWidth: 28; implicitHeight: 28 }
            IconButton { iconName: "close"; tip: qsTr("Close (Esc)"); implicitWidth: 32; implicitHeight: 32; onClicked: card.close() }
        }

        RowLayout {
            visible: dictionary.error.length > 0
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: dictionary.error
                wrapMode: Text.Wrap
                color: Tokens.text
            }
            Button {
                text: qsTr("Retry")
                flat: true
                enabled: !dictionary.busy
                onClicked: dictionary.define(card.lookedUp, reader.language)
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.min(definitions.implicitHeight, 280)
            visible: dictionary.entries.length > 0
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                id: definitions
                width: parent.width
                spacing: 8
                Repeater {
                    model: dictionary.entries
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            text: modelData.partOfSpeech
                            font.italic: true
                            color: Tokens.accentText
                        }
                        Repeater {
                            model: modelData.definitions
                            delegate: Label {
                                required property string modelData
                                required property int index
                                Layout.fillWidth: true
                                text: (index + 1).toLocaleString(Qt.locale(), 'f', 0) + ". " + modelData
                                wrapMode: Text.Wrap
                                color: Tokens.text
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: card.saved ? qsTr("Wiktionary · Saved to Words") : qsTr("Wiktionary")
                color: Tokens.textSecondary
                font.pixelSize: 12
            }
            Button {
                flat: true
                text: qsTr("Search Book")
                onClicked: card.searchRequested(dictionary.word)
            }
        }
    }
}
