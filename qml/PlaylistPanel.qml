import QtQuick

Rectangle {
    id: root
    color: Theme.panelBackground
    Row {
        id: header
        width: parent.width
        height: Theme.playlistHeaderHeight
        PlaylistTab {
            width: root.width / 3
            height: header.height
            label: "瀏覽器"
            color: Theme.chromeBackground
        }
        PlaylistTab {
            id: selectedHeader
            width: root.width / 3
            height: header.height
            label: "播放清單"
            selected: true
            color: Theme.tabBackground
            PlayerIcon {
                anchors.right: parent.right
                anchors.top: parent.top
                width: 10
                height: 10
                rotation: -45
                name: "play"
                color: Theme.accent
            }
        }
        Rectangle {
            width: root.width - selectedHeader.width * 2
            height: header.height
            color: Theme.chromeBackground
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.dividerWidth
                color: Theme.border
            }
        }
    }
    Item {
        id: tabs
        anchors.top: header.bottom
        width: parent.width
        height: Theme.playlistTabsHeight
        Row {
            width: parent.width - tabNavigation.width
            height: parent.height
            PlaylistTab { label: "預設"; selected: true; width: 63 }
        }
        Rectangle {
            id: tabNavigation
            anchors.right: parent.right
            width: 64
            height: parent.height
            color: Theme.tabBackground
            Row {
                anchors.centerIn: parent
                spacing: 1
                IconButton {
                    width: 27
                    height: Theme.compactButtonHeight
                    iconName: "chevronRight"
                    label: qsTr("More playlists")
                    backgroundColor: Theme.buttonBackground
                    hoverColor: Theme.buttonHover
                    iconColor: Theme.textMuted
                    iconSize: 20
                }
                IconButton {
                    width: 27
                    height: Theme.compactButtonHeight
                    iconName: "chevronDown"
                    label: qsTr("Playlist options")
                    backgroundColor: Theme.buttonBackground
                    hoverColor: Theme.buttonHover
                    iconColor: Theme.textMuted
                    iconSize: 20
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.dividerWidth
                color: Theme.border
            }
        }
    }
    // Empty playlist surface, ready for a media model later.
    Item {
        anchors.top: tabs.bottom
        anchors.bottom: footer.top
        width: parent.width
        clip: true
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Playlist items")
    }
    Rectangle {
        id: footer
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.playlistFooterHeight
        color: Theme.footerBackground
        Rectangle {
            width: parent.width
            height: Theme.dividerWidth
            color: Theme.border
        }
        Row {
            id: ordering
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 2
            spacing: 8
            Repeater {
                model: [
                    { icon: "first", label: qsTr("Move to top") },
                    { icon: "up", label: qsTr("Move up") },
                    { icon: "down", label: qsTr("Move down") },
                    { icon: "last", label: qsTr("Move to bottom") }
                ]
                IconButton {
                    required property var modelData
                    width: Theme.compactButtonSize
                    height: Theme.compactButtonHeight
                    iconName: modelData.icon
                    label: modelData.label
                    iconSize: 20
                    backgroundColor: Theme.buttonBackground
                    hoverColor: Theme.buttonHover
                    iconColor: Theme.textMuted
                }
            }
            Repeater {
                model: ["新增", "移除", "排序"]
                Rectangle {
                    id: textButton
                    required property string modelData
                    width: 45
                    height: Theme.compactButtonHeight
                    radius: Theme.controlRadius
                    color: hover.hovered ? Theme.buttonHover : Theme.buttonBackground
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData
                    HoverHandler { id: hover }
                    Text {
                        anchors.centerIn: parent
                        text: textButton.modelData
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallFontSize
                        color: Theme.textMuted
                    }
                }
            }
        }
        Rectangle {
            anchors.left: ordering.right
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: ordering.verticalCenter
            height: 35
            radius: Theme.controlRadius
            color: Theme.searchBackground
            Accessible.role: Accessible.EditableText
            Accessible.name: qsTr("Search playlist")
            PlayerIcon {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                width: 25
                height: 25
                strokeWidth: 3
                color: Theme.iconMuted
            }
        }
    }
}
