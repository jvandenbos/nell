import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A flat toolbar button holding an icon, or a word when there is no icon
// that says it better ("Aa").
ToolButton {
    id: button
    property string iconName
    property color iconColor: Tokens.text
    property bool iconFilled: false
    property string tip
    // What a screen reader says: the tip without its "(Esc)" shortcut.
    property string accessibleName: tip.replace(/\s*\([^)]*\)$/, "")

    implicitWidth: 40
    implicitHeight: 40
    padding: 0
    // A click leaves focus where it was (the page), so the arrow keys keep
    // turning pages; Tab still reaches the button.
    focusPolicy: Qt.TabFocus
    Material.roundedScale: theme.radius
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 600
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName.length ? accessibleName : text

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            visible: button.iconName.length > 0
            name: button.iconName
            color: button.iconColor
            filled: button.iconFilled
        }
        Text {
            anchors.centerIn: parent
            visible: button.iconName.length === 0
            text: button.text
            color: button.iconColor
            font.pixelSize: 17
            font.family: "serif"
        }
        FocusRing { control: button; controlRadius: Math.min(8, theme.radius) }
    }
}
