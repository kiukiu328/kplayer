import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls.Basic

Window {
    id: window
    width: 1225
    height: 824
    minimumWidth: 1100
    minimumHeight: 500
    visible: true
    title: "KPlayer"
    color: Theme.border
    flags: Qt.Window | Qt.FramelessWindowHint
        | (alwaysOnTop ? Qt.WindowStaysOnTopHint : 0)
    property url mediaSource
    property int visibilityBeforeFullscreen: Window.Windowed
    property bool playlistVisible: true
    property bool alwaysOnTop: false
    // Manual track navigation always respects the queue boundaries.
    property int repeatMode: 0 // 0: off, 1: current track, 2: entire playlist
    readonly property bool playbackShortcutsEnabled: !playlist.editingSearch
        && !playlist.menuOpen
        && !mediaDialog.visible && !appMenu.visible && !settingsMenu.visible
        && !hasInteractiveFocus()

    function hasInteractiveFocus(): bool {
        const item = window.activeFocusItem
        return item !== null && (item instanceof TextInput || item instanceof TextEdit
            || item instanceof Slider || item instanceof AbstractButton
            || item instanceof IconButton || item.objectName === "trackList"
            || item.objectName === "appMenuButton" || item.objectName === "addMediaTab")
    }

    function openMedia(playAdded: bool) {
        mediaDialog.playAdded = playAdded
        mediaDialog.open()
    }

    function focusPlaylistSearch() {
        appMenu.close()
        settingsMenu.close()
        playlistVisible = true
        Qt.callLater(() => playlist.focusSearch())
    }

    function clearPlaylist() {
        videoStage.stop()
        mediaPlaylist.clear()
    }

    function showAppMenu(fromBottom: bool) {
        settingsMenu.close()
        if (fromBottom) {
            const point = playback.mapToItem(window.contentItem,
                playback.width - appMenu.width - 8, 0)
            appMenu.x = Math.max(8, point.x)
            appMenu.y = Math.max(8, point.y - appMenu.height - 8)
        } else {
            appMenu.x = Theme.windowBorder + 8
            appMenu.y = Theme.windowBorder + titleBar.height
        }
        appMenu.open()
    }

    function showSettingsMenu() {
        appMenu.close()
        const point = playback.mapToItem(window.contentItem,
            playback.width - settingsMenu.width - 8, 0)
        settingsMenu.x = Math.max(8, point.x)
        settingsMenu.y = Math.max(8, point.y - settingsMenu.height - 8)
        settingsMenu.open()
    }

    function toggleFullscreen() {
        if (visibility === Window.FullScreen) {
            if (visibilityBeforeFullscreen === Window.Maximized)
                showMaximized()
            else
                showNormal()
        } else {
            visibilityBeforeFullscreen = visibility
            showFullScreen()
        }
    }

    function togglePlayback() {
        if (mediaPlaylist.count === 0)
            openMedia(true)
        else
            videoStage.togglePause()
    }

    function playTrack(index: int) {
        if (index < 0 || index >= mediaPlaylist.count)
            return
        videoStage.stop()
        mediaPlaylist.currentIndex = index
        videoStage.play()
    }

    function skipTrack(offset: int) {
        playTrack(mediaPlaylist.currentIndex + offset)
    }

    function advanceAfterEnd() {
        if (mediaPlaylist.count === 0)
            return
        if (repeatMode === 1)
            playTrack(mediaPlaylist.currentIndex)
        else if (mediaPlaylist.currentIndex + 1 < mediaPlaylist.count)
            skipTrack(1)
        else if (repeatMode === 2)
            playTrack(0)
    }

    PlaylistModel { id: mediaPlaylist; objectName: "playlistModel" }
    Component.onCompleted: {
        if (mediaSource.toString().length > 0)
            mediaPlaylist.appendUrls([mediaSource])
    }

    FileDialog {
        id: mediaDialog
        objectName: "mediaDialog"
        property bool playAdded: true
        title: playAdded ? qsTr("Open media") : qsTr("Add media to playlist")
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            const firstIndex = mediaPlaylist.count
            if (mediaPlaylist.appendUrls(selectedFiles) > 0 && playAdded)
                window.playTrack(firstIndex)
        }
    }

    component PlayerMenu: Menu {
        popupType: Popup.Item
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallFontSize
        palette.text: Theme.textPrimary
        palette.window: Theme.chromeBackground
        palette.base: Theme.panelBackground
        palette.highlight: Theme.chromeHover
        palette.highlightedText: Theme.textPrimary
        palette.mid: Theme.border
        background: Rectangle {
            implicitWidth: 280
            color: Theme.chromeBackground
            border.color: Theme.border
            border.width: 1
            radius: Theme.controlRadius
        }
    }
    component PlayerMenuItem: MenuItem {
        id: menuItem
        implicitHeight: 34
        background: Rectangle {
            color: menuItem.highlighted ? Theme.chromeHover : Theme.transparent
        }
    }
    component PlayerMenuSeparator: MenuSeparator {
        contentItem: Rectangle {
            implicitWidth: 220
            implicitHeight: 1
            color: Theme.border
        }
    }

    Action {
        id: openMediaAction
        text: qsTr("Open media…")
        shortcut: "Ctrl+O"
        onTriggered: window.openMedia(true)
    }
    Action {
        id: searchPlaylistAction
        text: qsTr("Search playlist")
        shortcut: "Ctrl+F"
        onTriggered: window.focusPlaylistSearch()
    }
    Action {
        id: showPlaylistAction
        text: qsTr("Show playlist")
        shortcut: "Ctrl+L"
        checkable: true
        checked: window.playlistVisible
        onTriggered: window.playlistVisible = !window.playlistVisible
    }
    Action {
        id: fullscreenAction
        text: qsTr("Full screen")
        shortcut: "F11"
        checkable: true
        checked: window.visibility === Window.FullScreen
        onTriggered: window.toggleFullscreen()
    }
    Action {
        id: pinAction
        text: qsTr("Always on top")
        checkable: true
        checked: window.alwaysOnTop
        onTriggered: window.alwaysOnTop = !window.alwaysOnTop
    }
    PlayerMenu {
        id: appMenu
        objectName: "appMenu"
        parent: window.contentItem
        PlayerMenuItem { action: openMediaAction }
        PlayerMenuItem {
            objectName: "addMediaMenuItem"
            text: qsTr("Add to playlist…")
            onTriggered: window.openMedia(false)
        }
        PlayerMenuItem {
            objectName: "clearPlaylistMenuItem"
            text: qsTr("Clear playlist")
            enabled: mediaPlaylist.count > 0
            onTriggered: window.clearPlaylist()
        }
        PlayerMenuSeparator {}
        PlayerMenuItem { action: searchPlaylistAction }
        PlayerMenuItem { action: showPlaylistAction }
        PlayerMenuSeparator {}
        PlayerMenuItem { action: fullscreenAction }
        PlayerMenuItem { action: pinAction }
        PlayerMenuSeparator {}
        PlayerMenuItem { text: qsTr("Exit"); onTriggered: window.close() }
    }
    PlayerMenu {
        id: settingsMenu
        objectName: "playbackSettingsMenu"
        parent: window.contentItem
        PlayerMenuItem { text: qsTr("Repeat mode"); enabled: false }
        PlayerMenuItem {
            objectName: "repeatOffMenuItem"
            text: qsTr("Repeat off")
            checkable: true
            autoExclusive: true
            checked: window.repeatMode === 0
            onTriggered: window.repeatMode = 0
        }
        PlayerMenuItem {
            objectName: "repeatTrackMenuItem"
            text: qsTr("Repeat current track")
            checkable: true
            autoExclusive: true
            checked: window.repeatMode === 1
            onTriggered: window.repeatMode = 1
        }
        PlayerMenuItem {
            objectName: "repeatPlaylistMenuItem"
            text: qsTr("Repeat playlist")
            checkable: true
            autoExclusive: true
            checked: window.repeatMode === 2
            onTriggered: window.repeatMode = 2
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: window.visibility === Window.FullScreen
            && !playlist.editingSearch && !playlist.menuOpen
            && !appMenu.visible && !settingsMenu.visible && !mediaDialog.visible
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequence: "Space"
        enabled: window.playbackShortcutsEnabled
        onActivated: window.togglePlayback()
    }
    Shortcut {
        sequence: "M"
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.muted = !videoStage.muted
    }
    Shortcut {
        sequence: "Left"
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seekRelative(-10)
    }
    Shortcut {
        sequence: "Right"
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seekRelative(10)
    }
    Shortcut {
        sequence: "Up"
        enabled: window.playbackShortcutsEnabled
        onActivated: {
            videoStage.volume = Math.min(100, videoStage.volume + 5)
            videoStage.muted = false
        }
    }
    Shortcut {
        sequence: "Down"
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.volume = Math.max(0, videoStage.volume - 5)
    }

    Item {
        anchors.fill: parent
        anchors.margins: Theme.windowBorder

        TitleBar {
            id: titleBar
            anchors.top: parent.top
            width: parent.width
            maximized: window.visibility === Window.Maximized
            fullscreen: window.visibility === Window.FullScreen
            alwaysOnTop: window.alwaysOnTop
            onMenuRequested: window.showAppMenu(false)
            onPinRequested: window.alwaysOnTop = !window.alwaysOnTop
            onMinimizeRequested: window.showMinimized()
            onMaximizeRequested: {
                if (window.visibility === Window.Maximized)
                    window.showNormal()
                else
                    window.showMaximized()
            }
            onFullscreenRequested: window.toggleFullscreen()
            onCloseRequested: window.close()
        }

        Item {
            anchors.top: titleBar.bottom
            anchors.topMargin: Theme.dividerWidth
            anchors.bottom: parent.bottom
            width: parent.width

            Item {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: playlist.left
                anchors.rightMargin: window.playlistVisible ? Theme.panelDivider : 0

                VideoStage {
                    id: videoStage
                    source: mediaPlaylist.currentSource
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: playback.top
                    onMoveRequested: {
                        if (window.visibility !== Window.FullScreen)
                            window.startSystemMove()
                    }
                    onFullscreenRequested: window.toggleFullscreen()
                    onPlayPauseRequested: window.togglePlayback()
                    onEndOfFile: window.advanceAfterEnd()
                }
                PlaybackBar {
                    id: playback
                    objectName: "playbackBar"
                    anchors.bottom: parent.bottom
                    width: parent.width
                    playing: videoStage.playing
                    position: videoStage.position
                    duration: videoStage.duration
                    seekable: videoStage.seekable
                    muted: videoStage.muted
                    volume: videoStage.volume
                    mediaSource: mediaPlaylist.currentSource
                    playlistVisible: window.playlistVisible
                    onPlaylistRequested: window.playlistVisible = !window.playlistVisible
                    onSearchRequested: window.focusPlaylistSearch()
                    onSettingsRequested: window.showSettingsMenu()
                    onMenuRequested: window.showAppMenu(true)
                    onMuteRequested: videoStage.muted = !videoStage.muted
                    onVolumeRequested: (value) => {
                        videoStage.volume = value
                        if (value > 0)
                            videoStage.muted = false
                    }
                    onSeekRequested: (seconds) => videoStage.seek(seconds)
                    onSeekRelativeRequested: (seconds) => videoStage.seekRelative(seconds)
                    onPreviousRequested: window.skipTrack(-1)
                    onNextRequested: window.skipTrack(1)
                    onPlayPauseRequested: window.togglePlayback()
                    onStopRequested: videoStage.stop()
                    onOpenRequested: window.openMedia(true)
                }
            }

            PlaylistPanel {
                id: playlist
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                visible: window.playlistVisible
                width: window.playlistVisible
                    ? Math.min(Theme.playlistWidth, parent.width * Theme.playlistRatio) : 0
                playlistModel: mediaPlaylist
                onTrackRequested: (index) => window.playTrack(index)
                onAddRequested: window.openMedia(false)
                onClearRequested: window.clearPlaylist()
            }
        }
    }

    WindowResizeFrame {
        anchors.fill: parent
        enabled: window.visibility === Window.Windowed
        onResizeRequested: (edges) => window.startSystemResize(edges)
    }
}
