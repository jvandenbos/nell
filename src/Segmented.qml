import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A row of mutually exclusive choices, the selected one in the accent.
// Options are [value, label] pairs. Left and Right move the choice, as in a
// radio group.
RowLayout {
    id: control
    property var options: []
    property var value
    property string accessibleName
    signal picked(var value)

    spacing: 0
    Accessible.role: Accessible.Grouping
    Accessible.name: accessibleName

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
            text: modelData[1]
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.RadioButton
            Accessible.name: text
            Accessible.checkable: true
            Accessible.checked: selected
            onClicked: control.picked(modelData[0])
            Keys.onLeftPressed: if (index > 0) control.picked(control.options[index - 1][0])
            Keys.onRightPressed: if (index < control.options.length - 1) control.picked(control.options[index + 1][0])
            background: Rectangle {
                radius: Math.min(6, theme.radius)
                color: option.selected ? Tokens.accent : option.hovered ? Tokens.hover : Tokens.surfaceSunken
                FocusRing { control: option; controlRadius: parent.radius }
            }
            contentItem: Label {
                text: option.text
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                color: option.selected ? Tokens.accentForeground : Tokens.text
            }
        }
    }
}
