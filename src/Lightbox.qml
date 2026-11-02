import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A picture from the page, as big as the window allows, on a scrim. Wheel,
// + and − zoom; drag to look around; Esc, a click beside it, or the close
// button goes back to the page.
Popup {
    id: box
    property string source
    property real zoom: 1

    function show(path) {
        source = "file://" + path
        zoom = 1
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: parent.width
    height: parent.height
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Picture")
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.85) }
    background: Item {}

    onOpened: flick.forceActiveFocus()

    contentItem: Item {
        // Beside the picture: closes.
        MouseArea { anchors.fill: parent; onClicked: box.close() }

        Flickable {
            id: flick
            anchors.fill: parent
            contentWidth: Math.max(width, picture.width * box.zoom)
            contentHeight: Math.max(height, picture.height * box.zoom)
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Keys.onEscapePressed: box.close()
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)
                    box.zoom = Math.min(6, box.zoom * 1.25)
                else if (event.key === Qt.Key_Minus)
                    box.zoom = Math.max(1, box.zoom / 1.25)
                else if (event.key === Qt.Key_0)
                    box.zoom = 1
                else
                    return
                event.accepted = true
            }
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => box.zoom = Math.max(1, Math.min(6, box.zoom * (event.angleDelta.y > 0 ? 1.1 : 1 / 1.1)))
            }

            Item {
                width: flick.contentWidth
                height: flick.contentHeight
                Image {
                    id: picture
                    anchors.centerIn: parent
                    source: box.source
                    // Fits the window at zoom 1; the window's size is the
                    // picture's room, whatever its own.
                    width: Math.min(sourceSize.width, box.width - 48)
                    height: Math.min(sourceSize.height, box.height - 48)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    asynchronous: true
                    scale: box.zoom
                    Behavior on scale { NumberAnimation { duration: Tokens.duration(120) } }
                    // A click on the picture itself does nothing but keep it.
                    MouseArea { anchors.fill: parent; cursorShape: box.zoom > 1 ? Qt.OpenHandCursor : Qt.ArrowCursor }
                }
            }
        }

        IconButton {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 12
            iconName: "close"
            iconColor: "white"
            tip: qsTr("Close (Esc)")
            onClicked: box.close()
        }
        Label {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.margins: 16
            visible: box.zoom > 1.01
            text: Math.round(box.zoom * 100).toLocaleString(Qt.locale(), 'f', 0) + "%"
            color: "white"
            font.pixelSize: 12
        }
    }
}
