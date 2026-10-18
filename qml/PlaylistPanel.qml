pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root
    objectName: "playlistPanel"
    required property PlaylistModel playlistModel
    required property PlaylistCollection playlistCollection
    required property VideoLibrary videoLibrary
    signal trackRequested(int index)
    signal addRequested()
    signal addFolderRequested()
    signal clearRequested()
    signal fileLocationFailed()
    readonly property bool editingSearch: search.activeFocus
    readonly property bool menuOpen: playlistOptions.visible || sortOptions.visible
        || playlistNameDialog.visible || fileMenu.visible
    property int mappingRevision: 0
    property url contextSource
    readonly property int selectedSourceIndex: {
        mappingRevision
        return filteredTracks.sourceIndex(trackList.currentIndex)
    }

    property var selectedRows: []
    property int selectionAnchor: -1
    property bool editingQueue: false
    property bool draggingTracks: false
    property bool initialSelectionPending: true
    property real dragY: 0
    property int dropBoundary: -1
    property real dropLineY: 0
    readonly property var platformStyleHints: Qt.styleHints

    // Restored models already have their current index before signal handlers
    // are connected. Position once after layout, or when a hidden panel opens.
    Component.onCompleted: Qt.callLater(root.restoreInitialSelection)
    onVisibleChanged: if (visible && initialSelectionPending)
        Qt.callLater(root.restoreInitialSelection)

    function restoreInitialSelection() {
        if (!initialSelectionPending || !visible || trackList.width <= 0 || trackList.height <= 0)
            return
        trackList.forceLayout()
        selectSource(playlistModel.currentIndex)
        initialSelectionPending = false
    }

    PlayerMenu {
        id: fileMenu
        objectName: "playlistFileMenu"
        width: Math.max(Theme.playlistMenuWidth, openLocation.implicitWidth)
        PlayerMenuItem {
            id: openLocation
            objectName: "openFileLocationMenuItem"
            text: qsTr("Open file location")
            enabled: root.videoLibrary.canOpenFileLocation(root.contextSource)
            onTriggered: if (!root.videoLibrary.openFileLocation(root.contextSource))
                root.fileLocationFailed()
        }
    }

    function selectSource(index: int) {
        trackList.currentIndex = filteredTracks.filteredIndex(index)
        selectedRows = index >= 0 && trackList.currentIndex >= 0 ? [index] : []
        selectionAnchor = trackList.currentIndex
        if (trackList.currentIndex >= 0)
            trackList.positionViewAtIndex(trackList.currentIndex, ListView.Contain)
    }

    function selectRow(row: int, modifiers: int) {
        const source = filteredTracks.sourceIndex(row)
        if (source < 0)
            return
        if ((modifiers & Qt.ShiftModifier) && selectionAnchor >= 0) {
            let rows = (modifiers & Qt.ControlModifier) ? selectedRows.slice() : []
            for (let i = Math.min(selectionAnchor, row); i <= Math.max(selectionAnchor, row); ++i) {
                const mapped = filteredTracks.sourceIndex(i)
                if (!rows.includes(mapped))
                    rows.push(mapped)
            }
            selectedRows = rows.sort((a, b) => a - b)
        } else if (modifiers & Qt.ControlModifier) {
            selectedRows = selectedRows.includes(source)
                ? selectedRows.filter(value => value !== source)
                : selectedRows.concat([source]).sort((a, b) => a - b)
            selectionAnchor = row
        } else {
            selectedRows = [source]
            selectionAnchor = row
        }
        trackList.currentIndex = row
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
        if (selectedRows.length === 0)
            return
        const row = trackList.currentIndex
        editingQueue = true
        const rows = selectedRows.slice().sort((a, b) => b - a)
        for (let source of rows)
            playlistModel.removeAt(source)
        editingQueue = false
        selectSource(filteredTracks.sourceIndex(Math.min(row, trackList.count - 1)))
    }

    function moveSelection(destination: int) {
        editingQueue = true
        const moved = playlistModel.moveTracks(selectedRows, destination)
        editingQueue = false
        if (moved.length > 0) {
            selectedRows = moved
            trackList.currentIndex = filteredTracks.filteredIndex(moved[0])
            selectionAnchor = trackList.currentIndex
            trackList.positionViewAtIndex(trackList.currentIndex, ListView.Contain)
        }
    }

    function moveSelected(to: int) {
        if (selectedRows.length === 0)
            return
        const first = selectedRows[0]
        const last = selectedRows[selectedRows.length - 1]
        moveSelection(to <= selectedSourceIndex
            ? Math.max(0, first + to - selectedSourceIndex)
            : Math.min(playlistModel.count, last + 1 + to - selectedSourceIndex))
    }

    function updateDrop(y: real) {
        dragY = y
        const row = trackList.indexAt(10 + trackList.contentX, y + trackList.contentY)
        if (row < 0) {
            dropBoundary = y + trackList.contentY <= 0 ? 0 : playlistModel.count
            dropLineY = dropBoundary === 0 ? 0 : trackList.contentHeight
        } else {
            const item = trackList.itemAtIndex(row)
            const after = y + trackList.contentY > item.y + item.height / 2
            dropBoundary = filteredTracks.sourceIndex(row) + (after ? 1 : 0)
            dropLineY = item.y + (after ? item.height : 0)
        }
    }

    function sortTracks(ascending: bool) {
        editingQueue = true
        const selected = playlistModel.sortByTitle(ascending, selectedSourceIndex)
        editingQueue = false
        selectSource(selected)
    }

    function editPlaylistName(create: bool, saveCurrent: bool) {
        playlistNameDialog.saveCurrent = saveCurrent
        playlistNameDialog.editIndex = create ? -1 : playlistCollection.currentIndex
        if (create) {
            let number = 1
            let name = qsTr("播放清單 %1").arg(number)
            while (!playlistCollection.nameAvailable(name))
                name = qsTr("播放清單 %1").arg(++number)
            playlistName.text = name
        } else {
            playlistName.text = playlistCollection.currentName
        }
        playlistNameDialog.open()
    }

    PlaylistFilterModel {
        id: filteredTracks
        objectName: "filteredPlaylist"
        playlist: root.playlistModel
    }
    Connections {
        target: filteredTracks
        function onDataChanged() { root.mappingRevision++ }
        function onRowsInserted() { root.mappingRevision++ }
        function onRowsRemoved() { root.mappingRevision++ }
        function onRowsMoved() { root.mappingRevision++ }
        function onModelReset() {
            root.mappingRevision++
            if (!root.editingQueue) {
                root.selectedRows = []
                root.selectionAnchor = -1
            }
        }
        function onLayoutChanged() { root.mappingRevision++ }
    }
    Connections {
        target: root.playlistModel
        function onCurrentIndexChanged() { if (!root.editingQueue) root.selectSource(root.playlistModel.currentIndex) }
        function onCurrentSourceChanged() { if (!root.editingQueue) root.selectSource(root.playlistModel.currentIndex) }
    }
    Connections {
        target: root.playlistCollection
        function onCurrentPlaylistChanged() {
            search.clear()
            root.mappingRevision++
            Qt.callLater(() => root.selectSource(root.playlistModel.currentIndex))
        }
    }

    color: Theme.panelBackground
    // Views and controls handle wheels first. Keep any unhandled scrolling
    // (including empty lists and their bounds) off the video underneath.
    MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.NoButton
        onWheel: (wheel) => wheel.accepted = true
    }
    Row {
        id: header
        width: parent.width
        height: Theme.playlistHeaderHeight
        PlaylistTab {
            id: selectedHeader
            width: root.width
            height: header.height
            label: qsTr("播放清單")
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
    }
    Item {
        id: tabs
        anchors.top: header.bottom
        width: parent.width
        height: Theme.playlistTabsHeight
        ListView {
            id: playlistTabs
            objectName: "playlistTabs"
            width: parent.width - tabNavigation.width
            height: parent.height
            clip: true
            orientation: ListView.Horizontal
            boundsBehavior: Flickable.StopAtBounds
            model: root.playlistCollection
            currentIndex: root.playlistCollection.currentIndex
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
            delegate: PlaylistTab {
                id: playlistTab
                required property int index
                required property string name
                objectName: "playlistTab" + index
                label: index === 0 ? qsTr("預設") : name
                selected: index === root.playlistCollection.currentIndex
                width: Math.min(180, implicitWidth)
                height: playlistTabs.height
                activeFocusOnTab: true
                Accessible.onPressAction: root.playlistCollection.currentIndex = index
                Keys.onReturnPressed: root.playlistCollection.currentIndex = index
                Keys.onSpacePressed: root.playlistCollection.currentIndex = index
                TapHandler { onTapped: root.playlistCollection.currentIndex = playlistTab.index }
                TapHandler {
                    acceptedButtons: Qt.MiddleButton
                    onTapped: root.playlistCollection.removePlaylist(playlistTab.index)
                }
            }
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
                    label: qsTr("Select playlist")
                    backgroundColor: Theme.buttonBackground
                    hoverColor: Theme.buttonHover
                    iconColor: Theme.textMuted
                    iconSize: 20
                    onClicked: playlistOptions.open()
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.dividerWidth
                color: Theme.border
            }
        }
        DropArea {
            anchors.fill: parent
            onEntered: (drag) => { drag.accepted = drag.hasUrls }
            onDropped: (drop) => {
                if (drop.hasUrls && root.videoLibrary.addDroppedUrls(root.playlistCollection, drop.urls) > 0)
                    drop.acceptProposedAction()
                else
                    drop.accepted = false
            }
            Rectangle {
                anchors.fill: parent
                visible: parent.containsDrag
                color: Theme.transparent
                border.color: Theme.accent
                border.width: 2
            }
        }
    }
    FontMetrics {
        id: trackFont
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallFontSize
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
        flickableDirection: Flickable.HorizontalAndVerticalFlick
        contentWidth: {
            root.mappingRevision
            const titles = filteredTracks.titles()
            let longest = 0
            for (const title of titles)
                longest = Math.max(longest, trackFont.advanceWidth(title))
            return Math.max(width, 30 + Math.max(24, String(root.playlistModel.count).length * 8)
                + 8 + longest + 14 + playlistScrollBar.width)
        }
        activeFocusOnTab: true
        interactive: !root.draggingTracks
        Keys.onReturnPressed: root.trackRequested(root.selectedSourceIndex)
        Keys.onEnterPressed: root.trackRequested(root.selectedSourceIndex)
        Keys.onSpacePressed: root.trackRequested(root.selectedSourceIndex)
        Keys.onDeletePressed: root.removeSelected()
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                if (!(event.modifiers & Qt.ControlModifier)) {
                    const next = Math.max(0, Math.min(trackList.count - 1,
                        trackList.currentIndex + (event.key === Qt.Key_Up ? -1 : 1)))
                    root.selectRow(next, event.modifiers)
                    trackList.positionViewAtIndex(next, ListView.Contain)
                    event.accepted = true
                    return
                }
            }
            if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                let rows = []
                for (let i = 0; i < trackList.count; ++i)
                    rows.push(filteredTracks.sourceIndex(i))
                root.selectedRows = rows
                event.accepted = true
            }
            if (event.modifiers & Qt.ControlModifier) {
                if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    root.moveSelected(root.selectedSourceIndex + (event.key === Qt.Key_Up ? -1 : 1))
                    event.accepted = true
                }
            }
        }
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Playlist items")
        ScrollBar.vertical: ScrollBar {
            id: playlistScrollBar
            objectName: "playlistVerticalScrollBar"
        }
        ScrollBar.horizontal: ScrollBar {
            id: playlistHorizontalScrollBar
            objectName: "playlistHorizontalScrollBar"
            policy: ScrollBar.AsNeeded
            visible: trackList.contentWidth > trackList.width
        }

        // Keep the pointer grab on the view while scrolling recycles delegates.
        MouseArea {
            parent: trackList
            width: trackList.width - playlistScrollBar.width
            height: trackList.height - (playlistHorizontalScrollBar.visible
                ? playlistHorizontalScrollBar.height : 0)
            z: 1
            preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            property point pressPosition
            property int pressedSource: -1
            property bool collapseOnRelease: false
            onWheel: (wheel) => {
                if (!(wheel.modifiers & Qt.ShiftModifier)) {
                    wheel.accepted = false
                    return
                }
                const pixels = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.pixelDelta.x
                const angle = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
                const delta = pixels !== 0 ? pixels : angle / 120 * 60
                wheel.accepted = true
                if (!root.draggingTracks)
                    trackList.contentX = Math.max(0, Math.min(
                        trackList.contentWidth - trackList.width, trackList.contentX - delta))
            }
            onPressed: (mouse) => {
                const row = trackList.indexAt(mouse.x + trackList.contentX, mouse.y + trackList.contentY)
                pressedSource = filteredTracks.sourceIndex(row)
                if (pressedSource < 0) {
                    mouse.accepted = false
                    return
                }
                if (mouse.button === Qt.RightButton) {
                    collapseOnRelease = false
                    if (!root.selectedRows.includes(pressedSource))
                        root.selectRow(row, Qt.NoModifier)
                    return
                }
                pressPosition = Qt.point(mouse.x, mouse.y)
                collapseOnRelease = !(mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))
                    && root.selectedRows.includes(pressedSource)
                if (!collapseOnRelease)
                    root.selectRow(row, mouse.modifiers)
                trackList.forceActiveFocus()
            }
            onPositionChanged: (mouse) => {
                if (!pressed || pressedButtons !== Qt.LeftButton)
                    return
                if (!root.draggingTracks && root.selectedRows.includes(pressedSource)
                    && Math.hypot(mouse.x - pressPosition.x, mouse.y - pressPosition.y)
                        >= root.platformStyleHints.startDragDistance)
                    root.draggingTracks = true
                if (root.draggingTracks)
                    root.updateDrop(mouse.y)
            }
            onReleased: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    root.contextSource = filteredTracks.sourceAt(filteredTracks.filteredIndex(pressedSource))
                    const point = mapToItem(root, mouse.x, mouse.y)
                    fileMenu.popup(point.x, point.y)
                    return
                }
                const dragged = root.draggingTracks
                root.draggingTracks = false
                if (dragged)
                    root.moveSelection(root.dropBoundary)
                else if (collapseOnRelease)
                    root.selectRow(filteredTracks.filteredIndex(pressedSource), Qt.NoModifier)
                root.dropBoundary = -1
            }
            onCanceled: {
                root.draggingTracks = false
                root.dropBoundary = -1
            }
            onDoubleClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton)
                    root.trackRequested(pressedSource)
            }
        }

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
            width: trackList.contentWidth
            height: 42
            color: root.selectedRows.includes(queueIndex) || trackHover.hovered
                ? Theme.chromeHover : Theme.transparent
            Accessible.role: Accessible.ListItem
            Accessible.name: (queueIndex + 1) + ". " + title
            Accessible.selected: root.selectedRows.includes(queueIndex)
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
                id: trackNumber
                objectName: "trackNumber" + track.index
                anchors.left: parent.left
                anchors.leftMargin: 30
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(24, String(root.playlistModel.count).length * 8)
                text: (track.queueIndex + 1) + "."
                horizontalAlignment: Text.AlignRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFontSize
                color: track.current ? Theme.accent : Theme.textMuted
            }
            Text {
                anchors.left: trackNumber.right
                anchors.leftMargin: 8
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: track.title
                textFormat: Text.PlainText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFontSize
                color: track.current ? Theme.accent : Theme.textPrimary
            }

        }
        Text {
            anchors.centerIn: parent
            width: parent.width - 32
            horizontalAlignment: Text.AlignHCenter
            text: root.playlistModel.count === 0
                ? qsTr("拖曳檔案或資料夾到此處") : qsTr("No matching tracks")
            wrapMode: Text.Wrap
            visible: trackList.count === 0
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
        }
    }
    Timer {
        interval: 30
        repeat: true
        running: root.draggingTracks
        onTriggered: {
            const direction = root.dragY < 30 ? -1 : root.dragY > trackList.height - 30 ? 1 : 0
            if (direction !== 0) {
                trackList.contentY = Math.max(0, Math.min(
                    Math.max(0, trackList.contentHeight - trackList.height),
                    trackList.contentY + direction * 12))
                root.updateDrop(root.dragY)
            }
        }
    }
    Rectangle {
        parent: trackList
        z: 3
        width: trackList.width
        height: 2
        color: Theme.accent
        visible: root.draggingTracks
        y: Math.max(0, Math.min(trackList.height - height, root.dropLineY - trackList.contentY))
    }
    DropArea {
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        onEntered: (drag) => { drag.accepted = drag.hasUrls }
        onDropped: (drop) => {
            if (drop.hasUrls && root.videoLibrary.addDroppedUrlsToPlaylist(root.playlistModel, drop.urls) > 0)
                drop.acceptProposedAction()
            else
                drop.accepted = false
        }
        Rectangle {
            anchors.fill: parent
            anchors.bottomMargin: footer.height
            visible: parent.containsDrag
            color: Theme.transparent
            border.color: Theme.accent
            border.width: 2
        }
    }
    Rectangle {
        id: footer
        objectName: "playlistFooter"
        readonly property bool inlineSearch: width >= 24 + ordering.actionsWidth + 8 + 140
        anchors.bottom: parent.bottom
        width: parent.width
        height: Math.max(Theme.playlistFooterHeight,
            24 + ordering.height + (inlineSearch ? 0 : 5 + search.height))
        color: Theme.footerBackground
        Rectangle {
            width: parent.width
            height: Theme.dividerWidth
            color: Theme.border
        }
        Flow {
            id: ordering
            objectName: "playlistActions"
            readonly property real actionsWidth: moveButtons.width + spacing + actionButtons.width
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.top: parent.top
            anchors.topMargin: 12
            width: footer.inlineSearch ? actionsWidth : parent.width - 24
            spacing: 8
            Row {
                id: moveButtons
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
                        enabled: root.selectedRows.length > 0 && (index < 2
                            ? root.selectedRows[0] > 0
                            : root.selectedRows[root.selectedRows.length - 1] < root.playlistModel.count - 1)
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
            }
            Row {
                id: actionButtons
                spacing: 8
                Repeater {
                    model: [
                        { text: qsTr("新增"), label: qsTr("Add media"), name: "addPlaylistButton" },
                        { text: qsTr("移除"), label: qsTr("Remove selected track"), name: "removePlaylistButton" },
                        { text: qsTr("排序"), label: qsTr("Sort playlist"), name: "sortPlaylistButton" }
                    ]
                    Button {
                        id: textButton
                        required property var modelData
                        required property int index
                        objectName: modelData.name
                        width: Math.max(45, contentItem.implicitWidth + 16)
                        height: Theme.compactButtonHeight
                        leftPadding: 8
                        rightPadding: 8
                        enabled: index === 0 || (index === 1
                            ? root.selectedRows.length > 0 : root.playlistModel.count > 1)
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
        }
        TextField {
            id: search
            objectName: "playlistSearch"
            anchors.left: footer.inlineSearch ? ordering.right : parent.left
            anchors.leftMargin: footer.inlineSearch ? 8 : 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.top: footer.inlineSearch ? ordering.top : ordering.bottom
            anchors.topMargin: footer.inlineSearch ? 0 : 5
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
                    root.selectSource(filteredTracks.sourceIndex(0))
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

    PlayerMenu {
        id: playlistOptions
        objectName: "playlistOptionsMenu"
        parent: root
        width: Theme.playlistMenuWidth
        x: root.width - width - 8
        y: header.height + tabs.height
        onOpened: currentIndex = -1
        Instantiator {
            model: root.playlistCollection
            delegate: PlayerMenuItem {
                required property int index
                required property string name
                objectName: "selectPlaylistMenuItem" + index
                text: index === 0 ? qsTr("預設") : name
                checkable: true
                autoExclusive: true
                radioIndicator: true
                checked: index === root.playlistCollection.currentIndex
                onTriggered: root.playlistCollection.currentIndex = index
            }
            onObjectAdded: (index, object) => playlistOptions.insertItem(index, object)
            onObjectRemoved: (index, object) => playlistOptions.removeItem(object)
        }
        PlayerMenuSeparator {}
        PlayerMenuItem {
            objectName: "addNamedPlaylistMenuItem"
            text: qsTr("新增播放清單")
            Accessible.name: qsTr("新增播放清單")
            onTriggered: root.editPlaylistName(true, false)
        }
        PlayerMenuItem {
            objectName: "addFolderPlaylistMenuItem"
            text: qsTr("新增資料夾…")
            onTriggered: root.addFolderRequested()
        }
        PlayerMenuItem {
            objectName: "saveCurrentPlaylistMenuItem"
            text: qsTr("將目前播放清單另存為…")
            enabled: root.playlistModel.count > 0
            onTriggered: root.editPlaylistName(true, true)
        }
        PlayerMenuItem {
            objectName: "deleteNamedPlaylistMenuItem"
            text: qsTr("刪除專輯")
            enabled: root.playlistCollection.currentIndex > 0
            onTriggered: root.playlistCollection.removePlaylist(root.playlistCollection.currentIndex)
        }
        PlayerMenuItem {
            objectName: "editNamedPlaylistMenuItem"
            text: qsTr("編輯專輯")
            enabled: root.playlistCollection.currentIndex > 0
            onTriggered: root.editPlaylistName(false, false)
        }
    }
    Dialog {
        id: playlistNameDialog
        objectName: "playlistNameDialog"
        property int editIndex: -1
        property bool saveCurrent: false
        readonly property bool validName: root.playlistCollection.nameAvailable(
            playlistName.text, editIndex)
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 320
        modal: true
        title: saveCurrent ? qsTr("將目前播放清單另存為…")
            : editIndex < 0 ? qsTr("新增播放清單") : qsTr("編輯專輯")
        standardButtons: Dialog.Ok | Dialog.Cancel
        palette.window: Theme.menuBackground
        palette.windowText: Theme.menuText
        palette.text: Theme.menuText
        palette.button: Theme.buttonBackground
        palette.buttonText: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.menuFontSize
        onOpened: {
            standardButton(Dialog.Ok).text = Qt.binding(() => qsTr("OK"))
            standardButton(Dialog.Cancel).text = Qt.binding(() => qsTr("Cancel"))
            standardButton(Dialog.Ok).enabled = Qt.binding(() => validName)
            playlistName.forceActiveFocus()
            playlistName.selectAll()
        }
        onAccepted: {
            if (saveCurrent)
                root.playlistCollection.saveCurrentPlaylistAs(playlistName.text)
            else if (editIndex < 0)
                root.playlistCollection.addPlaylist(playlistName.text)
            else
                root.playlistCollection.renamePlaylist(editIndex, playlistName.text)
        }
        contentItem: Column {
            spacing: 8
            Label {
                text: qsTr("名稱")
                color: Theme.menuText
            }
            TextField {
                id: playlistName
                objectName: "playlistNameField"
                width: parent.width
                color: Theme.textPrimary
                selectionColor: Theme.menuRadioAccent
                selectedTextColor: Theme.textPrimary
                Accessible.name: qsTr("播放清單名稱")
                background: Rectangle {
                    color: Theme.searchBackground
                    border.color: playlistName.activeFocus ? Theme.menuRadioAccent : Theme.border
                }
                onAccepted: {
                    if (playlistNameDialog.validName)
                        playlistNameDialog.accept()
                }
            }
            Label {
                visible: !playlistNameDialog.validName
                text: playlistName.text.trim().length === 0
                    ? qsTr("請輸入名稱") : qsTr("此名稱已存在")
                color: Theme.menuText
                font.pixelSize: Theme.menuFontSize
            }
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
