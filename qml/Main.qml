import QtQuick
import QtQuick.Dialogs

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
    property url mediaSource
    property int visibilityBeforeFullscreen: Window.Windowed

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
            mediaDialog.open()
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

    PlaylistModel { id: mediaPlaylist; objectName: "playlistModel" }
    Component.onCompleted: {
        if (mediaSource.toString().length > 0)
            mediaPlaylist.appendUrls([mediaSource])
    }

    FileDialog {
        id: mediaDialog
        title: qsTr("Open media")
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            const firstIndex = mediaPlaylist.count
            if (mediaPlaylist.appendUrls(selectedFiles) > 0)
                window.playTrack(firstIndex)
        }
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
                anchors.rightMargin: Theme.panelDivider

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
                    onEndOfFile: window.skipTrack(1)
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
                    onOpenRequested: mediaDialog.open()
                }
            }

            PlaylistPanel {
                id: playlist
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Math.min(Theme.playlistWidth, parent.width * Theme.playlistRatio)
                playlistModel: mediaPlaylist
                onTrackRequested: (index) => window.playTrack(index)
            }
        }
    }
}
