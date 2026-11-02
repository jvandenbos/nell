import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The way around when not reading, in words: the shelf's three views, the
// catalogs by name, the reading record, and adding books. Shown beside the
// library and the catalogs once the window is wide enough for it; below
// that, the library's header carries the same things.
Rectangle {
    id: sidebar
    // The row for the place shown: "library:all", "catalogs", "catalog:2",
    // or "" while a book is open.
    property string current: ""
    signal libraryPicked(string status)
    signal catalogsPicked()
    signal catalogPicked(int index)
    signal recordPicked(Item from)
    signal addBooksPicked()
    signal helpPicked(Item from)
    signal escaped()

    implicitWidth: 232
    color: Tokens.surface

    // As keyboard focus, so the ring shows on the row it lands on.
    function focusRows() { rows.forceActiveFocus(Qt.TabFocusReason) }

    readonly property var model: {
        const library = qsTr("Library"), catalogs = qsTr("Catalogs")
        const list = [
            { key: "library:all", section: library, text: qsTr("All Books"), icon: "library", child: false, detail: "" },
            { key: "library:reading", section: library, text: qsTr("Reading"), icon: "bookmark", child: false, detail: "" },
            { key: "library:finished", section: library, text: qsTr("Finished"), icon: "badge", child: false, detail: "" },
            { key: "catalogs", section: catalogs, text: qsTr("All Catalogs"), icon: "cloud", child: false, detail: "" }
        ]
        const saved = catalog.catalogs
        for (let i = 0; i < saved.length; ++i)
            list.push({ key: "catalog:" + i, section: catalogs, text: saved[i].name, icon: "", child: true, detail: "" })
        const streak = record.stats.streak
        list.push({ key: "record", section: qsTr("Reading"), text: qsTr("Reading Record"), icon: "stats", child: false,
                    detail: streak > 0 ? qsTr("%n-day streak", "", streak) : qsTr("Goal and streak") })
        return list
    }

    function activate(row) {
        const key = row.key
        if (key.startsWith("library:"))
            sidebar.libraryPicked(key.substring(8))
        else if (key === "catalogs")
            sidebar.catalogsPicked()
        else if (key.startsWith("catalog:"))
            sidebar.catalogPicked(parseInt(key.substring(8)))
        else if (key === "record")
            sidebar.recordPicked(rows)
    }

    // Arriving somewhere by key or click puts the keyboard's row there too.
    onCurrentChanged: {
        const index = model.findIndex(row => row.key === current)
        if (index >= 0)
            rows.currentIndex = index
    }

    ListView {
        id: rows
        anchors.top: parent.top
        anchors.bottom: foot.top
        width: parent.width
        topMargin: 12
        bottomMargin: 12
        clip: true
        model: sidebar.model
        currentIndex: 0
        keyNavigationEnabled: true
        activeFocusOnTab: true
        boundsBehavior: Flickable.StopAtBounds
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Sidebar")

        section.property: "section"
        section.delegate: Label {
            width: rows.width
            topPadding: 18
            bottomPadding: 6
            leftPadding: 24
            text: section
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
            color: Tokens.textSecondary
        }

        delegate: AbstractButton {
            id: row
            required property var modelData
            required property int index
            readonly property bool here: sidebar.current === modelData.key
            x: 8
            width: rows.width - 16
            height: modelData.detail.length ? 50 : 36
            leftPadding: 12
            rightPadding: 12
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.ListItem
            Accessible.name: modelData.text + (modelData.detail.length ? ", " + modelData.detail : "")
            Accessible.selected: here
            onClicked: sidebar.activate(modelData)
            Keys.onReturnPressed: sidebar.activate(modelData)
            Keys.onEnterPressed: sidebar.activate(modelData)
            Keys.onEscapePressed: sidebar.escaped()
            Keys.onRightPressed: sidebar.escaped()

            // The place shown is marked by its own fill; the focus ring says
            // where the keyboard is, which may be elsewhere.
            background: Rectangle {
                radius: Math.min(8, theme.radius)
                color: row.here ? Tokens.surfaceSunken : row.hovered ? Tokens.surfaceRaised : "transparent"
                FocusRing { control: row; controlRadius: parent.radius }
            }
            contentItem: RowLayout {
                spacing: 10
                Item { visible: row.modelData.child; Layout.preferredWidth: 18; Layout.preferredHeight: 1 }
                Icon {
                    visible: row.modelData.icon.length > 0
                    name: row.modelData.icon.length ? row.modelData.icon : "library"
                    size: 18
                    color: row.here ? Tokens.accentText : Tokens.textSecondary
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.text
                        color: row.here ? Tokens.accentText : Tokens.text
                        font.weight: row.here ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                    Label {
                        visible: row.modelData.detail.length > 0
                        Layout.fillWidth: true
                        text: row.modelData.detail
                        font.pixelSize: 12
                        color: Tokens.textSecondary
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: foot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        spacing: 4

        AccentButton {
            Layout.fillWidth: true
            text: qsTr("Add Books")
            onClicked: sidebar.addBooksPicked()
        }
        AbstractButton {
            id: helpRow
            Layout.fillWidth: true
            implicitHeight: 32
            leftPadding: 12
            rightPadding: 12
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Keyboard Shortcuts")
            onClicked: sidebar.helpPicked(helpRow)
            background: Rectangle {
                radius: Math.min(8, theme.radius)
                color: helpRow.hovered ? Tokens.surfaceRaised : "transparent"
                FocusRing { control: helpRow; controlRadius: parent.radius }
            }
            contentItem: RowLayout {
                Label { Layout.fillWidth: true; text: qsTr("Keyboard Shortcuts"); font.pixelSize: 13; color: Tokens.textSecondary; elide: Text.ElideRight }
                Label { text: "?"; font.pixelSize: 13; color: Tokens.textSecondary }
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Tokens.border
    }
}
