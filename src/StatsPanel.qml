import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Shapes

// How the reading is going: today against the daily goal, the streak, the
// week, the year, and the badges. All of it from reading.json on this
// machine; none of it goes anywhere.
Popup {
    id: panel
    property Item returnFocus: null
    readonly property var stats: record.stats
    readonly property var today: stats.today
    readonly property real ringProgress: today.goal > 0 ? Math.min(1, today.seconds / (today.goal * 60)) : 0

    function show(from) {
        returnFocus = from
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent.width - 32)
    height: Math.min(content.implicitHeight + 2 * padding + 8, parent.height - 32)
    padding: 24
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Reading")

    onOpened: flick.forceActiveFocus()
    onClosed: if (returnFocus) returnFocus.forceActiveFocus()

    Overlay.modal: Rectangle { color: Tokens.scrim }
    background: Rectangle {
        radius: theme.radius
        color: Tokens.surfaceRaised
        border.width: 1
        border.color: Tokens.border
    }

    function hours(h) { return h < 10 ? Number(h).toLocaleString(Qt.locale(), 'f', 1) : Math.round(h).toLocaleString(Qt.locale(), 'f', 0) }

    contentItem: Flickable {
        id: flick
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Keys.onEscapePressed: panel.close()

        ColumnLayout {
            id: content
            width: flick.width
            spacing: 20

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Reading")
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    color: Tokens.text
                    Accessible.role: Accessible.Heading
                }
                IconButton { iconName: "close"; tip: qsTr("Close (Esc)"); implicitWidth: 32; implicitHeight: 32; onClicked: panel.close() }
            }

            // Today's ring and the streak beside it.
            RowLayout {
                Layout.fillWidth: true
                spacing: 24
                Item {
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 120
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("%1 of %2 minutes today").arg(panel.today.minutes).arg(panel.today.goal)
                    Shape {
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: Tokens.border
                            strokeWidth: 10
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            PathAngleArc { centerX: 60; centerY: 60; radiusX: 52; radiusY: 52; startAngle: -90; sweepAngle: 360 }
                        }
                        ShapePath {
                            strokeColor: Tokens.accent
                            strokeWidth: 10
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            PathAngleArc {
                                centerX: 60; centerY: 60; radiusX: 52; radiusY: 52
                                startAngle: -90
                                sweepAngle: 360 * panel.ringProgress
                                Behavior on sweepAngle { NumberAnimation { duration: Tokens.duration(400); easing.type: Easing.OutCubic } }
                            }
                        }
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 0
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: panel.today.minutes.toLocaleString(Qt.locale(), 'f', 0)
                            font.pixelSize: 30
                            font.weight: Font.Bold
                            color: Tokens.text
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("of %1 min").arg(panel.today.goal)
                            font.pixelSize: 12
                            color: Tokens.textSecondary
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        text: panel.today.goalMet ? qsTr("Today's goal met") : qsTr("%n min to go today", "", Math.max(0, panel.today.goal - panel.today.minutes))
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: panel.today.goalMet ? Tokens.accentText : Tokens.text
                    }
                    Label {
                        text: panel.stats.streak > 0 ? qsTr("%n-day streak", "", panel.stats.streak) : qsTr("No streak yet; a minute of reading starts one")
                        color: Tokens.text
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: panel.stats.longestStreak > panel.stats.streak
                        text: qsTr("Longest: %n days", "", panel.stats.longestStreak)
                        color: Tokens.textSecondary
                        font.pixelSize: 13
                    }
                    RowLayout {
                        spacing: 8
                        Label { text: qsTr("Daily goal, in minutes"); color: Tokens.textSecondary; font.pixelSize: 13 }
                        SpinBox {
                            from: 5; to: 600; stepSize: 5
                            value: prefs.dailyGoal
                            editable: true
                            onValueModified: prefs.dailyGoal = value
                            Accessible.name: qsTr("Daily goal, in minutes")
                            Material.roundedScale: theme.radius
                            implicitWidth: 132
                        }
                    }
                }
            }

            // The week, today at the right.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { text: qsTr("This week"); font.weight: Font.DemiBold; color: Tokens.text }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: panel.stats.week
                        ColumnLayout {
                            required property var modelData
                            required property int index
                            readonly property real scaleTop: Math.max(panel.today.goal, ...panel.stats.week.map(d => d.minutes))
                            Layout.fillWidth: true
                            spacing: 4
                            Accessible.role: Accessible.StaticText
                            Accessible.name: Qt.formatDate(new Date(modelData.date + "T12:00:00"), "dddd") + ": " + qsTr("%n min", "", modelData.minutes)
                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 72
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 72
                                    radius: 4
                                    color: Tokens.hover
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: Math.max(modelData.minutes > 0 ? 4 : 0, 72 * modelData.minutes / scaleTop)
                                    radius: 4
                                    color: modelData.goalMet ? Tokens.accent : Tokens.textSecondary
                                    opacity: index === 6 ? 1 : 0.75
                                }
                                // The goal line.
                                Rectangle {
                                    width: parent.width
                                    height: 1
                                    y: 72 - 72 * panel.today.goal / scaleTop
                                    color: Tokens.accentText
                                    opacity: 0.6
                                }
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: Qt.formatDate(new Date(modelData.date + "T12:00:00"), "ddd").slice(0, 2)
                                font.pixelSize: 11
                                font.weight: index === 6 ? Font.Bold : Font.Normal
                                color: index === 6 ? Tokens.text : Tokens.textSecondary
                            }
                        }
                    }
                }
            }

            // The longer view.
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 24
                rowSpacing: 4
                Label { text: qsTr("This year"); color: Tokens.textSecondary; font.pixelSize: 13 }
                Label {
                    text: qsTr("%n books finished", "", panel.stats.year.books) + " · " + qsTr("%1 hr").arg(panel.hours(panel.stats.year.hours))
                    color: Tokens.text
                }
                Label { text: qsTr("All time"); color: Tokens.textSecondary; font.pixelSize: 13 }
                Label {
                    text: qsTr("%n books", "", panel.stats.total.books) + " · " + qsTr("%1 hr").arg(panel.hours(panel.stats.total.hours)) + " · " + qsTr("%n days", "", panel.stats.total.days)
                    color: Tokens.text
                }
            }

            // Badges: earned in the accent, the rest showing how far along they are.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { text: qsTr("Badges"); font.weight: Font.DemiBold; color: Tokens.text }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: panel.stats.badges
                        Rectangle {
                            required property var modelData
                            width: badgeRow.implicitWidth + 20
                            height: 30
                            radius: 15
                            color: modelData.earned ? Tokens.accent : "transparent"
                            border.width: 1
                            border.color: modelData.earned ? Tokens.accent : Tokens.border
                            opacity: modelData.earned ? 1 : 0.7
                            Accessible.role: Accessible.StaticText
                            Accessible.name: modelData.name + ": " + modelData.blurb + (modelData.earned ? "" : " (" + Math.round(modelData.progress * 100) + "%)")
                            ToolTip.visible: badgeHover.hovered
                            ToolTip.delay: 400
                            ToolTip.text: modelData.blurb + (modelData.earned ? "" : " · " + Math.round(modelData.progress * 100) + "%")
                            HoverHandler { id: badgeHover }
                            // How far along an unearned badge is: a thread along its foot.
                            Rectangle {
                                visible: !modelData.earned && modelData.progress > 0
                                anchors.left: parent.left
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: 12
                                anchors.bottomMargin: 5
                                width: (parent.width - 24) * modelData.progress
                                height: 2
                                radius: 1
                                color: Tokens.accent
                            }
                            Row {
                                id: badgeRow
                                anchors.centerIn: parent
                                spacing: 6
                                Icon {
                                    name: "badge"
                                    filled: modelData.earned
                                    size: 14
                                    color: modelData.earned ? Tokens.accentForeground : Tokens.textSecondary
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Label {
                                    text: modelData.name
                                    font.pixelSize: 12
                                    color: modelData.earned ? Tokens.accentForeground : Tokens.text
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
