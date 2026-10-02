pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root
    objectName: "playlistPanel"
    required property PlaylistModel playlistModel
    signal trackRequested(int index)
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
    ListView {
        id: trackList
        objectName: "trackList"
        anchors.top: tabs.bottom
        anchors.bottom: footer.top
        width: parent.width
        clip: true
        model: root.playlistModel
        currentIndex: root.playlistModel.currentIndex
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        Keys.onReturnPressed: root.trackRequested(currentIndex)
        Keys.onEnterPressed: root.trackRequested(currentIndex)
        Keys.onSpacePressed: root.trackRequested(currentIndex)
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Playlist items")
        ScrollBar.vertical: ScrollBar {}

        Connections {
            target: root.playlistModel
            function onCurrentIndexChanged() {
                trackList.currentIndex = root.playlistModel.currentIndex
                trackList.positionViewAtIndex(trackList.currentIndex, ListView.Contain)
            }
        }
        delegate: Rectangle {
            id: track
            objectName: "track" + index
            required property int index
            required property string title
            required property url source
            readonly property bool current: index === root.playlistModel.currentIndex
            width: trackList.width
            height: 42
            color: trackList.currentIndex === index || trackHover.hovered
                ? Theme.chromeHover : Theme.transparent
            Accessible.role: Accessible.ListItem
            Accessible.name: title
            Accessible.onPressAction: root.trackRequested(track.index)
            ToolTip.text: qsTr("%1\nDouble click to play").arg(source.toString())
            ToolTip.visible: trackHover.hovered
            ToolTip.delay: 600
            ToolTip.timeout: 5000
            HoverHandler { id: trackHover }
            PlayerIcon {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                name: "play"
                color: Theme.accent
                visible: track.current
            }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 34
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: track.title
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFontSize
                color: track.current ? Theme.accent : Theme.textPrimary
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    trackList.currentIndex = track.index
                    trackList.forceActiveFocus()
                }
                onDoubleClicked: root.trackRequested(track.index)
            }
        }
        Text {
            anchors.centerIn: parent
            width: parent.width - 32
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Drag files here to add to playlist")
            wrapMode: Text.Wrap
            visible: root.playlistModel.count === 0
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
        }
    }
    DropArea {
        anchors.fill: parent
        onEntered: (drag) => { drag.accepted = drag.hasUrls }
        onDropped: (drop) => {
            if (drop.hasUrls && root.playlistModel.appendUrls(drop.urls) > 0)
                drop.acceptProposedAction()
            else
                drop.accepted = false
        }
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: header.height + tabs.height
            anchors.bottomMargin: footer.height
            visible: parent.containsDrag
            color: Theme.transparent
            border.color: Theme.accent
            border.width: 2
        }
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
                    ToolTip.text: modelData
                    ToolTip.visible: hover.hovered
                    ToolTip.delay: 600
                    ToolTip.timeout: 5000
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
            HoverHandler { id: searchHover }
            ToolTip.text: qsTr("Search playlist")
            ToolTip.visible: searchHover.hovered
            ToolTip.delay: 600
            ToolTip.timeout: 5000
            PlayerIcon {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                width: 25
                height: 25
                color: Theme.iconMuted
            }
        }
    }
}
