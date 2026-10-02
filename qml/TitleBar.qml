pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    height: Theme.titleHeight
    color: Theme.chromeBackground

    Rectangle {
        width: Theme.titleMenuWidth
        height: parent.height
        color: Theme.transparent
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: "KPlayer"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            color: Theme.textPrimary
        }
        PlayerIcon {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            name: "chevronDown"
            color: Theme.iconMuted
            strokeWidth: 1
        }
        Rectangle {
            anchors.right: parent.right
            height: parent.height
            width: Theme.dividerWidth
            color: Theme.border
        }
    }

    Row {
        id: windowControls
        anchors.right: parent.right
        anchors.rightMargin: 8
        height: parent.height
        Repeater {
            model: [
                { icon: "pin", label: qsTr("Always on top") },
                { icon: "minimize", label: qsTr("Minimize") },
                { icon: "maximize", label: qsTr("Maximize") },
                { icon: "fullscreen", label: qsTr("Full screen") },
                { icon: "close", label: qsTr("Close") }
            ]
            IconButton {
                required property var modelData
                width: Theme.windowButtonWidth
                height: windowControls.height
                iconName: modelData.icon
                label: modelData.label
                iconColor: Theme.iconMuted
                iconSize: 20
            }
        }
    }
    Rectangle {
        anchors.right: windowControls.left
        anchors.rightMargin: 7
        height: parent.height
        width: Theme.dividerWidth
        color: Theme.border
    }
}
