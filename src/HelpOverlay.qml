import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, one press of ? away.
Rectangle {
    id: overlay
    property bool reading: false
    signal dismissed()

    color: "#cc000000"
    z: 90

    MouseArea { anchors.fill: parent; onClicked: overlay.dismissed() }

    readonly property var readingKeys: [
        ["→  Space  PgDn", qsTr("Next page")],
        ["←  Shift+Space  PgUp", qsTr("Previous page")],
        ["]  [", qsTr("Next or previous chapter")],
        ["Home  End", qsTr("Start or end of the book")],
        ["T", qsTr("Contents, bookmarks, highlights, and words")],
        ["/  Ctrl+F", qsTr("Search the book")],
        ["A", qsTr("Appearance: theme, font, and size")],
        ["Ctrl++  Ctrl+−", qsTr("Bigger or smaller text")],
        ["B", qsTr("Bookmark this page")],
        ["H", qsTr("Highlight the selection")],
        ["N", qsTr("Add a note to the selection")],
        ["D", qsTr("Look up the selected word")],
        ["Ctrl+C", qsTr("Copy the selection")],
        ["Ctrl+Z  Ctrl+Shift+Z", qsTr("Undo or redo a highlight, note, or bookmark")],
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
        ["Q", qsTr("Quit")]
    ]

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(640, parent.width - 48)
        height: Math.min(content.implicitHeight + 48, parent.height - 48)
        radius: theme.radius
        color: "#1c1c20"
        border.width: 1
        border.color: "#34343a"

        Flickable {
            anchors.fill: parent
            anchors.margins: 24
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                width: parent.width
                spacing: 6

                Label {
                    text: overlay.reading ? qsTr("Reading shortcuts") : qsTr("Library shortcuts")
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
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
                            color: theme.accent
                            Layout.preferredWidth: 190
                        }
                        Label {
                            text: modelData[1]
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            opacity: 0.9
                        }
                    }
                }
            }
        }
    }
}
