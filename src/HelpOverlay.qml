import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, one press of ? away. Modal, so the keys pressed while it's
// up don't turn pages behind it; Esc or a click outside closes it, and focus
// goes back to whatever had it.
Popup {
    id: overlay
    property bool reading: false
    property Item returnFocus: null

    function show(from) {
        returnFocus = from
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(640, parent.width - 32)
    height: Math.min(content.implicitHeight + 2 * padding + 8, parent.height - 32)
    padding: 24
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    Accessible.role: Accessible.Dialog
    Accessible.name: title.text

    onOpened: flick.forceActiveFocus()
    onClosed: if (returnFocus) returnFocus.forceActiveFocus()

    Overlay.modal: Rectangle { color: Tokens.scrim }

    background: Rectangle {
        radius: theme.radius
        color: Tokens.surfaceRaised
        border.width: 1
        border.color: Tokens.border
    }

    readonly property var readingKeys: [
        ["→  Space  PgDn", qsTr("Next page")],
        ["←  Shift+Space  PgUp", qsTr("Previous page")],
        ["]  [", qsTr("Next or previous chapter")],
        ["Home  End", qsTr("Start or end of the book")],
        ["Ctrl+G", qsTr("Go to a page or percent")],
        ["T", qsTr("Contents, bookmarks, highlights, and words")],
        ["/  Ctrl+F", qsTr("Search the book")],
        ["A", qsTr("Appearance: theme, font, and size")],
        ["Ctrl++  Ctrl+−", qsTr("Bigger or smaller text")],
        ["P", qsTr("Change what the progress corner shows")],
        ["B", qsTr("Bookmark this page")],
        ["H", qsTr("Highlight the selection in the last color used")],
        ["N", qsTr("Add a note to the selection")],
        ["D", qsTr("Look up the selected word")],
        ["Ctrl+C", qsTr("Copy the selection")],
        ["Ctrl+Z  Ctrl+Shift+Z", qsTr("Undo or redo a highlight, note, or bookmark")],
        ["Tab", qsTr("Show the controls and move through them")],
        ["Esc", qsTr("Close a panel, or go back to the library")]
    ]
    readonly property var libraryKeys: [
        ["Space  Enter", qsTr("Open the selected book")],
        ["Arrows", qsTr("Move between books")],
        ["Ctrl+O", qsTr("Add books")],
        ["/  Ctrl+F", qsTr("Find in the library")],
        ["Delete", qsTr("Remove the selected book from the library")]
    ]
    readonly property var commonKeys: [
        ["F11  F", qsTr("Full screen")],
        ["?", qsTr("Show or hide these shortcuts")],
        ["Ctrl+Q  Q", qsTr("Quit")]
    ]

    contentItem: Flickable {
        id: flick
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        Keys.onUpPressed: flick.contentY = Math.max(0, flick.contentY - 40)
        Keys.onDownPressed: flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height), flick.contentY + 40)

        ColumnLayout {
            id: content
            width: flick.width
            spacing: 8

            Label {
                id: title
                text: overlay.reading ? qsTr("Reading shortcuts") : qsTr("Library shortcuts")
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: Tokens.text
                Layout.bottomMargin: 8
            }

            Repeater {
                model: (overlay.reading ? overlay.readingKeys : overlay.libraryKeys).concat(overlay.commonKeys)
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 16
                    Label {
                        text: modelData[0]
                        font.family: "monospace"
                        color: Tokens.accentText
                        Layout.preferredWidth: Math.min(190, content.width * 0.4)
                        wrapMode: Text.Wrap
                    }
                    Label {
                        text: modelData[1]
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        color: Tokens.text
                    }
                }
            }
        }
    }
}
