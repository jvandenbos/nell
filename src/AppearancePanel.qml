import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Theme, typeface, size, and layout, as under Apple Books' "Aa". Every change
// shows on the page behind it straight away.
Popup {
    id: panel
    property bool pdf: false

    readonly property var themeNames: ({
        original: qsTr("Original"), paper: qsTr("Paper"), calm: qsTr("Calm"),
        quiet: qsTr("Quiet"), night: qsTr("Night"), omarchy: "Omarchy"
    })
    readonly property var themeColors: ({
        original: ["#ffffff", "#1d1d1f"], paper: ["#f7f2e8", "#2c2a26"], calm: ["#ebdfc6", "#3b2f22"],
        quiet: ["#3b3b3e", "#d6d6d6"], night: ["#111111", "#c4c4c4"], omarchy: [theme.background, theme.foreground]
    })

    width: 340
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    background: Rectangle {
        radius: theme.radius
        color: "#1f1f23"
        border.width: 1
        border.color: "#38383f"
    }

    contentItem: ColumnLayout {
        spacing: 16

        // Size
        RowLayout {
            visible: !panel.pdf
            Layout.fillWidth: true
            spacing: 8
            Button {
                Layout.fillWidth: true
                flat: true
                text: "A"
                font.pixelSize: 14
                font.family: "serif"
                enabled: prefs.fontSize > 12
                onClicked: prefs.fontSize -= 1
                Material.roundedScale: theme.radius
            }
            Label {
                text: prefs.fontSize.toLocaleString(Qt.locale(), 'f', 0)
                color: "#a0a0a8"
                Layout.preferredWidth: 30
                horizontalAlignment: Text.AlignHCenter
            }
            Button {
                Layout.fillWidth: true
                flat: true
                text: "A"
                font.pixelSize: 22
                font.family: "serif"
                enabled: prefs.fontSize < 44
                onClicked: prefs.fontSize += 1
                Material.roundedScale: theme.radius
            }
        }

        // Themes
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            rowSpacing: 10
            columnSpacing: 10
            Repeater {
                model: prefs.readingThemes
                delegate: AbstractButton {
                    required property string modelData
                    Layout.fillWidth: true
                    implicitHeight: 54
                    onClicked: prefs.readingTheme = modelData
                    contentItem: Rectangle {
                        radius: Math.min(8, theme.radius + 4)
                        color: panel.themeColors[modelData][0]
                        border.width: prefs.readingTheme === modelData ? 2 : 1
                        border.color: prefs.readingTheme === modelData ? theme.accent : "#44444c"
                        Column {
                            anchors.centerIn: parent
                            spacing: 0
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "Aa"
                                font.family: "serif"
                                font.pixelSize: 17
                                color: panel.themeColors[modelData][1]
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: panel.themeNames[modelData]
                                font.pixelSize: 10
                                color: panel.themeColors[modelData][1]
                                opacity: 0.8
                            }
                        }
                    }
                }
            }
        }

        // Typeface
        ColumnLayout {
            visible: !panel.pdf
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: [""].concat(prefs.fonts)
                delegate: ItemDelegate {
                    required property string modelData
                    Layout.fillWidth: true
                    implicitHeight: 38
                    onClicked: prefs.fontFamily = modelData
                    contentItem: RowLayout {
                        Label {
                            Layout.fillWidth: true
                            text: modelData.length ? modelData : qsTr("Publisher’s font")
                            font.family: modelData.length ? modelData : "serif"
                            font.pixelSize: 16
                            elide: Text.ElideRight
                        }
                        Label {
                            visible: prefs.fontFamily === modelData
                            text: "✓"
                            color: theme.accent
                        }
                    }
                }
            }
        }

        // Spacing and margins
        ColumnLayout {
            visible: !panel.pdf
            Layout.fillWidth: true
            spacing: 6
            Label { text: qsTr("Line spacing"); color: "#a0a0a8"; font.pixelSize: 12 }
            Segmented {
                Layout.fillWidth: true
                spacing: 4
                options: [[1.0, qsTr("Tight")], [1.1, qsTr("Normal")], [1.3, qsTr("Loose")]]
                value: prefs.lineSpacing
                onPicked: value => prefs.lineSpacing = value
            }
            Label { text: qsTr("Margins"); color: "#a0a0a8"; font.pixelSize: 12; Layout.topMargin: 6 }
            Segmented {
                Layout.fillWidth: true
                spacing: 4
                options: [[0, qsTr("Narrow")], [1, qsTr("Normal")], [2, qsTr("Wide")]]
                value: prefs.margins
                onPicked: value => prefs.margins = value
            }
        }

        Switch {
            visible: !panel.pdf
            Layout.fillWidth: true
            text: qsTr("Justify text")
            checked: prefs.justify
            onToggled: prefs.justify = checked
        }
        Switch {
            Layout.fillWidth: true
            text: qsTr("Two pages when there’s room")
            checked: prefs.spread
            onToggled: prefs.spread = checked
        }
    }
}
