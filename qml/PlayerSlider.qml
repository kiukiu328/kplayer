import QtQuick
import QtQuick.Controls.Basic

Slider {
    id: root
    property string tooltipText
    property real backendValue: 0
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
    ToolTip.visible: hovered || pressed || activeFocus
    ToolTip.delay: pressed ? 0 : 600
    ToolTip.timeout: 5000

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
