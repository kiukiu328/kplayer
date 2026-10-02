pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: root
    property bool playing: false
    signal playPauseRequested()
    signal stopRequested()
    signal openRequested()
    height: Theme.seekHeight + Theme.transportHeight
    color: Theme.chromeBackground
    Rectangle {
        width: parent.width
        height: Theme.dividerWidth
        color: Theme.border
    }
    Item {
        id: seekBar
        width: parent.width
        height: Theme.seekHeight
        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: volume.left
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            height: 3
            color: Theme.border
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("Playback position")
        }
        Item {
            id: volume
            anchors.right: parent.right
            anchors.rightMargin: 16
            height: parent.height
            width: 132
            PlayerIcon {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: 20
                name: "volume"
                color: Theme.iconMuted
            }
            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: 28
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: 2
                color: Theme.accent
                Accessible.role: Accessible.Slider
                Accessible.name: qsTr("Volume")
                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 12
                    height: 12
                    radius: 6
                    color: Theme.textPrimary
                    border.width: 1
                    border.color: Theme.iconMuted
                }
            }
        }
    }
    Rectangle {
        anchors.top: seekBar.bottom
        width: parent.width
        height: Theme.dividerWidth
        color: Theme.border
    }
    Row {
        id: transport
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        height: Theme.transportHeight - Theme.dividerWidth
        Repeater {
            model: [
                { icon: root.playing ? "pause" : "play",
                  label: root.playing ? qsTr("Pause") : qsTr("Play") },
                { icon: "stop", label: qsTr("Stop") },
                { icon: "previous", label: qsTr("Previous track") },
                { icon: "next", label: qsTr("Next track") },
                { icon: "eject", label: qsTr("Open media") }
            ]
            IconButton {
                required property var modelData
                required property int index
                width: Math.min(Theme.transportButtonWidth, root.width / 12)
                height: transport.height
                iconName: modelData.icon
                label: modelData.label
                iconSize: 20
                rightDivider: true
                onClicked: {
                    switch (index) {
                    case 0: root.playPauseRequested(); break;
                    case 1: root.stopRequested(); break;
                    case 4: root.openRequested(); break;
                    }
                }
            }
        }
    }
    Row {
        anchors.left: transport.right
        anchors.leftMargin: 15
        anchors.verticalCenter: transport.verticalCenter
        spacing: 8
        Text {
            text: "00:00:00"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            color: Theme.textPrimary
        }
        Text {
            text: "/"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            color: Theme.textMuted
        }
        Text {
            text: "00:00:00"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            color: Theme.textMuted
        }
    }
    Row {
        id: tools
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: transport.height
        IconButton {
            height: tools.height
            width: Math.min(Theme.toolButtonWidth, root.width * 0.0645)
            iconName: "searchList"
            label: qsTr("Search media")
            iconColor: Theme.accent
            iconSize: 26
        }
        IconButton {
            height: tools.height
            width: Math.min(Theme.toolButtonWidth, root.width * 0.0645)
            iconName: "list"
            label: qsTr("Playlist")
            iconColor: Theme.accent
            iconSize: 22
        }
        IconButton {
            height: tools.height
            width: Math.min(56, root.width * 0.0753)
            iconName: "settings"
            label: qsTr("Settings")
            iconColor: Theme.iconMuted
            rightDivider: true
        }
        IconButton {
            height: tools.height
            width: Math.min(Theme.transportButtonWidth, root.width / 12)
            iconName: "menu"
            label: qsTr("Menu")
            iconSize: 24
        }
    }
}
