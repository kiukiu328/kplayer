import QtQuick
import QtQuick.Controls.Basic

Slider {
    id: root
    property string tooltipText
    property real backendValue: 0
    property bool previewEnabled: false
    readonly property bool previewVisible: previewEnabled && enabled && (timelineHover.hovered || pressed)
    readonly property real previewX: pressed
        ? leftPadding + handle.width / 2 + visualPosition * (availableWidth - handle.width)
        : Math.max(leftPadding, Math.min(width - rightPadding, timelineHover.point.position.x))
    // valueAt() rounds to stepSize; previews need the exact pointer time.
    readonly property real previewTime: from + (to - from) * (pressed ? position
        : (mirrored ? 1 - hoverPosition : hoverPosition))
    readonly property real hoverPosition: Math.max(0, Math.min(1,
        (previewX - leftPadding - handle.width / 2) / Math.max(1, availableWidth - handle.width)))
    // Synchronize only on backend changes, so release cannot overwrite a pending seek.
    onBackendValueChanged: {
        if (!pressed)
            value = backendValue
    }
    Component.onCompleted: value = backendValue
    padding: 6
    hoverEnabled: true
    wheelEnabled: true
    implicitHeight: Theme.seekHeight
    opacity: enabled ? 1 : 0.45
    ToolTip.text: tooltipText
    ToolTip.visible: !previewVisible && (hovered || pressed || activeFocus)
    ToolTip.delay: pressed ? 0 : 600
    ToolTip.timeout: 5000

    HoverHandler { id: timelineHover; blocking: false }

    background: Rectangle {
        x: root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        width: root.availableWidth
        height: 3
        radius: height / 2
        color: Theme.border
        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: Theme.accent
        }
    }
    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + (root.availableHeight - height) / 2
        implicitWidth: 12
        implicitHeight: 12
        radius: width / 2
        color: root.pressed || root.activeFocus ? Theme.accent : Theme.textPrimary
        border.width: 1
        border.color: Theme.iconMuted
    }
}
