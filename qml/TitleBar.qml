pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: root
    property bool maximized: false
    property bool fullscreen: false
    signal minimizeRequested()
    signal maximizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
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
                { icon: root.maximized ? "restore" : "maximize",
                  label: root.maximized ? qsTr("Restore") : qsTr("Maximize") },
                { icon: root.fullscreen ? "exitFullscreen" : "fullscreen",
                  label: root.fullscreen ? qsTr("Exit full screen") : qsTr("Full screen") },
                { icon: "close", label: qsTr("Close") }
            ]
            IconButton {
                required property var modelData
                required property int index
                width: Theme.windowButtonWidth
                height: windowControls.height
                iconName: modelData.icon
                label: modelData.label
                iconColor: Theme.iconMuted
                iconSize: 20
                onClicked: {
                    switch (index) {
                    case 1: root.minimizeRequested(); break;
                    case 2: root.maximizeRequested(); break;
                    case 3: root.fullscreenRequested(); break;
                    case 4: root.closeRequested(); break;
                    }
                }
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
