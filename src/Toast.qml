import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// One line at the bottom of the window that says what happened or what's wrong.
Rectangle {
    id: toast
    property string text
    function show(message) {
        if (!message || !message.length)
            return
        text = message
        opacity = 1
        hide.restart()
    }

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 72
    width: Math.min(label.implicitWidth + 40, parent.width - 48)
    height: label.implicitHeight + 22
    radius: theme.radius
    color: "#26262a"
    border.width: 1
    border.color: "#3a3a40"
    opacity: 0
    visible: opacity > 0
    z: 100
    Behavior on opacity { NumberAnimation { duration: 180 } }

    Timer { id: hide; interval: 4500; onTriggered: toast.opacity = 0 }

    Label {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, toast.parent.width - 88)
        text: toast.text
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: "#ececec"
    }

    MouseArea { anchors.fill: parent; onClicked: toast.opacity = 0 }
}
