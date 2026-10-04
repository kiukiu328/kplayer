import QtQuick
import QtQuick.Templates as T

MouseArea {
    id: root
    required property Item surface
    signal moveRequested()
    property point pressPosition
    property bool moveStarted: false
    // Qt exposes styleHints as QObject in QML type metadata.
    readonly property var platformStyleHints: Qt.styleHints
    acceptedButtons: Qt.LeftButton
    preventStealing: true

    // Inspect every visible branch: decorative overlays may sit above a control.
    // Reject the press so the actual control keeps its normal pointer handling.
    function blocksDrag(item: Item, point: point): bool {
        if (item === root || !item.visible)
            return false
        const local = item.mapFromItem(root, point.x, point.y)
        if (!item.contains(local))
            return false
        if (item instanceof IconButton || item instanceof PlaylistTab
            || item instanceof T.Control || item instanceof TextInput
            || item instanceof TextEdit || item instanceof VideoStage
            || item.objectName === "appMenuButton"
            || item.objectName === "playlistResizeHandle")
            return true
        const list = item as ListView
        if (list && list.indexAt(local.x + list.contentX, local.y + list.contentY) >= 0)
            return true
        for (let child of item.children) {
            if (blocksDrag(child, point))
                return true
        }
        return false
    }

    onPressed: (mouse) => {
        mouse.accepted = !blocksDrag(surface, Qt.point(mouse.x, mouse.y))
        pressPosition = Qt.point(mouse.x, mouse.y)
        moveStarted = false
    }
    onPositionChanged: (mouse) => {
        if (pressed && !moveStarted
            && Math.hypot(mouse.x - pressPosition.x, mouse.y - pressPosition.y)
                >= root.platformStyleHints.startDragDistance) {
            moveStarted = true
            moveRequested()
        }
    }
}
