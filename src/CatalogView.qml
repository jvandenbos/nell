import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Catalogs on the network, browsed like shelves: the saved catalogs first,
// then each one's feeds. A book got here lands in the library.
Item {
    id: view
    signal closeRequested()
    signal openRequested(string id)

    readonly property bool browsing: catalog.current >= 0
    readonly property bool narrow: width < 800
    readonly property real contentLeft: Math.max(24, (width - 920) / 2)
    readonly property real contentWidth: width - 2 * contentLeft

    function focusList() { (view.browsing ? entries : shelves).forceActiveFocus() }
    // The list that's shown is the one with the keyboard.
    onBrowsingChanged: if (visible) focusList()
    function goBack() {
        if (catalog.canGoBack)
            catalog.back()
        else if (view.browsing)
            catalog.leave()
        else
            view.closeRequested()
    }

    Rectangle { anchors.fill: parent; color: Tokens.surface }

    Shortcut { context: Qt.ApplicationShortcut; sequence: "Escape"; enabled: view.visible && !win.textEntry && !win.helpVisible && !addDialog.visible; onActivated: view.goBack() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "Backspace"; enabled: view.visible && !win.textEntry && !win.helpVisible; onActivated: view.goBack() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: "/"; enabled: view.visible && !win.textEntry && catalog.searchable; onActivated: search.forceActiveFocus() }
    Shortcut { context: Qt.ApplicationShortcut; sequence: StandardKey.Find; enabled: view.visible && catalog.searchable; onActivated: search.forceActiveFocus() }

    ColumnLayout {
        id: header
        x: view.contentLeft
        y: 24
        width: view.contentWidth
        spacing: 16
        z: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            IconButton {
                iconName: "back"
                tip: catalog.canGoBack ? qsTr("Back (Esc)") : view.browsing ? qsTr("Catalogs (Esc)") : qsTr("Library (Esc)")
                onClicked: view.goBack()
            }
            Label {
                Layout.fillWidth: true
                text: view.browsing ? catalog.title : qsTr("Catalogs")
                font.pixelSize: 30
                font.weight: Font.Bold
                color: Tokens.text
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }
            BusyIndicator { visible: catalog.busy; running: visible; implicitWidth: 28; implicitHeight: 28 }
            TextField {
                id: search
                visible: view.browsing && catalog.searchable && !view.narrow
                Layout.preferredWidth: 240
                placeholderText: qsTr("Search the catalog")
                Material.roundedScale: theme.radius
                Accessible.name: qsTr("Search the catalog")
                onAccepted: { catalog.search(text); entries.forceActiveFocus() }
                Keys.onEscapePressed: { text = ""; entries.forceActiveFocus() }
            }
            AccentButton {
                visible: !view.browsing
                text: qsTr("Add Catalog")
                onClicked: addDialog.ask()
            }
        }
        TextField {
            visible: view.browsing && catalog.searchable && view.narrow
            Layout.fillWidth: true
            placeholderText: qsTr("Search the catalog")
            Material.roundedScale: theme.radius
            Accessible.name: qsTr("Search the catalog")
            onAccepted: { catalog.search(text); entries.forceActiveFocus() }
        }
        Label {
            visible: catalog.error.length > 0
            Layout.fillWidth: true
            text: catalog.error
            color: Tokens.danger
            wrapMode: Text.Wrap
        }
    }

    // ---- The saved catalogs ------------------------------------------------
    ListView {
        id: shelves
        visible: !view.browsing
        objectName: "shelf"
        x: view.contentLeft
        y: header.y + header.height + 24
        width: view.contentWidth
        height: view.height - y - 24
        clip: true
        spacing: 8
        model: catalog.catalogs
        currentIndex: 0
        keyNavigationEnabled: true
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Catalogs")

        header: Column {
            width: shelves.width
            spacing: 12
            bottomPadding: 16
            Label {
                width: parent.width
                visible: catalog.catalogs.length === 0
                wrapMode: Text.Wrap
                color: Tokens.textSecondary
                text: qsTr("No catalogs yet. A catalog is a server that offers books as an OPDS feed: Calibre's content server (http://your-computer:8080/opds), Kavita, Standard Ebooks, Project Gutenberg, and others.")
            }
            RowLayout {
                width: parent.width
                spacing: 8
                Label { text: qsTr("Save books to"); color: Tokens.textSecondary; font.pixelSize: 13 }
                TextField {
                    id: downloads
                    Layout.fillWidth: true
                    text: prefs.downloadDirectory
                    Material.roundedScale: theme.radius
                    Accessible.name: qsTr("Save books to")
                    onEditingFinished: if (text.trim().length) prefs.downloadDirectory = text.trim()
                    Keys.onEscapePressed: { text = prefs.downloadDirectory; shelves.forceActiveFocus() }
                }
            }
        }

        delegate: ItemDelegate {
            id: shelf
            required property var modelData
            required property int index
            width: shelves.width
            height: 64
            highlighted: ListView.isCurrentItem
            Material.roundedScale: theme.radius
            onClicked: catalog.open(index)
            Accessible.name: modelData.name + ", " + modelData.url
            background: Rectangle {
                radius: theme.radius
                color: shelf.hovered || shelf.highlighted ? Tokens.hover : Tokens.surfaceRaised
                border.width: 1
                border.color: shelf.highlighted && shelves.activeFocus ? Tokens.accent : Tokens.border
            }
            contentItem: RowLayout {
                spacing: 12
                Icon { name: "cloud"; color: Tokens.accentText; size: 22 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label { text: shelf.modelData.name; font.weight: Font.DemiBold; color: Tokens.text; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { text: shelf.modelData.url; color: Tokens.textSecondary; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.fillWidth: true }
                }
                IconButton { iconName: "trash"; tip: qsTr("Remove this catalog"); onClicked: removeDialog.ask(shelf.index) }
            }
        }
    }

    // ---- A feed --------------------------------------------------------------
    ListView {
        id: entries
        visible: view.browsing
        objectName: "shelf"
        x: view.contentLeft
        y: header.y + header.height + 24
        width: view.contentWidth
        height: view.height - y - 24
        clip: true
        spacing: 8
        model: catalog.entries
        currentIndex: 0
        keyNavigationEnabled: true
        Accessible.role: Accessible.List
        Accessible.name: catalog.title
        onAtYEndChanged: if (atYEnd && catalog.hasMore && !catalog.busy) catalog.more()
        onModelChanged: if (!catalog.busy) currentIndex = 0

        Label {
            anchors.centerIn: parent
            visible: !catalog.busy && catalog.entries.length === 0 && catalog.error.length === 0
            text: qsTr("Nothing here")
            color: Tokens.textSecondary
        }

        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index
            readonly property bool book: modelData.kind === "book"
            readonly property string getUrl: modelData.epub.length ? modelData.epub : modelData.pdf
            readonly property var progress: catalog.downloads[getUrl]
            readonly property bool downloading: progress !== undefined
            // The row has the keyboard when it's current, so the keys are
            // answered here: Enter gets a book or follows a feed.
            function act() { if (book) catalog.get(index); else catalog.browse(modelData.url) }
            Keys.onReturnPressed: act()
            Keys.onEnterPressed: act()
            Keys.onSpacePressed: act()
            width: entries.width
            height: book ? Math.max(104, body.implicitHeight + 24) : 52
            highlighted: ListView.isCurrentItem
            Material.roundedScale: theme.radius
            onClicked: { entries.currentIndex = index; if (!book) catalog.browse(modelData.url) }
            Accessible.name: modelData.title + (modelData.author.length ? ", " + modelData.author : "")
            background: Rectangle {
                radius: theme.radius
                color: row.hovered || row.highlighted ? Tokens.hover : Tokens.surfaceRaised
                border.width: 1
                border.color: row.highlighted && entries.activeFocus ? Tokens.accent : Tokens.border
            }
            contentItem: RowLayout {
                spacing: 14
                // The cover, once it's here.
                Rectangle {
                    visible: row.book
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 80
                    Layout.alignment: Qt.AlignTop
                    radius: 3
                    color: Tokens.surfaceSunken
                    clip: true
                    Image {
                        id: cover
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        source: {
                            if (!row.book || !row.modelData.thumbnail.length)
                                return ""
                            const path = catalog.cover(row.modelData.thumbnail)
                            return path.length ? "file://" + path : ""
                        }
                        Connections {
                            target: catalog
                            function onCoverReady(url, path) { if (url === row.modelData.thumbnail) cover.source = "file://" + path }
                        }
                    }
                }
                Icon { visible: !row.book; name: "contents"; color: Tokens.accentText; size: 20 }
                ColumnLayout {
                    id: body
                    Layout.fillWidth: true
                    spacing: 2
                    Label { text: row.modelData.title; font.weight: Font.DemiBold; color: Tokens.text; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { visible: text.length > 0; text: row.modelData.author; color: Tokens.textSecondary; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label {
                        visible: text.length > 0
                        text: row.modelData.summary
                        color: Tokens.textSecondary
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        maximumLineCount: row.book ? 2 : 1
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                Icon { visible: !row.book; name: "forward"; color: Tokens.textSecondary; size: 18 }
                ColumnLayout {
                    visible: row.book
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 4
                    AccentButton {
                        visible: !row.downloading
                        text: row.modelData.epub.length ? qsTr("Get EPUB") : qsTr("Get PDF")
                        onClicked: catalog.get(row.index)
                    }
                    ProgressBar {
                        visible: row.downloading
                        implicitWidth: 110
                        value: row.downloading ? row.progress : 0
                        indeterminate: row.downloading && row.progress === 0
                        Accessible.name: qsTr("Downloading")
                    }
                }
            }
        }

        footer: Item {
            width: entries.width
            height: catalog.hasMore ? 56 : 8
            Button {
                anchors.centerIn: parent
                visible: catalog.hasMore
                flat: true
                text: qsTr("More")
                enabled: !catalog.busy
                onClicked: catalog.more()
            }
        }
    }

    // ---- Adding one ------------------------------------------------------------
    Dialog {
        id: addDialog
        function ask() { name.text = ""; url.text = ""; user.text = ""; password.text = ""; open(); url.forceActiveFocus() }
        anchors.centerIn: parent
        modal: true
        Material.roundedScale: theme.radius
        title: qsTr("Add a catalog")
        standardButtons: Dialog.Cancel | Dialog.Ok
        ColumnLayout {
            width: Math.min(420, view.width - 96)
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Tokens.textSecondary
                font.pixelSize: 13
                text: qsTr("The address of an OPDS feed. Calibre's content server serves one at http://your-computer:8080/opds; its login, if it has one, goes below.")
            }
            TextField { id: url; Layout.fillWidth: true; placeholderText: qsTr("Address"); Material.roundedScale: theme.radius; Accessible.name: qsTr("Address"); onAccepted: addDialog.accept() }
            TextField { id: name; Layout.fillWidth: true; placeholderText: qsTr("Name (optional)"); Material.roundedScale: theme.radius; Accessible.name: qsTr("Name") }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField { id: user; Layout.fillWidth: true; placeholderText: qsTr("Username"); Material.roundedScale: theme.radius; Accessible.name: qsTr("Username") }
                TextField { id: password; Layout.fillWidth: true; placeholderText: qsTr("Password"); echoMode: TextInput.Password; Material.roundedScale: theme.radius; Accessible.name: qsTr("Password"); onAccepted: addDialog.accept() }
            }
        }
        Component.onCompleted: standardButton(Dialog.Ok).text = qsTr("Add")
        onAccepted: {
            if (!catalog.add(name.text, url.text, user.text, password.text))
                return
            entries.forceActiveFocus()
        }
        onRejected: shelves.forceActiveFocus()
    }

    Dialog {
        id: removeDialog
        property int index: -1
        function ask(i) { index = i; open() }
        anchors.centerIn: parent
        modal: true
        Material.roundedScale: theme.radius
        title: qsTr("Remove “%1”?").arg(index >= 0 && index < catalog.catalogs.length ? catalog.catalogs[index].name : "")
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            width: Math.min(380, view.width - 96)
            wrapMode: Text.Wrap
            text: qsTr("Books already got from it stay in the library.")
            color: Tokens.text
        }
        Component.onCompleted: standardButton(Dialog.Ok).text = qsTr("Remove")
        onAccepted: { catalog.remove(index); shelves.forceActiveFocus() }
        onRejected: shelves.forceActiveFocus()
    }
}
