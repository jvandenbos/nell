import QtQuick
import QtQuick.Controls

// The one button on a screen that does the main thing, filled with the accent.
Button {
    id: button
    implicitHeight: 40
    leftPadding: 18
    rightPadding: 18
    background: Rectangle {
        radius: Math.min(8, theme.radius)
        color: button.down ? Qt.darker(theme.accent, 1.15) : button.hovered ? Qt.lighter(theme.accent, 1.08) : theme.accent
        opacity: button.enabled ? 1 : 0.4
    }
    contentItem: Label {
        text: button.text
        font.weight: Font.DemiBold
        color: theme.accentForeground
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
