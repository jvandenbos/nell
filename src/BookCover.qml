import QtQuick

// A book's cover, or one set in type from its title when it has none, with
// a little depth so it reads as an object on a shelf.
Item {
    id: cover
    property string source
    property string title
    property string author
    property bool missing: false

    // A stable color per title, so a book without a cover is always the same one.
    readonly property var coverColors: ["#7a3b2e", "#2e4a7a", "#3b6b4a", "#6b3b6b", "#7a6a2e", "#2e6b6b", "#5a4a3a", "#3a3a5a"]
    readonly property color base: {
        let hash = 0
        for (let i = 0; i < title.length; ++i)
            hash = (hash * 31 + title.charCodeAt(i)) % 1000003
        return coverColors[hash % coverColors.length]
    }
    readonly property int corner: Math.min(4, theme.radius)

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 4
        anchors.leftMargin: 2
        anchors.rightMargin: -2
        anchors.bottomMargin: -5
        radius: cover.corner + 2
        color: "#000000"
        opacity: 0.35
    }

    Rectangle {
        anchors.fill: parent
        radius: cover.corner
        clip: true
        color: image.status === Image.Ready ? "#1c1c20" : cover.base
        border.width: image.status === Image.Ready ? 0 : 1
        border.color: Qt.lighter(cover.base, 1.3)

        Column {
            visible: image.status !== Image.Ready
            anchors.fill: parent
            anchors.margins: parent.width * 0.1
            anchors.topMargin: parent.height * 0.14
            spacing: parent.height * 0.06
            Text {
                width: parent.width
                text: cover.title
                color: "#f5efe0"
                font.family: "serif"
                font.pixelSize: Math.max(10, Math.round(cover.width * 0.11))
                wrapMode: Text.Wrap
                maximumLineCount: 5
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }
            Rectangle {
                width: parent.width * 0.3
                height: 1
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#f5efe0"
                opacity: 0.6
            }
            Text {
                width: parent.width
                text: cover.author
                color: "#f5efe0"
                opacity: 0.85
                font.family: "serif"
                font.italic: true
                font.pixelSize: Math.max(9, Math.round(cover.width * 0.075))
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Image {
            id: image
            anchors.fill: parent
            source: cover.source.length ? "file://" + cover.source : ""
            // Book covers fill the shape; a landscape PDF page fits inside it.
            fillMode: implicitWidth > implicitHeight ? Image.PreserveAspectFit : Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
            mipmap: true
            sourceSize.height: 600
            opacity: cover.missing ? 0.35 : 1
        }

        // The spine's crease, as on a printed cover.
        Rectangle {
            x: parent.width * 0.035
            width: 2
            height: parent.height
            color: "#ffffff"
            opacity: 0.12
        }
    }
}
