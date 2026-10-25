import QtQuick
import QtQuick.Controls

// The one button on a screen that does the main thing, filled with the accent.
Button {
    id: button
    implicitHeight: 40
    leftPadding: 16
    rightPadding: 16
    Accessible.role: Accessible.Button
    Accessible.name: text
    background: Rectangle {
        radius: Math.min(8, theme.radius)
        color: button.down ? Qt.darker(Tokens.accent, 1.15) : button.hovered ? Qt.lighter(Tokens.accent, 1.08) : Tokens.accent
        opacity: button.enabled ? 1 : 0.4
        FocusRing { control: button; controlRadius: parent.radius }
    }
    contentItem: Label {
        text: button.text
        font.weight: Font.DemiBold
        color: Tokens.accentForeground
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
