pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root
    objectName: "playlistPanel"
    required property PlaylistModel playlistModel
    signal trackRequested(int index)
    signal addRequested()
    signal clearRequested()
    readonly property bool editingSearch: search.activeFocus
    readonly property bool menuOpen: playlistOptions.visible || sortOptions.visible
    property int mappingRevision: 0
    readonly property int selectedSourceIndex: {
        mappingRevision
        return filteredTracks.sourceIndex(trackList.currentIndex)
    }

    function selectSource(index: int) {
        trackList.currentIndex = filteredTracks.filteredIndex(index)
        if (trackList.currentIndex >= 0)
            trackList.positionViewAtIndex(trackList.currentIndex, ListView.Contain)
    }

    function focusSearch() {
        playlistOptions.close()
        sortOptions.close()
        search.forceActiveFocus()
        search.selectAll()
    }

    function showCurrentTrack() {
        search.clear()
        selectSource(playlistModel.currentIndex)
        trackList.forceActiveFocus()
    }

    function removeSelected() {
        const row = trackList.currentIndex
        if (playlistModel.removeAt(selectedSourceIndex))
            trackList.currentIndex = Math.min(row, trackList.count - 1)
    }

    function moveSelected(to: int) {
        if (playlistModel.moveTrack(selectedSourceIndex, to))
            selectSource(to)
    }

    function sortTracks(ascending: bool) {
        selectSource(playlistModel.sortByTitle(ascending, selectedSourceIndex))
    }

    PlaylistFilterModel {
        id: filteredTracks
        objectName: "filteredPlaylist"
        playlist: root.playlistModel
    }
    Connections {
        target: filteredTracks
        function onRowsInserted() { root.mappingRevision++ }
        function onRowsRemoved() { root.mappingRevision++ }
        function onRowsMoved() { root.mappingRevision++ }
        function onModelReset() { root.mappingRevision++ }
        function onLayoutChanged() { root.mappingRevision++ }
    }
    Connections {
        target: root.playlistModel
        function onCurrentIndexChanged() { root.selectSource(root.playlistModel.currentIndex) }
        function onCurrentSourceChanged() { root.selectSource(root.playlistModel.currentIndex) }
    }

    color: Theme.panelBackground
    Row {
        id: header
        width: parent.width
        height: Theme.playlistHeaderHeight
        PlaylistTab {
            objectName: "addMediaTab"
            width: root.width / 3
            height: header.height
            label: "瀏覽器"
            color: Theme.chromeBackground
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Add media to playlist")
            Accessible.onPressAction: root.addRequested()
            activeFocusOnTab: true
            Keys.onReturnPressed: root.addRequested()
            Keys.onSpacePressed: root.addRequested()
            TapHandler { onTapped: root.addRequested() }
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
                    objectName: "showCurrentTrackButton"
                    width: 27
                    height: Theme.compactButtonHeight
                    iconName: "chevronRight"
                    label: qsTr("Show current track")
                    backgroundColor: Theme.buttonBackground
                    hoverColor: Theme.buttonHover
                    iconColor: Theme.textMuted
                    iconSize: 20
                    enabled: root.playlistModel.count > 0
                    opacity: enabled ? 1 : 0.4
                    onClicked: root.showCurrentTrack()
                }
                IconButton {
                    objectName: "playlistOptionsButton"
                    width: 27
                    height: Theme.compactButtonHeight
                    iconName: "chevronDown"
                    label: qsTr("Playlist options")
                    backgroundColor: Theme.buttonBackground
                    hoverColor: Theme.buttonHover
                    iconColor: Theme.textMuted
                    iconSize: 20
                    onClicked: playlistOptions.popup()
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
        model: filteredTracks
        currentIndex: filteredTracks.filteredIndex(root.playlistModel.currentIndex)
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        Keys.onReturnPressed: root.trackRequested(root.selectedSourceIndex)
        Keys.onEnterPressed: root.trackRequested(root.selectedSourceIndex)
        Keys.onSpacePressed: root.trackRequested(root.selectedSourceIndex)
        Keys.onDeletePressed: root.removeSelected()
        Keys.onPressed: (event) => {
            if (event.modifiers & Qt.ControlModifier) {
                if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    root.moveSelected(root.selectedSourceIndex + (event.key === Qt.Key_Up ? -1 : 1))
                    event.accepted = true
                }
            }
        }
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Playlist items")
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
            id: track
            objectName: "track" + index
            required property int index
            required property string title
            required property url source
            readonly property int queueIndex: {
                root.mappingRevision
                return filteredTracks.sourceIndex(index)
            }
            readonly property bool current: queueIndex === root.playlistModel.currentIndex
            width: trackList.width
            height: 42
            color: trackList.currentIndex === index || trackHover.hovered
                ? Theme.chromeHover : Theme.transparent
            Accessible.role: Accessible.ListItem
            Accessible.name: title
            Accessible.onPressAction: root.trackRequested(track.queueIndex)
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
                onDoubleClicked: root.trackRequested(track.queueIndex)
            }
        }
        Text {
            anchors.centerIn: parent
            width: parent.width - 32
            horizontalAlignment: Text.AlignHCenter
            text: root.playlistModel.count === 0
                ? qsTr("Drag files here to add to playlist") : qsTr("No matching tracks")
            wrapMode: Text.Wrap
            visible: trackList.count === 0
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
                    { icon: "first", name: "moveTopButton", label: qsTr("Move to top") },
                    { icon: "up", name: "moveUpButton", label: qsTr("Move up") },
                    { icon: "down", name: "moveDownButton", label: qsTr("Move down") },
                    { icon: "last", name: "moveBottomButton", label: qsTr("Move to bottom") }
                ]
                IconButton {
                    required property var modelData
                    required property int index
                    objectName: modelData.name
                    width: Theme.compactButtonSize
                    height: Theme.compactButtonHeight
                    iconName: modelData.icon
                    label: modelData.label
                    iconSize: 20
                    backgroundColor: Theme.buttonBackground
                    hoverColor: Theme.buttonHover
                    iconColor: Theme.textMuted
                    enabled: root.selectedSourceIndex >= 0 && (index < 2
                        ? root.selectedSourceIndex > 0
                        : root.selectedSourceIndex < root.playlistModel.count - 1)
                    opacity: enabled ? 1 : 0.4
                    onClicked: {
                        const selected = root.selectedSourceIndex
                        switch (index) {
                        case 0: root.moveSelected(0); break;
                        case 1: root.moveSelected(selected - 1); break;
                        case 2: root.moveSelected(selected + 1); break;
                        case 3: root.moveSelected(root.playlistModel.count - 1); break;
                        }
                    }
                }
            }
            Repeater {
                model: [
                    { text: "新增", label: qsTr("Add media"), name: "addPlaylistButton" },
                    { text: "移除", label: qsTr("Remove selected track"), name: "removePlaylistButton" },
                    { text: "排序", label: qsTr("Sort playlist"), name: "sortPlaylistButton" }
                ]
                Button {
                    id: textButton
                    required property var modelData
                    required property int index
                    objectName: modelData.name
                    width: 45
                    height: Theme.compactButtonHeight
                    enabled: index === 0 || (index === 1
                        ? root.selectedSourceIndex >= 0 : root.playlistModel.count > 1)
                    opacity: enabled ? 1 : 0.4
                    Accessible.name: modelData.label
                    ToolTip.text: modelData.label
                    ToolTip.visible: hovered || activeFocus
                    ToolTip.delay: 600
                    ToolTip.timeout: 5000
                    background: Rectangle {
                        radius: Theme.controlRadius
                        color: textButton.hovered || textButton.activeFocus
                            ? Theme.buttonHover : Theme.buttonBackground
                    }
                    contentItem: Text {
                        text: textButton.modelData.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallFontSize
                        color: Theme.textPrimary
                    }
                    onClicked: {
                        switch (index) {
                        case 0: root.addRequested(); break;
                        case 1: root.removeSelected(); break;
                        case 2: sortOptions.popup(); break;
                        }
                    }
                }
            }
        }
        TextField {
            id: search
            objectName: "playlistSearch"
            anchors.left: ordering.right
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: ordering.verticalCenter
            height: 35
            leftPadding: 8
            rightPadding: 30
            placeholderText: qsTr("Search")
            color: Theme.textPrimary
            placeholderTextColor: Theme.textMuted
            selectionColor: Theme.accent
            selectedTextColor: Theme.border
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            background: Rectangle {
                radius: Theme.controlRadius
                color: Theme.searchBackground
                border.color: search.activeFocus ? Theme.accent : Theme.transparent
            }
            Accessible.name: qsTr("Search playlist")
            ToolTip.text: qsTr("Search playlist")
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.timeout: 5000
            onTextChanged: {
                filteredTracks.searchText = text
                root.selectSource(root.playlistModel.currentIndex)
                if (trackList.currentIndex < 0 && trackList.count > 0)
                    trackList.currentIndex = 0
            }
            onAccepted: {
                if (root.selectedSourceIndex >= 0)
                    root.trackRequested(root.selectedSourceIndex)
            }
            Keys.onEscapePressed: {
                clear()
                trackList.forceActiveFocus()
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 25
                height: 25
                iconName: search.text.length > 0 ? "close" : "search"
                iconSize: 18
                iconColor: Theme.iconMuted
                label: search.text.length > 0 ? qsTr("Clear search") : qsTr("Search playlist")
                onClicked: {
                    search.clear()
                    root.focusSearch()
                }
            }
        }
    }

    Menu {
        id: playlistOptions
        objectName: "playlistOptionsMenu"
        popupType: Popup.Item
        palette.window: Theme.chromeBackground
        palette.text: Theme.textPrimary
        palette.highlight: Theme.chromeHover
        palette.highlightedText: Theme.accent
        background: Rectangle {
            implicitWidth: 220
            color: Theme.chromeBackground
            border.color: Theme.iconMuted
        }
        MenuItem { text: qsTr("Add media…"); onTriggered: root.addRequested() }
        MenuItem {
            text: qsTr("Show current track")
            enabled: root.playlistModel.count > 0
            onTriggered: root.showCurrentTrack()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Remove selected track")
            enabled: root.selectedSourceIndex >= 0
            onTriggered: root.removeSelected()
        }
        MenuItem {
            text: qsTr("Clear playlist")
            enabled: root.playlistModel.count > 0
            onTriggered: root.clearRequested()
        }
    }
    Menu {
        id: sortOptions
        objectName: "playlistSortMenu"
        popupType: Popup.Item
        palette.window: Theme.chromeBackground
        palette.text: Theme.textPrimary
        palette.highlight: Theme.chromeHover
        palette.highlightedText: Theme.accent
        background: Rectangle {
            implicitWidth: 220
            color: Theme.chromeBackground
            border.color: Theme.iconMuted
        }
        MenuItem { text: qsTr("Title: A–Z"); onTriggered: root.sortTracks(true) }
        MenuItem { text: qsTr("Title: Z–A"); onTriggered: root.sortTracks(false) }
    }
}
