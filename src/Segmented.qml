import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A row of mutually exclusive choices, the selected one in the accent.
// Options are [value, label] pairs.
RowLayout {
    id: control
    property var options: []
    property var value
    signal picked(var value)

    spacing: 0

    Repeater {
        model: control.options
        delegate: AbstractButton {
            id: option
            required property var modelData
            required property int index
            readonly property bool selected: control.value === modelData[0]
                                              || (typeof control.value === "number" && Math.abs(control.value - modelData[0]) < 0.001)
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            implicitHeight: 32
            onClicked: control.picked(modelData[0])
            background: Rectangle {
                radius: Math.min(6, theme.radius)
                color: option.selected ? theme.accent : option.hovered ? "#2e2e34" : "#26262b"
            }
            contentItem: Label {
                text: option.modelData[1]
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                color: option.selected ? theme.accentForeground : "#d8d8dc"
            }
        }
    }
}
