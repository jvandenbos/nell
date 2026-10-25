import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Ctrl+G: straight to a page, or to a percentage of the way through.
Popup {
    id: dialog
    property bool pdf: false
    property string problem

    function go() {
        const text = field.text.trim()
        const percent = /^(\d+(?:[.,]\d+)?)\s*%$/.exec(text)
        if (percent) {
            const value = Number(percent[1].replace(",", "."))
            if (value >= 0 && value <= 100) {
                reader.goToFraction(value / 100)
                close()
                return
            }
        } else if (/^\d+$/.test(text)) {
            const page = Number(text)
            if (page >= 1 && page <= reader.pageCount) {
                reader.goToPage(page - 1)
                close()
                return
            }
        }
        problem = qsTr("Type a page from 1 to %1, or a percentage like 40%.").arg(reader.pageCount.toLocaleString(Qt.locale(), 'f', 0))
    }

    width: Math.min(360, parent ? parent.width - 32 : 360)
    padding: 16
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    Accessible.role: Accessible.Dialog
    Accessible.name: heading.text

    onAboutToShow: { problem = ""; field.text = "" }
    onOpened: field.forceActiveFocus()

    Overlay.modal: Rectangle { color: Tokens.scrim }
    background: Rectangle {
        radius: theme.radius
        color: Tokens.surfaceRaised
        border.width: 1
        border.color: Tokens.border
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                id: heading
                Layout.fillWidth: true
                text: qsTr("Go To")
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: Tokens.text
            }
            IconButton { iconName: "close"; tip: qsTr("Close (Esc)"); implicitWidth: 32; implicitHeight: 32; onClicked: dialog.close() }
        }
        TextField {
            id: field
            Layout.fillWidth: true
            placeholderText: qsTr("Page (1–%1) or percent").arg(reader.pageCount.toLocaleString(Qt.locale(), 'f', 0))
            Material.roundedScale: theme.radius
            Accessible.name: placeholderText
            inputMethodHints: Qt.ImhPreferNumbers
            onAccepted: dialog.go()
            onTextChanged: dialog.problem = ""
        }
        Label {
            Layout.fillWidth: true
            visible: dialog.problem.length > 0
            text: dialog.problem
            wrapMode: Text.Wrap
            color: Tokens.danger
            font.pixelSize: 12
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Cancel"); flat: true; onClicked: dialog.close() }
            AccentButton { text: qsTr("Go"); onClicked: dialog.go() }
        }
    }
}
