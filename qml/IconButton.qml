import QtQuick

Rectangle {
    id: root
    required property string iconName
    required property string label
    property color iconColor: Theme.iconPrimary
    property color backgroundColor: Theme.transparent
    property color hoverColor: Theme.chromeHover
    property int iconSize: Theme.iconSize
    property bool rightDivider: false
    implicitWidth: Theme.toolButtonWidth
    implicitHeight: Theme.transportHeight
    color: hover.hovered ? hoverColor : backgroundColor
    radius: Theme.controlRadius
    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.description: qsTr("Visual preview; action is not connected yet.")

    HoverHandler { id: hover }
    PlayerIcon {
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        name: root.iconName
        color: root.iconColor
    }
    Rectangle {
        visible: root.rightDivider
        anchors.right: parent.right
        height: parent.height
        width: Theme.dividerWidth
        color: Theme.border
    }
}
