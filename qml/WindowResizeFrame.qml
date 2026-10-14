pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    objectName: "windowResizeFrame"
    property int edgeSize: 6
    property int cornerSize: 16
    signal resizeRequested(int edges)
    visible: enabled
    z: 100
    WindowResizeCursor { frame: root }

    Repeater {
        model: [
            { name: "Left", edges: Qt.LeftEdge, cursor: Qt.SizeHorCursor,
              left: true, top: false, side: true },
            { name: "Right", edges: Qt.RightEdge, cursor: Qt.SizeHorCursor,
              left: false, top: false, side: true },
            { name: "Top", edges: Qt.TopEdge, cursor: Qt.SizeVerCursor,
              left: false, top: true, side: false },
            { name: "Bottom", edges: Qt.BottomEdge, cursor: Qt.SizeVerCursor,
              left: false, top: false, side: false },
            { name: "TopLeft", edges: Qt.TopEdge | Qt.LeftEdge, cursor: Qt.SizeFDiagCursor,
              left: true, top: true, corner: true },
            { name: "TopRight", edges: Qt.TopEdge | Qt.RightEdge, cursor: Qt.SizeBDiagCursor,
              left: false, top: true, corner: true },
            { name: "BottomLeft", edges: Qt.BottomEdge | Qt.LeftEdge, cursor: Qt.SizeBDiagCursor,
              left: true, top: false, corner: true },
            { name: "BottomRight", edges: Qt.BottomEdge | Qt.RightEdge, cursor: Qt.SizeFDiagCursor,
              left: false, top: false, corner: true }
        ]
        MouseArea {
            id: handle
            required property var modelData
            readonly property int resizeEdges: modelData.edges
            readonly property bool corner: modelData.corner === true
            readonly property bool side: modelData.side === true
            objectName: "resize" + modelData.name
            width: corner ? root.cornerSize
                : side ? root.edgeSize : Math.max(0, root.width - 2 * root.cornerSize)
            height: corner ? root.cornerSize
                : side ? Math.max(0, root.height - 2 * root.cornerSize) : root.edgeSize
            x: corner || side ? (modelData.left ? 0 : root.width - width) : root.cornerSize
            y: corner ? (modelData.top ? 0 : root.height - height)
                : side ? root.cornerSize : (modelData.top ? 0 : root.height - height)
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            cursorShape: modelData.cursor
            HoverHandler {
                cursorShape: handle.cursorShape
            }
            onWheel: (wheel) => wheel.accepted = true
            onPressed: root.resizeRequested(resizeEdges)
        }
    }
}
