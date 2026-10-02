import QtQuick
import QtQuick.Shapes

Item {
    id: root
    required property string name
    property color color: Theme.iconPrimary
    property real strokeWidth: 1.8
    implicitWidth: Theme.iconSize
    implicitHeight: Theme.iconSize

    // Shared 24 x 24 vector paths stay crisp at any display scale.
    readonly property var filledPaths: ({
        "play": "M6 3 L21 12 L6 21 Z",
        "stop": "M5 5 H19 V19 H5 Z",
        "previous": "M5 5 H8 V19 H5 Z M19 5 V19 L9 12 Z",
        "next": "M16 5 H19 V19 H16 Z M5 5 L15 12 L5 19 Z",
        "eject": "M4 14 L12 5 L20 14 Z M4 17 H20 V20 H4 Z",
        "up": "M5 16 L12 7 L19 16 Z",
        "down": "M5 8 H19 L12 17 Z",
        "first": "M5 4 H19 V6 H5 Z M5 18 L12 9 L19 18 Z",
        "last": "M5 6 H19 L12 15 Z M5 18 H19 V20 H5 Z",
        "pin": "M7 3 H17 L15 8 V12 L19 15 H13 V21 H11 V15 H5 L9 12 V8 Z",
        "list": "M4 2 H20 V22 H4 Z"
    })
    readonly property var linePaths: ({
        "chevronDown": "M6 9 L12 15 L18 9",
        "chevronRight": "M9 5 L16 12 L9 19",
        "minimize": "M4 19 H20",
        "maximize": "M5 5 H19 V19 H5 Z M5 7 H19",
        "fullscreen": "M4 9 V4 H9 M15 4 H20 V9 M20 15 V20 H15 M9 20 H4 V15",
        "close": "M5 5 L19 19 M19 5 L5 19",
        "menu": "M3 5 H21 M3 12 H21 M3 19 H21",
        "volume": "M3 9 H7 L12 5 V19 L7 15 H3 Z M16 8 V16 M20 5 V19",
        "search": "M16 10 A6 6 0 1 1 4 10 A6 6 0 1 1 16 10 M15 15 L21 21",
        "searchList": "M2 3 H18 M2 8 H8 M2 13 H7 M19 13 A5 5 0 1 1 9 13 A5 5 0 1 1 19 13 M18 17 L23 22",
        "settings": "M10 2 H14 L15 6 L18 7 L21 5 L23 9 L20 12 L20 14 L23 17 L21 21 L17 19 L15 20 L14 23 H10 L9 20 L6 19 L3 21 L1 17 L4 14 V11 L1 8 L3 4 L7 6 L9 5 Z M16 12 A4 4 0 1 1 8 12 A4 4 0 1 1 16 12"
    })
    readonly property bool filled: filledPaths[name] !== undefined

    Item {
        width: 24
        height: 24
        anchors.centerIn: parent
        scale: Math.min(root.width, root.height) / 24

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: root.filled ? Theme.transparent : root.color
                strokeWidth: root.filled ? 0 : root.strokeWidth
                fillColor: root.filled ? root.color : Theme.transparent
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg {
                    path: root.filled ? root.filledPaths[root.name] : (root.linePaths[root.name] || "")
                }
            }
        }
        Column {
            visible: root.name === "list"
            x: 7
            y: 7
            spacing: 3
            Repeater {
                model: 3
                Rectangle { width: 10; height: 1.5; color: Theme.chromeBackground }
            }
        }
    }
}
