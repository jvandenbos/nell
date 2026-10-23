import QtQuick
import QtQuick.Shapes

// A line icon on a 24-unit grid, drawn as a shape so it takes any color and
// stays sharp at any size. Paths in the style of Lucide (ISC license).
Item {
    id: icon
    property string name
    property color color: "white"
    property bool filled: false
    property real size: 20
    implicitWidth: size
    implicitHeight: size

    readonly property var paths: ({
        "back": "M15 18 L9 12 L15 6",
        "forward": "M9 18 L15 12 L9 6",
        "contents": "M8 6 H21 M8 12 H21 M8 18 H21 M3.5 6 H4 M3.5 12 H4 M3.5 18 H4",
        "search": "M17.5 11 A6.5 6.5 0 1 1 4.5 11 A6.5 6.5 0 1 1 17.5 11 Z M20.5 20.5 L15.8 15.8",
        "bookmark": "M18 21 L12 16.5 L6 21 V5 A2 2 0 0 1 8 3 H16 A2 2 0 0 1 18 5 Z",
        "plus": "M12 5 V19 M5 12 H19",
        "close": "M18 6 L6 18 M6 6 L18 18",
        "note": "M21 15 A2 2 0 0 1 19 17 H7 L3 21 V5 A2 2 0 0 1 5 3 H19 A2 2 0 0 1 21 5 Z",
        "copy": "M9 9 H20 V20 H9 Z M5 15 H4 V4 H15 V5",
        "library": "M4 19.5 A2.5 2.5 0 0 1 6.5 17 H20 M6.5 2 H20 V22 H6.5 A2.5 2.5 0 0 1 4 19.5 V4.5 A2.5 2.5 0 0 1 6.5 2 Z",
        "trash": "M3 6 H21 M8 6 V4 H16 V6 M19 6 L18 20 H6 L5 6 M10 11 V16 M14 11 V16",
        "export": "M12 3 V15 M7 8 L12 3 L17 8 M5 14 V20 H19 V14",
        "define": "M2 4 H8 A4 4 0 0 1 12 8 V21 A3 3 0 0 0 9 18 H2 Z M22 4 H16 A4 4 0 0 0 12 8 V21 A3 3 0 0 1 15 18 H22 Z",
        "highlighter": "M9 11 L3 17 V20 H9 L15 14 M22 6 L18 2 L10 10 L14 14 Z",
        "undo": "M9 14 L4 9 L9 4 M4 9 H15 A5 5 0 0 1 15 19 H11"
    })

    Shape {
        width: 24
        height: 24
        scale: icon.size / 24
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: icon.color
            strokeWidth: 1.8
            fillColor: icon.filled ? icon.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.paths[icon.name] || "" }
        }
    }
}
