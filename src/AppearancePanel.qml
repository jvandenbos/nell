import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Theme, typeface, size, and layout, as under Apple Books' "Aa". Every change
// shows on the page behind it straight away. Never taller than the window
// leaves room for: past that it scrolls, and the font list scrolls on its own.
Popup {
    id: panel
    property bool pdf: false
    property real maxHeight: 600

    readonly property var themeNames: ({
        original: qsTr("Original"), paper: qsTr("Paper"), calm: qsTr("Calm"),
        quiet: qsTr("Quiet"), night: qsTr("Night"), omarchy: "Omarchy"
    })
    // Each theme's [page, text] colors from the one table in Preferences;
    // Omarchy's follow the desktop, so the binding watches the theme too.
    readonly property var themeColors: {
        theme.background
        theme.foreground
        const colors = {}
        for (const key of prefs.readingThemes)
            colors[key] = prefs.themeColors(key)
        return colors
    }

    width: Math.min(352, parent ? parent.width - 24 : 352)
    height: Math.min(implicitHeight, maxHeight)
    padding: 16
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Appearance")

    onOpened: firstControl.forceActiveFocus(Qt.TabFocusReason)

    background: Rectangle {
        radius: theme.radius
        color: Tokens.surfaceRaised
        border.width: 1
        border.color: Tokens.border
    }

    contentItem: Flickable {
        id: flick
        implicitHeight: column.implicitHeight
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: flick.contentHeight > flick.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

        ColumnLayout {
            id: column
            width: flick.width
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Appearance")
                    font.weight: Font.DemiBold
                    color: Tokens.text
                }
                IconButton { iconName: "close"; tip: qsTr("Close (Esc)"); implicitWidth: 32; implicitHeight: 32; onClicked: panel.close() }
            }

            // Size: in a PDF it's the size fit text zooms the body text to.
            RowLayout {
                visible: !panel.pdf || reader.fitText
                Layout.fillWidth: true
                spacing: 8
                Button {
                    id: smaller
                    Layout.fillWidth: true
                    flat: true
                    text: "A"
                    font.pixelSize: 14
                    font.family: "serif"
                    enabled: prefs.fontSize > 12
                    onClicked: prefs.fontSize -= 1
                    Material.roundedScale: theme.radius
                    Accessible.name: qsTr("Decrease text size")
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Decrease text size (Ctrl+−)")
                }
                Label {
                    text: prefs.fontSize.toLocaleString(Qt.locale(), 'f', 0)
                    color: Tokens.textSecondary
                    Layout.preferredWidth: 32
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("Text size %1").arg(text)
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
                    Accessible.name: qsTr("Increase text size")
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Increase text size (Ctrl++)")
                }
            }

            // Themes
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                rowSpacing: 8
                columnSpacing: 8
                Accessible.role: Accessible.Grouping
                Accessible.name: qsTr("Theme")
                Repeater {
                    id: themes
                    model: prefs.readingThemes
                    delegate: AbstractButton {
                        id: swatch
                        required property string modelData
                        required property int index
                        readonly property bool selected: prefs.readingTheme === modelData
                        Layout.fillWidth: true
                        implicitHeight: 56
                        focusPolicy: Qt.StrongFocus
                        text: panel.themeNames[modelData]
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: qsTr("%1 theme").arg(text)
                        Accessible.checkable: true
                        Accessible.checked: selected
                        onClicked: prefs.readingTheme = modelData
                        contentItem: Rectangle {
                            radius: Math.min(8, theme.radius + 4)
                            color: panel.themeColors[swatch.modelData][0]
                            border.width: swatch.selected ? 2 : 1
                            border.color: swatch.selected ? Tokens.focus : Tokens.border
                            FocusRing { control: swatch; controlRadius: parent.radius }
                            Column {
                                anchors.centerIn: parent
                                spacing: 0
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Aa"
                                    font.family: "serif"
                                    font.pixelSize: 17
                                    color: panel.themeColors[swatch.modelData][1]
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: swatch.text
                                    font.pixelSize: 10
                                    color: panel.themeColors[swatch.modelData][1]
                                }
                            }
                        }
                    }
                }
            }

            // Typeface: a list of its own, five rows tall, however many
            // faces are installed.
            ListView {
                id: fonts
                visible: !panel.pdf
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(count, 5) * 40
                clip: true
                model: [""].concat(prefs.fonts)
                currentIndex: Math.max(0, model.indexOf(prefs.fontFamily))
                keyNavigationEnabled: true
                activeFocusOnTab: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Typeface")
                Keys.onReturnPressed: prefs.fontFamily = model[currentIndex]
                Keys.onSpacePressed: prefs.fontFamily = model[currentIndex]
                delegate: ItemDelegate {
                    id: face
                    required property string modelData
                    required property int index
                    width: ListView.view.width
                    implicitHeight: 40
                    highlighted: ListView.isCurrentItem && fonts.activeFocus
                    focusPolicy: Qt.NoFocus
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: modelData.length ? modelData : qsTr("Publisher’s font")
                    Accessible.checkable: true
                    Accessible.checked: prefs.fontFamily === modelData
                    onClicked: { fonts.currentIndex = index; prefs.fontFamily = modelData }
                    contentItem: RowLayout {
                        Label {
                            Layout.fillWidth: true
                            text: face.modelData.length ? face.modelData : qsTr("Publisher’s font")
                            font.family: face.modelData.length ? face.modelData : "serif"
                            font.pixelSize: 16
                            color: Tokens.text
                            elide: Text.ElideRight
                        }
                        Label {
                            visible: prefs.fontFamily === face.modelData
                            text: "✓"
                            color: Tokens.accentText
                        }
                    }
                }
            }

            // Spacing and margins
            ColumnLayout {
                visible: !panel.pdf
                Layout.fillWidth: true
                spacing: 8
                Label { text: qsTr("Line spacing"); color: Tokens.textSecondary; font.pixelSize: 12 }
                Segmented {
                    Layout.fillWidth: true
                    spacing: 4
                    accessibleName: qsTr("Line spacing")
                    options: [[1.0, qsTr("Tight")], [1.1, qsTr("Normal")], [1.3, qsTr("Loose")]]
                    value: prefs.lineSpacing
                    onPicked: value => prefs.lineSpacing = value
                }
                Label { text: qsTr("Margins"); color: Tokens.textSecondary; font.pixelSize: 12; Layout.topMargin: 8 }
                Segmented {
                    Layout.fillWidth: true
                    spacing: 4
                    accessibleName: qsTr("Margins")
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
            // TODO(fit-text): replace this switch with an Auto / On / Off
            // Segmented bound to reader.fitTextMode ("auto", "on", "off") once
            // the C++ side lands, and add the header/footer-removal
            // preference's switch beside it. Wired after the merge.
            Switch {
                visible: panel.pdf
                Layout.fillWidth: true
                text: qsTr("Fit text to size")
                checked: reader.fitText
                onToggled: reader.fitText = checked
            }
            Switch {
                visible: !reader.fitText
                Layout.fillWidth: true
                text: qsTr("Two pages when there’s room")
                checked: prefs.spread
                onToggled: prefs.spread = checked
            }
            Switch {
                Layout.fillWidth: true
                text: qsTr("Reduce motion")
                checked: prefs.reduceMotion
                onToggled: prefs.reduceMotion = checked
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Turn pages and show controls without sliding or fading")
            }
        }
    }

    // The first thing Tab would reach, for focus on opening.
    readonly property Item firstControl: smaller.visible ? smaller : themes.count ? themes.itemAt(0) : flick
}
