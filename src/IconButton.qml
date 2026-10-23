import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A flat toolbar button holding an icon, or a word when there is no icon
// that says it better ("Aa").
ToolButton {
    id: button
    property string iconName
    property color iconColor: Material.foreground
    property bool iconFilled: false
    property string tip

    implicitWidth: 40
    implicitHeight: 40
    padding: 0
    Material.roundedScale: theme.radius
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 600

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
    }
}
