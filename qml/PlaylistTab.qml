import QtQuick

Rectangle {
    id: root
    required property string label
    property bool selected: false
    implicitHeight: Theme.playlistTabsHeight
    color: selected ? Theme.panelBackground : Theme.tabBackground
    Accessible.role: Accessible.PageTab
    Accessible.name: label
    Text {
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
