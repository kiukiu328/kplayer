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
    signal clicked()
    implicitWidth: Theme.toolButtonWidth
    implicitHeight: Theme.transportHeight
    color: hover.hovered ? hoverColor : backgroundColor
    radius: Theme.controlRadius
    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.onPressAction: root.clicked()
    activeFocusOnTab: true
    Keys.onSpacePressed: root.clicked()
    Keys.onReturnPressed: root.clicked()

    HoverHandler { id: hover }
    TapHandler { onTapped: root.clicked() }
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
