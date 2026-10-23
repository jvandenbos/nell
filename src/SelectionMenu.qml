import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// What to do with selected text: highlight it in a color, add a note, copy
// it, look it up, or search for it — the Apple Books bar with Kindle's
// dictionary in it.
Popup {
    id: menu
    signal highlight(string color)
    signal note()
    signal copy()
    signal define()
    signal search()
    signal remove()

    readonly property var colors: [
        { key: "yellow", color: "#ffd60a" },
        { key: "green", color: "#5fd068" },
        { key: "blue", color: "#5ab4fa" },
        { key: "pink", color: "#ff6fa5" },
        { key: "purple", color: "#b38cff" }
    ]
    readonly property string currentColor: {
        reader.annotationsChanged
        const id = reader.selectedHighlight
        if (!id.length)
            return ""
        const match = reader.highlights.find(h => h.id === id)
        return match ? match.color : ""
    }

    padding: 6
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    background: Rectangle {
        radius: theme.radius
        color: "#232327"
        border.width: 1
        border.color: "#38383f"
    }

    contentItem: RowLayout {
        spacing: 2

        Repeater {
            model: menu.colors
            delegate: AbstractButton {
                required property var modelData
                implicitWidth: 32
                implicitHeight: 32
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Highlight (H)")
                onClicked: menu.highlight(modelData.key)
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: 20
                        height: 20
                        radius: 10
                        color: modelData.color
                        border.width: menu.currentColor === modelData.key ? 2 : 0
                        border.color: "white"
                    }
                }
            }
        }
        AbstractButton {
            implicitWidth: 32
            implicitHeight: 32
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Underline")
            onClicked: menu.highlight("underline")
            contentItem: Text {
                text: "U"
                font.pixelSize: 17
                font.underline: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: menu.currentColor === "underline" ? "#ff453a" : "#e8e8ea"
            }
        }

        Rectangle { width: 1; height: 22; color: "#44444c"; Layout.leftMargin: 4; Layout.rightMargin: 4 }

        IconButton { iconName: "note"; tip: qsTr("Note (N)"); implicitWidth: 34; onClicked: menu.note() }
        IconButton { iconName: "copy"; tip: qsTr("Copy (Ctrl+C)"); implicitWidth: 34; onClicked: menu.copy() }
        IconButton { iconName: "define"; tip: qsTr("Look Up (D)"); implicitWidth: 34; onClicked: menu.define() }
        IconButton { iconName: "search"; tip: qsTr("Search Book"); implicitWidth: 34; onClicked: menu.search() }
        IconButton {
            visible: reader.selectedHighlight.length > 0
            iconName: "trash"
            tip: qsTr("Remove Highlight")
            implicitWidth: 34
            onClicked: menu.remove()
        }
    }
}
