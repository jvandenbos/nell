import QtQuick

// The one keyboard-focus outline: 2 px in the accent, 2 px clear of the
// control, shown only for keyboard focus so a click doesn't leave a ring.
// Put it inside a control's background (or any item filling the control).
Rectangle {
    property Item control: parent ? parent.parent : null
    property real controlRadius: 0

    anchors.fill: parent
    anchors.margins: -4
    radius: controlRadius > 0 ? controlRadius + 4 : 0
    color: "transparent"
    border.width: 2
    border.color: Tokens.focus
    visible: control !== null && (control.visualFocus === true
                                  || (control.visualFocus === undefined && control.activeFocus))
    z: 10
}
