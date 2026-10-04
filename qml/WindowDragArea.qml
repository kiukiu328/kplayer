import QtQuick
import QtQuick.Controls.Basic

MouseArea {
    id: root
    required property Item surface
    signal moveRequested()
    property point pressPosition
    property bool moveStarted: false
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
            || item instanceof Control || item instanceof TextInput
            || item instanceof TextEdit || item instanceof VideoStage
            || item.objectName === "appMenuButton")
            return true
        if (item instanceof ListView
            && item.indexAt(local.x + item.contentX, local.y + item.contentY) >= 0)
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
                >= Qt.styleHints.startDragDistance) {
            moveStarted = true
            moveRequested()
        }
    }
}
