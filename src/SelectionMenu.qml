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

    readonly property string currentColor: {
        reader.annotationsChanged
        const id = reader.selectedHighlight
        if (!id.length)
            return ""
        const match = reader.highlights.find(h => h.id === id)
        return match ? match.color : ""
    }

    padding: 4
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Selection")
    background: Rectangle {
        radius: theme.radius
        color: Tokens.surfaceRaised
        border.width: 1
        border.color: Tokens.border
    }

    contentItem: RowLayout {
        spacing: 0

        Repeater {
            model: Tokens.highlights
            delegate: AbstractButton {
                id: swatch
                required property var modelData
                implicitWidth: 36
                implicitHeight: 36
                focusPolicy: Qt.TabFocus
                // The color H uses carries its shortcut.
                readonly property string tip: modelData.key === prefs.highlightColor ? qsTr("%1 (H)").arg(modelData.name) : modelData.name
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: tip
                Accessible.role: Accessible.Button
                Accessible.name: modelData.name
                Accessible.checkable: true
                Accessible.checked: menu.currentColor === modelData.key
                onClicked: menu.highlight(modelData.key)
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: 22
                        height: 22
                        radius: 11
                        color: swatch.modelData.color
                        border.width: menu.currentColor === swatch.modelData.key ? 2 : 0
                        border.color: Tokens.text
                    }
                    FocusRing { control: swatch; anchors.margins: 0; controlRadius: 16 }
                }
            }
        }
        AbstractButton {
            id: underline
            implicitWidth: 36
            implicitHeight: 36
            focusPolicy: Qt.TabFocus
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Underline")
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Underline")
            Accessible.checkable: true
            Accessible.checked: menu.currentColor === "underline"
            onClicked: menu.highlight("underline")
            contentItem: Text {
                text: "U"
                font.pixelSize: 17
                font.underline: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: menu.currentColor === "underline" ? Tokens.underline : Tokens.text
                FocusRing { control: underline; anchors.margins: 0; controlRadius: 6 }
            }
        }

        Rectangle { implicitWidth: 1; implicitHeight: 24; color: Tokens.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }

        IconButton { iconName: "note"; tip: qsTr("Note (N)"); implicitWidth: 36; implicitHeight: 36; onClicked: menu.note() }
        IconButton { iconName: "copy"; tip: qsTr("Copy (Ctrl+C)"); implicitWidth: 36; implicitHeight: 36; onClicked: menu.copy() }
        IconButton { iconName: "define"; tip: qsTr("Look Up (D)"); implicitWidth: 36; implicitHeight: 36; onClicked: menu.define() }
        IconButton { iconName: "search"; tip: qsTr("Search Book"); implicitWidth: 36; implicitHeight: 36; onClicked: menu.search() }
        IconButton {
            visible: reader.selectedHighlight.length > 0
            iconName: "trash"
            tip: qsTr("Remove Highlight")
            implicitWidth: 36
            implicitHeight: 36
            onClicked: menu.remove()
        }
    }
}
