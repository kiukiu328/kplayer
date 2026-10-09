import QtQuick
import QtCore
import QtQuick.Dialogs
import QtQuick.Controls.Basic

Window {
    id: window
    width: 1225
    height: 824
    minimumWidth: isFullscreen ? 0 : 1100
    minimumHeight: isFullscreen ? 0 : 500
    visible: true
    title: "KPlayer"
    color: Theme.border
    flags: Qt.Window | Qt.FramelessWindowHint
        | (alwaysOnTop ? Qt.WindowStaysOnTopHint : 0)
    property url mediaSource
    readonly property string interfaceLanguage: AppLanguage.language
    signal windowMoveRequested()
    onWindowMoveRequested: {
        if (!isFullscreen)
            startSystemMove()
    }

    function preferSubtitleLanguage(language: string) {
        const codes = preferences.defaultLanguage.toLowerCase().split(",")
            .map(code => code.trim()).filter(code => code.length > 0)
        if (!codes.includes(language))
            codes.push(language)
        preferences.defaultLanguage = codes.join(",")
        videoStage.showHint(qsTr("已加入預設字幕語言：%1").arg(language))
    }
    property list<url> startupFiles: []
    readonly property PlaylistModel mediaPlaylist: playlists.currentPlaylist
    property int visibilityBeforeFullscreen: Window.Windowed
    readonly property bool isFullscreen: visibility === Window.FullScreen
    property bool playlistVisible: true
    property bool controlsVisible: true
    property bool alwaysOnTop: false
    property bool playlistBeforeFullscreen: true
    property bool controlsBeforeFullscreen: true
    // Manual track navigation always respects the queue boundaries.
    property int repeatMode: 0 // 0: off, 1: current track, 2: entire playlist
    onRepeatModeChanged: videoStage.showHint([qsTr("Repeat off"),
        qsTr("Repeat current file"), qsTr("Repeat playlist")][repeatMode])
    onAlwaysOnTopChanged: videoStage.showHint(alwaysOnTop
        ? qsTr("Always on top on") : qsTr("Always on top off"))
    onPlaylistVisibleChanged: videoStage.showHint(playlistVisible
        ? qsTr("Playlist shown") : qsTr("Playlist hidden"))
    onControlsVisibleChanged: videoStage.showHint(controlsVisible
        ? qsTr("Controls shown") : qsTr("Controls hidden"))
    onIsFullscreenChanged: videoStage.showHint(isFullscreen
        ? qsTr("Fullscreen") : qsTr("Fullscreen off"))
    readonly property bool playbackShortcutsEnabled: !playlist.editingSearch
        && !controlPanel.active
        && !playlist.menuOpen
        && !mediaDialog.visible
        && !subtitleDialog.visible && !subtitleMenu.visible
        && !appMenu.visible && !infoDialog.visible
        && !advancedSettings.visible && !videoAdjustments.visible
        && !hasInteractiveFocus()

    function hasInteractiveFocus(): bool {
        // Focus scopes can give their delegate or content item the active focus.
        for (let item = window.activeFocusItem; item !== null; item = item.parent) {
            if (item instanceof TextInput || item instanceof TextEdit
                || item instanceof Slider || item instanceof AbstractButton
                || item instanceof IconButton || item instanceof PlaylistTab
                || item.objectName === "trackList" || item.objectName === "appMenuButton")
                return true
        }
        return false
    }

    function openMedia(playAdded: bool) {
        mediaDialog.playAdded = playAdded
        mediaDialog.open()
    }

    function addOpenedFiles(files: list<url>) {
        mediaPlaylist.appendUniqueUrls(files)
        // Select the requested file before discovering its neighbors.
        for (let file of files) {
            const index = mediaPlaylist.indexOfSource(file)
            if (index >= 0) {
                playTrack(index)
                break
            }
        }
        videoLibrary.addSiblingVideos(mediaPlaylist, files)
    }

    function openSubtitles() {
        appMenu.dismiss()
        subtitleMenu.close()
        subtitleDialog.mediaSource = videoStage.source
        subtitleDialog.open()
    }

    function focusPlaylistSearch() {
        appMenu.close()
        playlistVisible = true
        Qt.callLater(() => playlist.focusSearch())
    }

    function clearPlaylist() {
        videoStage.stop()
        mediaPlaylist.clear()
    }

    function showAppMenu(fromBottom: bool) {
        if (appMenu.visible) {
            appMenu.dismiss()
            return
        }
        if (fromBottom) {
            const point = playback.mapToItem(window.contentItem,
                playback.width - appMenu.width - 8, 0)
            appMenu.x = Math.max(8, point.x)
            appMenu.y = Math.max(8, point.y - appMenu.height - 8)
        } else {
            appMenu.x = Theme.windowBorder
            appMenu.y = Theme.windowBorder + titleBar.height
        }
        appMenu.open()
    }

    function showContextMenu(x: real, y: real) {
        appMenu.dismiss()
        appMenu.popup(Math.max(8, Math.min(x, window.width - appMenu.width - 8)),
            Math.max(8, Math.min(y, window.height - appMenu.height - 8)))
    }

    function showControlPanel() {
        appMenu.close()
        subtitleMenu.close()
        if (!controlPanel.positioned) {
            controlPanel.x = window.x + window.width - controlPanel.width - 24
            controlPanel.y = window.y + Math.max(24, window.height - controlPanel.height - playback.height - 16)
            controlPanel.positioned = true
        }
        controlPanel.open()
    }

    function showAdvancedSettings() {
        appMenu.dismiss()
        subtitleMenu.close()
        advancedSettings.open()
    }

    function toggleFullscreen() {
        if (visibility === Window.FullScreen) {
            playlistVisible = playlistBeforeFullscreen
            controlsVisible = controlsBeforeFullscreen
            if (visibilityBeforeFullscreen === Window.Maximized)
                showMaximized()
            else
                showNormal()
        } else {
            visibilityBeforeFullscreen = visibility
            playlistBeforeFullscreen = playlistVisible
            controlsBeforeFullscreen = controlsVisible
            playlistVisible = false
            controlsVisible = false
            showFullScreen()
        }
    }

    function formatTime(seconds: real): string {
        const total = Math.max(0, Math.floor(seconds))
        return [Math.floor(total / 3600), Math.floor(total / 60) % 60, total % 60]
            .map(value => value.toString().padStart(2, "0")).join(":")
    }

    function showInformation(about: bool) {
        infoDialog.title = about ? qsTr("程式資訊") : qsTr("媒體資訊")
        infoText.text = about ? "KPlayer 0.1.0\n" + qsTr("使用 Qt Quick 與 libmpv 的媒體播放器")
            : qsTr("檔案：") + decodeURIComponent(mediaPlaylist.currentSource.toString())
              + "\n\n" + qsTr("長度：") + formatTime(videoStage.duration)
              + "\n" + qsTr("位置：") + formatTime(videoStage.position)
        infoDialog.open()
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

    PlaylistCollection { id: playlists; objectName: "playlistCollection" }
    Favorites { id: favorites; objectName: "favorites" }
    Settings {
        id: preferences
        category: "subtitles"
        property string defaultLanguage: ""
    }
    Settings {
        id: playbackPreferences
        category: "playback"
        property real maximumSpeed: 16
        property real playlistWidth: Theme.playlistWidth
    }
    AdvancedSettings {
        id: advancedSettings
        objectName: "advancedSettingsDialog"
        parent: window.contentItem
        width: Math.min(900, window.width - 32)
        height: Math.min(600, window.height - 32)
        maximumSpeed: playbackPreferences.maximumSpeed
        playbackSpeed: videoStage.speed
        onSpeedSaved: (speed, maximum) => {
            playbackPreferences.maximumSpeed = maximum
            videoStage.speed = speed
        }
        subtitleLanguage: preferences.defaultLanguage
        onLanguageSaved: (language) => preferences.defaultLanguage = language
    }
    VideoLibrary { id: videoLibrary; objectName: "videoLibrary" }
    ControlPanel {
        id: controlPanel
        objectName: "controlPanelWindow"
        transientParent: window
        player: videoStage
    }
    VideoAdjustments {
        id: videoAdjustments
        objectName: "videoAdjustmentsDialog"
        parent: window.contentItem
        anchors.centerIn: parent
        width: Math.min(420, window.width - 32)
        player: videoStage
    }
    FileAssociations { id: fileAssociations; objectName: "fileAssociations" }
    Connections {
        target: playlists
        function onCurrentPlaylistChanged() { videoStage.stop() }
    }
    Component.onCompleted: {
        if (mediaSource.toString().length > 0)
            mediaPlaylist.appendUrls([mediaSource])
        if (startupFiles.length > 0)
            Qt.callLater(() => window.addOpenedFiles(startupFiles))
    }
    Action {
        id: associateVideosAction
        text: qsTr("設為預設影片播放器…")
        enabled: fileAssociations.supported
        onTriggered: {
            if (!fileAssociations.openDefaultApps()) {
                infoDialog.title = qsTr("影片檔案關聯")
                infoText.text = fileAssociations.errorString
                infoDialog.open()
            }
        }
    }

    FileDialog {
        id: mediaDialog
        objectName: "mediaDialog"
        property bool playAdded: true
        title: playAdded ? qsTr("Open media") : qsTr("Add media to playlist")
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            if (playAdded)
                window.addOpenedFiles(selectedFiles)
            else
                window.mediaPlaylist.appendUrls(selectedFiles)
        }
    }

    FileDialog {
        id: subtitleDialog
        objectName: "subtitleDialog"
        property url mediaSource
        title: qsTr("新增字幕")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("字幕檔案 (*.srt *.ass *.ssa *.vtt *.sub *.idx *.sup *.smi *.txt)"),
                      qsTr("所有檔案 (*)")]
        onAccepted: {
            if (mediaSource.toString() === videoStage.source.toString()) {
                for (let file of selectedFiles)
                    videoStage.addSubtitle(file)
            }
        }
    }
    SubtitleMenu {
        id: subtitleMenu
        objectName: "subtitleSelectionMenu"
        preferredLanguages: preferences.defaultLanguage
        onPreferLanguageRequested: (language) => window.preferSubtitleLanguage(language)
        parent: window.contentItem
        player: videoStage
        onAddRequested: window.openSubtitles()
    }

    Action {
        id: openMediaAction
        text: qsTr("開啟檔案…")
        shortcut: "F3"
        onTriggered: window.openMedia(true)
    }
    Action {
        id: searchPlaylistAction
        text: qsTr("搜尋播放清單")
        shortcut: "Ctrl+F"
        onTriggered: window.focusPlaylistSearch()
    }
    Action {
        id: showPlaylistAction
        text: qsTr("播放清單…")
        shortcut: "F6"
        checkable: true
        checked: window.playlistVisible
        onTriggered: window.playlistVisible = !window.playlistVisible
    }
    Action {
        id: fullscreenAction
        text: qsTr("全螢幕")
        checkable: true
        checked: window.visibility === Window.FullScreen
        onTriggered: window.toggleFullscreen()
    }
    Action {
        id: pinAction
        text: qsTr("最上層顯示")
        checkable: true
        checked: window.alwaysOnTop
        onTriggered: window.alwaysOnTop = !window.alwaysOnTop
    }
    Action {
        id: closeMediaAction
        text: qsTr("關閉檔案")
        shortcut: "F4"
        enabled: window.mediaPlaylist.count > 0
        onTriggered: videoStage.stop()
    }
    Action {
        id: preferencesAction
        text: qsTr("進階設定…")
        shortcut: "F5"
        onTriggered: window.showAdvancedSettings()
    }
    Action {
        id: controlsAction
        text: qsTr("控制面版…")
        shortcut: "F7"
        checkable: true
        checked: window.controlsVisible
        onTriggered: window.controlsVisible = !window.controlsVisible
    }
    Action {
        id: mediaInfoAction
        text: qsTr("媒體資訊…")
        shortcut: "Ctrl+F1"
        enabled: window.mediaPlaylist.count > 0
        onTriggered: window.showInformation(false)
    }
    Action {
        id: aboutAction
        text: qsTr("程式資訊…")
        shortcut: "F1"
        onTriggered: window.showInformation(true)
    }
    Action {
        id: exitAction
        text: qsTr("結束")
        shortcut: "Alt+F4"
        onTriggered: window.close()
    }
    AppMenu {
        preferredSubtitleLanguages: preferences.defaultLanguage
        onPreferSubtitleLanguageRequested: (language) => window.preferSubtitleLanguage(language)
        id: appMenu
        parent: window.contentItem
        openAction: openMediaAction
        closeAction: closeMediaAction
        preferencesAction: preferencesAction
        playlistAction: showPlaylistAction
        controlsAction: controlsAction
        fullscreenAction: fullscreenAction
        pinAction: pinAction
        mediaInfoAction: mediaInfoAction
        aboutAction: aboutAction
        exitAction: exitAction
        searchAction: searchPlaylistAction
        associateVideosAction: associateVideosAction
        player: videoStage
        onAddSubtitleRequested: window.openSubtitles()
        favorites: favorites
        onFavoriteRequested: (source) => {
            window.mediaPlaylist.appendUniqueUrls([source])
            const index = window.mediaPlaylist.indexOfSource(source)
            if (index >= 0) {
                window.playTrack(index)
            } else {
                infoDialog.title = qsTr("我的最愛")
                infoText.text = qsTr("無法開啟檔案：") + source.toString()
                infoDialog.open()
            }
        }
        onVideoAdjustmentsRequested: {
            appMenu.dismiss()
            videoAdjustments.open()
        }
        hasMedia: window.mediaPlaylist.count > 0
        playing: videoStage.playing
        seekable: videoStage.seekable
        muted: videoStage.muted
        repeatMode: window.repeatMode
        onAddMediaRequested: window.openMedia(false)
        onClearPlaylistRequested: window.clearPlaylist()
        onPlayPauseRequested: window.togglePlayback()
        onStopRequested: videoStage.stop()
        onSkipTrackRequested: (offset) => window.skipTrack(offset)
        onSeekRequested: (seconds) => videoStage.seek(seconds)
        onMuteRequested: videoStage.muted = !videoStage.muted
        onVolumeRequested: (offset) => {
            videoStage.volume = Math.max(0, Math.min(100, videoStage.volume + offset))
            if (offset > 0)
                videoStage.muted = false
        }
        onRepeatRequested: (mode) => window.repeatMode = mode
        onCopyPositionRequested: {
            clipboardText.text = window.formatTime(videoStage.position)
            clipboardText.selectAll()
            clipboardText.copy()
        }
        onWindowSizeRequested: (maximized) => {
            if (window.visibility === Window.FullScreen)
                window.toggleFullscreen()
            if (maximized) {
                window.showMaximized()
            } else {
                window.showNormal()
                window.width = 1225
                window.height = 824
            }
        }
    }
    TextEdit { id: clipboardText; visible: false }
    Dialog {
        id: infoDialog
        objectName: "informationDialog"
        parent: window.contentItem
        anchors.centerIn: parent
        width: Math.min(420, window.width - 32)
        modal: true
        standardButtons: Dialog.Close
        palette.window: Theme.chromeBackground
        palette.windowText: Theme.textPrimary
        palette.text: Theme.textPrimary
        palette.button: Theme.buttonBackground
        palette.buttonText: Theme.textPrimary
        contentItem: Label {
            id: infoText
            wrapMode: Text.WrapAnywhere
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            color: Theme.textPrimary
        }
    }
    Shortcut { sequence: "Ctrl+O"; onActivated: openMediaAction.trigger() }
    Shortcut { sequence: "Ctrl+L"; onActivated: showPlaylistAction.trigger() }
    Shortcut {
        sequence: "Return"
        enabled: window.playbackShortcutsEnabled
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequence: "Enter"
        enabled: window.playbackShortcutsEnabled
        onActivated: window.toggleFullscreen()
    }

    Shortcut {
        sequence: "Escape"
        enabled: window.visibility === Window.FullScreen
            && !controlPanel.active
            && !playlist.editingSearch && !playlist.menuOpen
            && !appMenu.visible && !mediaDialog.visible
            && !infoDialog.visible
            && !subtitleDialog.visible && !subtitleMenu.visible
            && !advancedSettings.visible
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

    Shortcut {
        sequence: "Z"
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.toggleSpeed()
    }
    Shortcut {
        sequence: "X"
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.speed = Math.round((videoStage.speed - 0.1) * 10) / 10
    }
    Shortcut {
        sequence: "C"
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.speed = Math.round((videoStage.speed + 0.1) * 10) / 10
    }
    Shortcut {
        sequence: "PgUp"
        enabled: window.playbackShortcutsEnabled
        onActivated: window.skipTrack(-1)
    }
    Shortcut {
        sequence: "PgDown"
        enabled: window.playbackShortcutsEnabled
        onActivated: window.skipTrack(1)
    }
    Shortcut {
        sequence: "Alt+H"
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.subtitlesVisible = !videoStage.subtitlesVisible
    }
    Shortcut {
        sequence: "Alt+L"
        enabled: window.playbackShortcutsEnabled && videoStage.mediaLoaded
        onActivated: videoStage.cycleSubtitle()
    }
    Shortcut {
        sequence: "Alt+O"
        enabled: window.playbackShortcutsEnabled && videoStage.mediaLoaded
        onActivated: window.openSubtitles()
    }
    Shortcut {
        sequence: "L"
        enabled: window.playbackShortcutsEnabled
        onActivated: subtitleMenu.popup(32, 64)
    }
    Shortcut {
        sequence: "Backspace"
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seek(0)
    }
    Shortcut {
        sequence: "Ctrl+Backspace"
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seek(videoStage.duration / 2)
    }
    Shortcut {
        sequence: "Shift+Backspace"
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seek(Math.max(0, videoStage.duration - 30))
    }
    Shortcut {
        sequence: "Ctrl+Left"
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seekRelative(-30)
    }
    Shortcut {
        sequence: "Ctrl+Right"
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seekRelative(30)
    }
    Shortcut {
        sequence: "Shift+Left"
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seekRelative(-60)
    }
    Shortcut {
        sequence: "Shift+Right"
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seekRelative(60)
    }
    Shortcut {
        sequence: "D"
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.stepFrame(false)
    }
    Shortcut {
        sequence: "F"
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.stepFrame(true)
    }

    Item {
        anchors.fill: parent
        anchors.margins: window.isFullscreen ? 0 : Theme.windowBorder

        TitleBar {
            id: titleBar
            objectName: "titleBar"
            visible: !window.isFullscreen
            height: visible ? Theme.titleHeight : 0
            anchors.top: parent.top
            width: parent.width
            maximized: window.visibility === Window.Maximized
            fullscreen: window.visibility === Window.FullScreen
            alwaysOnTop: window.alwaysOnTop
            menuOpen: appMenu.visible
            mediaName: window.mediaPlaylist.currentTitle
            mediaFormat: window.mediaPlaylist.currentFormat
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
            anchors.topMargin: window.isFullscreen ? 0 : Theme.dividerWidth
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
                    maximumSpeed: playbackPreferences.maximumSpeed
                    defaultSubtitleLanguage: preferences.defaultLanguage
                    source: window.mediaPlaylist.currentSource
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: window.isFullscreen ? parent.bottom : playback.top
                    onMoveRequested: if (!window.isFullscreen) window.windowMoveRequested()
                    onFullscreenRequested: window.toggleFullscreen()
                    onPlayPauseRequested: window.togglePlayback()
                    onEndOfFile: window.advanceAfterEnd()
                    onContextMenuRequested: (x, y) => {
                        const point = videoStage.mapToItem(window.contentItem, x, y)
                        window.showContextMenu(point.x, point.y)
                    }
                }
                Item {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Theme.seekHeight + Theme.transportHeight + 16
                    z: 1
                    HoverHandler {
                        id: bottomHover
                        enabled: window.isFullscreen
                        blocking: false
                    }
                }
                PlaybackBar {
                    id: playback
                    objectName: "playbackBar"
                    anchors.bottom: parent.bottom
                    width: parent.width
                    visible: window.controlsVisible || (window.isFullscreen
                        && (bottomHover.hovered || playback.interacting || activeFocus
                            || appMenu.visible))
                    height: visible ? Theme.seekHeight + Theme.transportHeight : 0
                    playing: videoStage.playing
                    position: videoStage.position
                    duration: videoStage.duration
                    seekable: videoStage.seekable
                    muted: videoStage.muted
                    volume: videoStage.volume
                    mediaSource: window.mediaPlaylist.currentSource
                    playlistVisible: window.playlistVisible
                    onPlaylistRequested: window.playlistVisible = !window.playlistVisible
                    onSearchRequested: window.focusPlaylistSearch()
                    onSettingsRequested: window.showControlPanel()
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

            MouseArea {
                objectName: "playlistResizeHandle"
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                x: playlist.x - width / 2
                width: 8
                z: 2
                visible: window.playlistVisible
                cursorShape: Qt.SplitHCursor
                preventStealing: true
                property real startX
                property real startWidth
                onPressed: (mouse) => {
                    startX = mapToItem(window.contentItem, mouse.x, mouse.y).x
                    startWidth = playlist.width
                }
                onPositionChanged: (mouse) => {
                    if (pressed) {
                        const currentX = mapToItem(window.contentItem, mouse.x, mouse.y).x
                        playbackPreferences.playlistWidth = Math.max(360,
                            Math.min(parent.width - 400, startWidth + startX - currentX))
                    }
                }
            }

            PlaylistPanel {
                id: playlist
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                visible: window.playlistVisible
                width: window.playlistVisible
                    ? Math.max(360, Math.min(playbackPreferences.playlistWidth, parent.width - 400)) : 0
                playlistModel: window.mediaPlaylist
                playlistCollection: playlists
                onTrackRequested: (index) => window.playTrack(index)
                onAddRequested: window.openMedia(false)
                onClearRequested: window.clearPlaylist()
            }
        }
    }

    WindowDragArea {
        objectName: "windowDragArea"
        anchors.fill: parent
        surface: window.contentItem
        enabled: !window.isFullscreen
        z: 50
        onMoveRequested: window.windowMoveRequested()
    }

    WindowResizeFrame {
        anchors.fill: parent
        enabled: window.visibility === Window.Windowed
        onResizeRequested: (edges) => window.startSystemResize(edges)
    }
}
