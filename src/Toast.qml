import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One line at the bottom of the window that says what happened or what's
// wrong. News goes away by itself; a failure stays until it's dismissed, so
// it can't vanish while the reader is looking elsewhere. A popup in the
// overlay, so it shows (and takes clicks) above an open drawer.
//
// show(message, { error, details: [lines], actionText, action, timeout })
Popup {
    id: toast
    property string text
    property bool error: false
    property var details: []
    property bool expanded: false
    property string actionText
    property var action: null

    function show(message, options) {
        if (!message || !message.length)
            return
        const o = options || {}
        text = message
        error = o.error === true
        details = o.details || []
        expanded = false
        actionText = o.actionText || ""
        action = o.action || null
        hide.interval = o.timeout || 4500
        open()
        if (error)
            hide.stop()
        else
            hide.restart()
    }

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: parent.height - height - 72
    width: Math.min(implicitWidth, parent.width - 32)
    padding: 8
    leftPadding: 16
    modal: false
    focus: false
    closePolicy: Popup.NoAutoClose
    z: 100
    Material.roundedScale: theme.radius
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Tokens.duration(180) } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Tokens.duration(180) } }

    Timer { id: hide; onTriggered: toast.close() }

    background: Rectangle {
        radius: theme.radius
        color: Tokens.surfaceRaised
        border.width: 1
        border.color: toast.error ? Tokens.danger : Tokens.border
        // Hovering keeps it up; the reader is reading it.
        HoverHandler { onHoveredChanged: if (!toast.error) hovered ? hide.stop() : hide.restart() }
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            spacing: 8
            Label {
                Layout.maximumWidth: 520
                Layout.fillWidth: true
                text: toast.text
                wrapMode: Text.Wrap
                color: Tokens.text
            }
            Button {
                visible: toast.details.length > 0
                flat: true
                text: toast.expanded ? qsTr("Hide details") : qsTr("Show details")
                onClicked: toast.expanded = !toast.expanded
            }
            Button {
                visible: toast.actionText.length > 0
                flat: true
                text: toast.actionText
                Material.foreground: Tokens.accentText
                onClicked: {
                    const run = toast.action
                    toast.close()
                    if (run)
                        run()
                }
            }
            IconButton {
                iconName: "close"
                tip: qsTr("Dismiss")
                implicitWidth: 32
                implicitHeight: 32
                onClicked: toast.close()
            }
        }

        ScrollView {
            visible: toast.expanded
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(detailColumn.implicitHeight, 160)
            clip: true
            contentWidth: availableWidth
            Column {
                id: detailColumn
                width: parent.width
                spacing: 4
                Repeater {
                    model: toast.details
                    Label {
                        required property string modelData
                        width: parent.width
                        text: modelData
                        wrapMode: Text.Wrap
                        color: Tokens.textSecondary
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
