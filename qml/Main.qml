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
    title: qsTr("KPlayer")
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
        videoStage.defaultSubtitleRules = ""
        videoStage.defaultSubtitleTrackName = ""
        videoStage.defaultSubtitleTrack = 0
        const codes = videoStage.defaultSubtitleLanguage.toLowerCase().split(",")
            .map(code => code.trim()).filter(code => code.length > 0)
        if (!codes.includes(language))
            codes.push(language)
        videoStage.defaultSubtitleLanguage = codes.join(",")
        videoStage.showHint(qsTr("已加入預設字幕語言：%1").arg(language))
    }

    function preferSubtitleTrack(track: int) {
        videoStage.defaultSubtitleRules = ""
        videoStage.defaultSubtitleLanguage = ""
        videoStage.defaultSubtitleTrackName = ""
        videoStage.defaultSubtitleTrack = track
        videoStage.showHint(qsTr("已設定預設字幕軌編號：%1").arg(track))
    }

    function preferSubtitleTrackName(name: string) {
        videoStage.defaultSubtitleRules = ""
        videoStage.defaultSubtitleLanguage = ""
        videoStage.defaultSubtitleTrack = 0
        videoStage.defaultSubtitleTrackName = name
        videoStage.showHint(qsTr("已設定預設字幕軌名稱：%1").arg(name))
    }

    function clearSubtitlePreference() {
        videoStage.defaultSubtitleRules = ""
        videoStage.defaultSubtitleLanguage = ""
        videoStage.defaultSubtitleTrack = 0
        videoStage.defaultSubtitleTrackName = ""
        videoStage.showHint(qsTr("字幕選擇方式：自動"))
    }
    function addCurrentSubtitleRule(kind: string) {
        appMenu.dismiss()
        subtitleMenu.dismiss()
        advancedSettings.openForSubtitleRule(kind)
    }
    property list<url> startupFiles: []
    readonly property PlaylistModel mediaPlaylist: playlists.currentPlaylist
    property PlaylistModel playingPlaylist: null
    readonly property PlaylistModel playbackPlaylist: playingPlaylist ?? mediaPlaylist
    property int visibilityBeforeFullscreen: Window.Windowed
    readonly property bool isFullscreen: visibility === Window.FullScreen
    property bool playlistVisible: true
    property bool videoDragActive: false
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
    readonly property bool trackNavigationShortcutsEnabled: !playlist.editingSearch
        && !controlPanel.active
        && !playlist.menuOpen
        && !mediaDialog.visible
        && !folderDialog.visible
        && !subtitleDialog.visible && !subtitleMenu.visible
        && !appMenu.visible && !infoDialog.visible
        && !advancedSettings.visible && !videoAdjustments.visible
        && !hasInteractiveFocus()
    readonly property bool playbackShortcutsEnabled: trackNavigationShortcutsEnabled
        && !hasSliderFocus()
    readonly property bool activationShortcutsEnabled: playbackShortcutsEnabled
        && !hasButtonFocus()
    readonly property bool applicationShortcutsEnabled: !advancedSettings.visible
        && !mediaDialog.visible && !folderDialog.visible && !subtitleDialog.visible
        && !infoDialog.visible && !videoAdjustments.visible

    function shortcutSequences(command: string): var {
        // Reading entries keeps these bindings live when shortcuts are saved.
        const entries = shortcutStore.entries
        return shortcutStore.sequences(command)
    }
    function applicationShortcutSequences(command: string): var {
        const sequences = shortcutSequences(command)
        for (let item = window.activeFocusItem; item !== null; item = item.parent) {
            if (item instanceof TextInput || item instanceof TextEdit) {
                // Keep typing and cursor movement available after rebinding an action.
                return sequences.filter(sequence => sequence.includes("Ctrl+")
                    || sequence.includes("Alt+") || sequence.includes("Meta+")
                    || /^F\d{1,2}$/.test(sequence))
            }
        }
        return sequences
    }

    function hasInteractiveFocus(): bool {
        // Focus scopes can give their delegate or content item the active focus.
        for (let item = window.activeFocusItem; item !== null; item = item.parent) {
            if (item instanceof TextInput || item instanceof TextEdit
                || item.objectName === "trackList")
                return true
        }
        return false
    }

    function hasSliderFocus(): bool {
        for (let item = window.activeFocusItem; item !== null; item = item.parent) {
            if (item instanceof Slider)
                return true
        }
        return false
    }

    function hasButtonFocus(): bool {
        for (let item = window.activeFocusItem; item !== null; item = item.parent) {
            if (item instanceof AbstractButton || item instanceof IconButton
                || item instanceof PlaylistTab || item.objectName === "appMenuButton")
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
        library.addSiblingVideos(mediaPlaylist, files)
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
        if (mediaPlaylist === playbackPlaylist)
            videoStage.stop()
        mediaPlaylist.clear()
    }

    function showAppMenu() {
        if (appMenu.visible) {
            appMenu.dismiss()
            return
        }
        appMenu.x = Theme.windowBorder
        appMenu.y = Theme.windowBorder + titleBar.height
        appMenu.open()
    }

    function showContextMenu(x: real, y: real) {
        appMenu.dismiss()
        appMenu.popup(Math.max(8, Math.min(x, window.width - appMenu.width - 8)),
            Math.max(8, Math.min(y, window.height - appMenu.height - 8)))
    }

    function toggleControlPanel() {
        appMenu.close()
        subtitleMenu.close()
        if (controlPanel.visible) {
            controlPanel.hide()
            return
        }
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

    function updateVideoDrag() {
        videoDragActive = fullscreenVideoDrag.containsDrag || playlist.receivingVideoDrag
    }

    function formatTime(seconds: real): string {
        const total = Math.max(0, Math.floor(seconds))
        return [Math.floor(total / 3600), Math.floor(total / 60) % 60, total % 60]
            .map(value => value.toString().padStart(2, "0")).join(":")
    }

    function showInformation(about: bool) {
        infoDialog.title = about ? qsTr("程式資訊") : qsTr("媒體資訊")
        infoText.text = about ? qsTr("KPlayer %1\n%2").arg("0.1.0").arg(qsTr("使用 Qt Quick 與 libmpv 的媒體播放器"))
            : qsTr("檔案：") + decodeURIComponent(videoStage.source.toString())
              + "\n\n" + qsTr("長度：") + formatTime(videoStage.duration)
              + "\n" + qsTr("位置：") + formatTime(videoStage.position)
        infoDialog.open()
    }

    function togglePlayback() {
        if (playbackPlaylist.count === 0)
            openMedia(true)
        else
            videoStage.togglePause()
    }

    function playTrack(index: int) {
        playQueueTrack(mediaPlaylist, index)
    }

    function playQueueTrack(queue: PlaylistModel, index: int) {
        if (index < 0 || index >= queue.count)
            return
        videoStage.stop()
        playingPlaylist = queue
        queue.currentIndex = index
        videoStage.play()
    }

    function skipTrack(offset: int) {
        playQueueTrack(playbackPlaylist, playbackPlaylist.currentIndex + offset)
    }

    function advanceAfterEnd() {
        if (playbackPlaylist.count === 0)
            return
        if (repeatMode === 1)
            playQueueTrack(playbackPlaylist, playbackPlaylist.currentIndex)
        else if (playbackPlaylist.currentIndex + 1 < playbackPlaylist.count)
            skipTrack(1)
        else if (repeatMode === 2)
            playQueueTrack(playbackPlaylist, 0)
    }

    PlaylistCollection {
        id: playlists
        objectName: "playlistCollection"
        persistent: true
    }
    Favorites { id: favorites; objectName: "favorites" }
    ShortcutBindings { id: shortcutStore; objectName: "shortcutBindings" }
    Settings {
        id: playbackPreferences
        objectName: "playbackPreferences"
        category: "playback"
        property real playlistWidth: Theme.playlistWidth
        property bool previewEnabled: true
    }
    Settings {
        id: windowPreferences
        category: "window"
        property int normalWidth: 1225
        property int normalHeight: 824
        property bool maximized: false
        property bool playlistShown: true
        property bool controlsShown: true
        property alias alwaysOnTop: window.alwaysOnTop
        property alias repeatMode: window.repeatMode
    }
    property bool windowPreferencesReady: false
    property int lastWindowedWidth: 1225
    property int lastWindowedHeight: 824
    onWidthChanged: {
        if (windowPreferencesReady && visibility === Window.Windowed)
            lastWindowedWidth = width
    }
    onHeightChanged: {
        if (windowPreferencesReady && visibility === Window.Windowed)
            lastWindowedHeight = height
    }
    onClosing: {
        videoStage.saveResumePosition()
        windowPreferences.normalWidth = lastWindowedWidth
        windowPreferences.normalHeight = lastWindowedHeight
        windowPreferences.maximized = (isFullscreen ? visibilityBeforeFullscreen : visibility) === Window.Maximized
        windowPreferences.playlistShown = isFullscreen ? playlistBeforeFullscreen : playlistVisible
        windowPreferences.controlsShown = isFullscreen ? controlsBeforeFullscreen : controlsVisible
        windowPreferences.sync()
    }
    AdvancedSettings {
        id: advancedSettings
        objectName: "advancedSettingsDialog"
        parent: window.contentItem
        width: Math.min(900, window.width - 32)
        height: Math.min(600, window.height - 32)
        maximumSpeed: videoStage.maximumSpeed
        playbackSpeed: videoStage.speed
        previewEnabled: playbackPreferences.previewEnabled
        player: videoStage
        shortcutBindings: shortcutStore
        captureDirectory: videoStage.captureDirectory
        onCaptureDirectorySaved: (directory) => videoStage.captureDirectory = directory
        onPreviewSaved: (enabled) => playbackPreferences.previewEnabled = enabled
        onSpeedSaved: (speed, maximum) => {
            videoStage.maximumSpeed = maximum
            videoStage.speed = speed
        }
        subtitleLanguage: videoStage.defaultSubtitleLanguage
        subtitleTrackNumber: videoStage.defaultSubtitleTrack
        subtitleTrackName: videoStage.defaultSubtitleTrackName
        subtitleRules: videoStage.defaultSubtitleRules
        subtitleStyleOverride: videoStage.subtitleStyleOverride
        currentSubtitleTrack: videoStage.subtitleTracks.find(track => track.id === videoStage.subtitleTrack) || null
        onSubtitleSaved: (language, track, overrideStyle, trackName, rules) => {
            videoStage.defaultSubtitleLanguage = language
            videoStage.defaultSubtitleTrack = track
            videoStage.defaultSubtitleTrackName = trackName
            videoStage.defaultSubtitleRules = rules
            videoStage.subtitleStyleOverride = overrideStyle
        }
    }
    VideoLibrary { id: library; objectName: "videoLibrary" }
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
    FullscreenCursor {
        objectName: "fullscreenCursor"
        window: window
        enabled: window.isFullscreen && !playlist.menuOpen && !playlist.draggingTracks
            && !window.videoDragActive
            && !playlistResizeHandle.pressed && !playback.interacting
            && !appMenu.visible && !subtitleMenu.visible && !controlPanel.active
            && !mediaDialog.visible && !folderDialog.visible && !subtitleDialog.visible
            && !infoDialog.visible && !advancedSettings.visible && !videoAdjustments.visible
    }
    Connections {
        target: playlists
        function onCurrentPlaylistChanged() {
            if (videoStage.stopped)
                window.playingPlaylist = window.mediaPlaylist
        }
        function onPlaylistAboutToBeRemoved(queue: PlaylistModel) {
            if (window.playingPlaylist === queue)
                window.playingPlaylist = window.mediaPlaylist
        }
    }
    Component.onCompleted: {
        playingPlaylist = mediaPlaylist
        width = Math.max(minimumWidth, Math.min(windowPreferences.normalWidth, Screen.width))
        height = Math.max(minimumHeight, Math.min(windowPreferences.normalHeight, Screen.height))
        lastWindowedWidth = width
        lastWindowedHeight = height
        playlistVisible = windowPreferences.playlistShown
        controlsVisible = windowPreferences.controlsShown
        if (windowPreferences.maximized)
            showMaximized()
        windowPreferencesReady = true
        if (startupFiles.length > 0) {
            playlists.currentIndex = 0
            playlists.defaultPlaylist.clear()
            playingPlaylist = playlists.defaultPlaylist
            Qt.callLater(() => window.addOpenedFiles(startupFiles))
        } else if (mediaSource.toString().length > 0) {
            mediaPlaylist.appendUrls([mediaSource])
        }
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
    FolderDialog {
        id: folderDialog
        objectName: "playlistFolderDialog"
        title: qsTr("新增資料夾為播放清單")
        onAccepted: {
            if (library.addFolderPlaylist(playlists, selectedFolder) < 0)
                videoStage.showHint(qsTr("此資料夾沒有支援的影片"))
        }
    }
    SubtitleMenu {
        shortcutBindings: shortcutStore
        id: subtitleMenu
        objectName: "subtitleSelectionMenu"
        preferredLanguages: videoStage.defaultSubtitleLanguage
        preferredTrackNumber: videoStage.defaultSubtitleTrack
        preferredTrackName: videoStage.defaultSubtitleTrackName
        onPreferLanguageRequested: (language) => window.preferSubtitleLanguage(language)
        onPreferTrackNumberRequested: (track) => window.preferSubtitleTrack(track)
        onPreferTrackNameRequested: (name) => window.preferSubtitleTrackName(name)
        onAutomaticPreferenceRequested: window.clearSubtitlePreference()
        parent: window.contentItem
        player: videoStage
        onAddRequested: window.openSubtitles()
        onAddCurrentRuleRequested: (kind) => window.addCurrentSubtitleRule(kind)
    }

    Action {
        id: openMediaAction
        text: qsTr("開啟檔案…")
        onTriggered: window.openMedia(true)
    }
    Action {
        id: searchPlaylistAction
        text: qsTr("搜尋播放清單")
        onTriggered: window.focusPlaylistSearch()
    }
    Action {
        id: showPlaylistAction
        text: qsTr("播放清單…")
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
        enabled: window.playbackPlaylist.count > 0
        onTriggered: videoStage.stop()
    }
    Action {
        id: preferencesAction
        text: qsTr("進階設定…")
        onTriggered: window.showAdvancedSettings()
    }
    Action {
        id: controlsAction
        text: qsTr("控制面版…")
        checkable: true
        checked: window.controlsVisible
        onTriggered: window.controlsVisible = !window.controlsVisible
    }
    Action {
        id: mediaInfoAction
        text: qsTr("媒體資訊…")
        enabled: window.playbackPlaylist.count > 0
        onTriggered: window.showInformation(false)
    }
    Action {
        id: aboutAction
        text: qsTr("程式資訊…")
        onTriggered: window.showInformation(true)
    }
    Action {
        id: exitAction
        text: qsTr("結束")
        onTriggered: window.close()
    }
    AppMenu {
        shortcutBindings: shortcutStore
        preferredSubtitleLanguages: videoStage.defaultSubtitleLanguage
        preferredSubtitleTrackNumber: videoStage.defaultSubtitleTrack
        preferredSubtitleTrackName: videoStage.defaultSubtitleTrackName
        onPreferSubtitleLanguageRequested: (language) => window.preferSubtitleLanguage(language)
        onPreferSubtitleTrackNumberRequested: (track) => window.preferSubtitleTrack(track)
        onPreferSubtitleTrackNameRequested: (name) => window.preferSubtitleTrackName(name)
        onAutomaticSubtitlePreferenceRequested: window.clearSubtitlePreference()
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
        onAddCurrentSubtitleRuleRequested: (kind) => window.addCurrentSubtitleRule(kind)
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
        hasMedia: window.playbackPlaylist.count > 0
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
        onVolumeRequested: (offset) => videoStage.adjustVolume(offset)
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
        onOpened: standardButton(Dialog.Close).text = Qt.binding(() => qsTr("Close"))
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
    Shortcut {
        sequences: window.applicationShortcutSequences("openMedia")
        enabled: window.applicationShortcutsEnabled
        onActivated: openMediaAction.trigger()
    }
    Shortcut {
        sequences: window.applicationShortcutSequences("searchPlaylist")
        enabled: window.applicationShortcutsEnabled
        onActivated: searchPlaylistAction.trigger()
    }
    Shortcut {
        sequences: window.applicationShortcutSequences("showPlaylist")
        enabled: window.applicationShortcutsEnabled
        onActivated: showPlaylistAction.trigger()
    }
    Shortcut {
        sequences: window.applicationShortcutSequences("closeMedia")
        enabled: window.applicationShortcutsEnabled
        onActivated: closeMediaAction.trigger()
    }
    Shortcut {
        sequences: window.applicationShortcutSequences("preferences")
        enabled: window.applicationShortcutsEnabled
        onActivated: preferencesAction.trigger()
    }
    Shortcut {
        sequences: window.applicationShortcutSequences("controls")
        enabled: window.applicationShortcutsEnabled
        onActivated: controlsAction.trigger()
    }
    Shortcut {
        sequences: window.applicationShortcutSequences("mediaInfo")
        enabled: window.applicationShortcutsEnabled
        onActivated: mediaInfoAction.trigger()
    }
    Shortcut {
        sequences: window.applicationShortcutSequences("about")
        enabled: window.applicationShortcutsEnabled
        onActivated: aboutAction.trigger()
    }
    Shortcut {
        sequences: window.applicationShortcutSequences("exit")
        enabled: window.applicationShortcutsEnabled
        onActivated: exitAction.trigger()
    }

    Shortcut {
        sequences: window.shortcutSequences("fullscreen")
        enabled: window.activationShortcutsEnabled
        onActivated: window.toggleFullscreen()
    }

    Shortcut {
        sequences: window.shortcutSequences("exitFullscreen")
        enabled: window.visibility === Window.FullScreen
            && !controlPanel.active
            && !playlist.editingSearch && !playlist.menuOpen
            && !appMenu.visible && !mediaDialog.visible
            && !folderDialog.visible
            && !infoDialog.visible
            && !subtitleDialog.visible && !subtitleMenu.visible
            && !advancedSettings.visible
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequences: window.shortcutSequences("playPause")
        enabled: window.activationShortcutsEnabled
        onActivated: window.togglePlayback()
    }
    Shortcut {
        sequences: window.shortcutSequences("mute")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.muted = !videoStage.muted
    }
    Shortcut {
        sequences: window.shortcutSequences("captureFrame")
        autoRepeat: false
        enabled: window.trackNavigationShortcutsEnabled && videoStage.mediaLoaded
        onActivated: videoStage.captureCurrentFrame()
    }
    Shortcut {
        sequences: window.shortcutSequences("seekBackward")
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seekRelative(-10)
    }
    Shortcut {
        sequences: window.shortcutSequences("seekForward")
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seekRelative(10)
    }
    Shortcut {
        sequences: window.shortcutSequences("volumeUp")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.adjustVolume(5)
    }
    Shortcut {
        sequences: window.shortcutSequences("volumeDown")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.adjustVolume(-5)
    }

    Shortcut {
        sequences: window.shortcutSequences("toggleSpeed")
        autoRepeat: false
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.toggleSpeed()
    }
    Shortcut {
        sequences: window.shortcutSequences("resetFrame")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.resetFrameSize()
    }
    Shortcut {
        sequences: window.shortcutSequences("frameNarrower")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.adjustFrameSize(-1, 0)
    }
    Shortcut {
        sequences: window.shortcutSequences("frameWider")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.adjustFrameSize(1, 0)
    }
    Shortcut {
        sequences: window.shortcutSequences("frameTaller")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.adjustFrameSize(0, 1)
    }
    Shortcut {
        sequences: window.shortcutSequences("frameShorter")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.adjustFrameSize(0, -1)
    }
    Shortcut {
        sequences: window.shortcutSequences("frameLarger")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.adjustFrameSize(1, 1)
    }
    Shortcut {
        sequences: window.shortcutSequences("frameSmaller")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.adjustFrameSize(-1, -1)
    }
    Shortcut {
        sequences: window.shortcutSequences("speedDown")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.speed = Math.round((videoStage.speed - 0.1) * 10) / 10
    }
    Shortcut {
        sequences: window.shortcutSequences("speedUp")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.speed = Math.round((videoStage.speed + 0.1) * 10) / 10
    }
    Shortcut {
        sequences: window.shortcutSequences("previousTrack")
        enabled: window.trackNavigationShortcutsEnabled
        onActivated: window.skipTrack(-1)
    }
    Shortcut {
        sequences: window.shortcutSequences("nextTrack")
        enabled: window.trackNavigationShortcutsEnabled
        onActivated: window.skipTrack(1)
    }
    Shortcut {
        sequences: window.shortcutSequences("subtitlesVisible")
        enabled: window.playbackShortcutsEnabled
        onActivated: videoStage.subtitlesVisible = !videoStage.subtitlesVisible
    }
    Shortcut {
        sequences: window.shortcutSequences("cycleSubtitle")
        enabled: window.playbackShortcutsEnabled && videoStage.mediaLoaded
        onActivated: videoStage.cycleSubtitle()
    }
    Shortcut {
        sequences: window.shortcutSequences("openSubtitles")
        enabled: window.playbackShortcutsEnabled && videoStage.mediaLoaded
        onActivated: window.openSubtitles()
    }
    Shortcut {
        sequences: window.shortcutSequences("subtitleMenu")
        enabled: window.playbackShortcutsEnabled
        onActivated: subtitleMenu.popup(32, 64)
    }
    Shortcut {
        sequences: window.shortcutSequences("seekStart")
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seek(0)
    }
    Shortcut {
        sequences: window.shortcutSequences("seekMiddle")
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seek(videoStage.duration / 2)
    }
    Shortcut {
        sequences: window.shortcutSequences("seekNearEnd")
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seek(Math.max(0, videoStage.duration - 30))
    }
    Shortcut {
        sequences: window.shortcutSequences("seekBackward30")
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seekRelative(-30)
    }
    Shortcut {
        sequences: window.shortcutSequences("seekForward30")
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seekRelative(30)
    }
    Shortcut {
        sequences: window.shortcutSequences("seekBackward60")
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seekRelative(-60)
    }
    Shortcut {
        sequences: window.shortcutSequences("seekForward60")
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.seekRelative(60)
    }
    Shortcut {
        sequences: window.shortcutSequences("previousFrame")
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.stepFrame(false)
    }
    Shortcut {
        sequences: window.shortcutSequences("nextFrame")
        enabled: window.playbackShortcutsEnabled && videoStage.seekable
        onActivated: videoStage.stepFrame(true)
    }

    Item {
        anchors.fill: parent
        anchors.margins: window.isFullscreen ? 0 : Theme.windowBorder

        TitleBar {
            id: titleBar
            objectName: "titleBar"
            z: 3
            visible: !window.isFullscreen || topHover.hovered || appMenu.visible
            height: visible ? Theme.titleHeight : 0
            anchors.top: parent.top
            width: parent.width
            maximized: window.visibility === Window.Maximized
            fullscreen: window.visibility === Window.FullScreen
            alwaysOnTop: window.alwaysOnTop
            menuOpen: appMenu.visible
            mediaName: window.playbackPlaylist.currentTitle
            mediaFormat: window.playbackPlaylist.currentFormat
            onMenuRequested: window.showAppMenu()
            onPinRequested: window.alwaysOnTop = !window.alwaysOnTop
            onMinimizeRequested: window.showMinimized()
            onMaximizeRequested: {
                if (window.isFullscreen)
                    window.toggleFullscreen()
                if (window.visibility === Window.Maximized)
                    window.showNormal()
                else
                    window.showMaximized()
            }
            onFullscreenRequested: window.toggleFullscreen()
            onCloseRequested: window.close()
        }

        Item {
            anchors.top: parent.top
            width: parent.width
            height: titleBar.visible ? Theme.titleHeight : 8
            z: 4
            HoverHandler {
                id: topHover
                enabled: window.isFullscreen
                blocking: false
            }
        }

        Item {
            id: playerBody
            anchors.top: parent.top
            anchors.topMargin: window.isFullscreen ? 0 : titleBar.height + Theme.dividerWidth
            anchors.bottom: parent.bottom
            width: parent.width

            DropArea {
                id: fullscreenVideoDrag
                anchors.fill: parent
                enabled: window.isFullscreen
                onEntered: (drag) => {
                    drag.accepted = drag.hasUrls && library.canDropUrls(drag.urls)
                    if (drag.accepted)
                        window.videoDragActive = true
                }
                // Defer clearing until the playlist's drop area has received the
                // same move, so switching targets keeps the panel visible.
                onExited: Qt.callLater(window.updateVideoDrag)
                onDropped: (drop) => {
                    drop.accepted = false
                    Qt.callLater(window.updateVideoDrag)
                }
            }

            Item {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: window.isFullscreen ? parent.right : playlist.left
                anchors.rightMargin: !window.isFullscreen && playlist.visible ? Theme.panelDivider : 0

                VideoStage {
                    id: videoStage
                    persistent: true
                    source: window.playbackPlaylist.currentSource
                    subtitleBottomInset: window.isFullscreen && playback.visible ? playback.height : 0
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
                    width: playback.width
                    anchors.bottom: parent.bottom
                    height: playback.implicitHeight + 16
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
                    z: 2
                    anchors.bottom: parent.bottom
                    width: parent.width - (window.isFullscreen && playlist.visible ? playlist.width : 0)
                    visible: window.controlsVisible || (window.isFullscreen
                        && (bottomHover.hovered || playback.hovered || playback.interacting || activeFocus
                            || appMenu.visible))
                    height: visible ? implicitHeight : 0
                    playing: videoStage.playing
                    position: videoStage.position
                    duration: videoStage.duration
                    seekable: videoStage.seekable
                    muted: videoStage.muted
                    volume: videoStage.volume
                    mediaSource: videoStage.source
                    previewEnabled: playbackPreferences.previewEnabled
                    playlistVisible: window.playlistVisible
                    controlPanelVisible: controlPanel.visible
                    onPlaylistRequested: window.playlistVisible = !window.playlistVisible
                    onSearchRequested: window.focusPlaylistSearch()
                    onSettingsRequested: window.toggleControlPanel()
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
                id: playlistResizeHandle
                objectName: "playlistResizeHandle"
                // Keep this handle above the window drag surface in every window state.
                parent: window.contentItem
                x: playerBody.parent.x + playerBody.x + playlist.x - width / 2
                y: playerBody.parent.y + playerBody.y + playlist.y
                height: playlist.height
                width: 8
                z: 60
                visible: playlist.visible
                hoverEnabled: true
                cursorShape: Qt.SplitHCursor
                preventStealing: true
                HoverHandler {
                    cursorShape: Qt.SplitHCursor
                }
                property real startX
                property real startWidth
                onWheel: (wheel) => wheel.accepted = true
                onPressed: (mouse) => {
                    startX = mapToItem(window.contentItem, mouse.x, mouse.y).x
                    startWidth = playlist.width
                }
                onPositionChanged: (mouse) => {
                    if (pressed) {
                        const currentX = mapToItem(window.contentItem, mouse.x, mouse.y).x
                        playbackPreferences.playlistWidth = Math.max(360,
                            Math.min(playerBody.width - 400, startWidth + startX - currentX))
                    }
                }
            }

            PlaylistPanel {
                shortcutBindings: shortcutStore
                id: playlist
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: window.isFullscreen ? titleBar.height : 0
                z: 1
                visible: window.playlistVisible || (window.isFullscreen
                    && (window.videoDragActive || playlistHover.hovered || editingSearch || menuOpen
                        || draggingTracks || playlistResizeHandle.containsMouse
                        || playlistResizeHandle.pressed))
                width: visible
                    ? Math.max(360, Math.min(playbackPreferences.playlistWidth, parent.width - 400)) : 0
                playlistModel: window.mediaPlaylist
                playlistCollection: playlists
                videoLibrary: library
                onReceivingVideoDragChanged: {
                    if (receivingVideoDrag)
                        window.videoDragActive = true
                    else
                        Qt.callLater(window.updateVideoDrag)
                }
                onTrackRequested: (index) => window.playTrack(index)
                onAddRequested: window.openMedia(false)
                onAddFolderRequested: folderDialog.open()
                onClearRequested: window.clearPlaylist()
                onFileLocationFailed: videoStage.showHint(qsTr("Could not open file location"))
            }

            Item {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: window.isFullscreen ? titleBar.height : 0
                width: playlist.visible ? playlist.width + 16 : 16
                z: 1
                HoverHandler {
                    id: playlistHover
                    enabled: window.isFullscreen
                    blocking: false
                }
            }
        }
    }

    WindowDragArea {
        objectName: "windowDragArea"
        anchors.fill: parent
        surface: window.contentItem
        enabled: !window.isFullscreen
        visible: enabled
        z: 50
        onMoveRequested: window.windowMoveRequested()
    }

    WindowResizeFrame {
        anchors.fill: parent
        enabled: window.visibility === Window.Windowed
        onResizeRequested: (edges) => window.startSystemResize(edges)
    }
}
