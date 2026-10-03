import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root
    required property string label
    property bool selected: false
    implicitWidth: Math.max(63, tabLabel.implicitWidth + 16)
    implicitHeight: Theme.playlistTabsHeight
    color: selected ? Theme.panelBackground : Theme.tabBackground
    Accessible.role: Accessible.PageTab
    Accessible.name: label
    HoverHandler { id: hover }
    ToolTip.text: label
    ToolTip.visible: hover.hovered
    ToolTip.delay: 600
    ToolTip.timeout: 5000
    Text {
        id: tabLabel
        anchors.fill: parent
        anchors.margins: 8
        text: root.label
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        color: root.selected ? Theme.textPrimary : Theme.textMuted
    }
    Rectangle {
        visible: !root.selected
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.dividerWidth
        color: Theme.border
    }
    Rectangle {
        anchors.right: parent.right
        height: parent.height
        width: Theme.dividerWidth
        color: Theme.border
    }
}
