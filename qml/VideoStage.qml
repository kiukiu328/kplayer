import QtQuick

Rectangle {
    id: root
    objectName: "videoStage"
    property alias source: video.source
    property alias playing: video.playing
    property alias stopped: video.stopped
    property alias position: video.position
    property alias duration: video.duration
    property alias seekable: video.seekable
    property alias muted: video.muted
    property alias volume: video.volume
    property alias maximumSpeed: video.maximumSpeed
    property alias speed: video.speed
    property alias aspectRatio: video.aspectRatio
    property alias brightness: video.brightness
    property alias contrast: video.contrast
    property alias saturation: video.saturation
    property alias hue: video.hue
    property alias equalizerEnabled: video.equalizerEnabled
    property alias equalizerBands: video.equalizerBands
    property alias preamp: video.preamp
    property alias soften: video.soften
    property alias sharpen: video.sharpen
    property alias deblock: video.deblock
    property alias subtitleFont: video.subtitleFont
    property alias subtitleFontSize: video.subtitleFontSize
    property alias subtitlePosition: video.subtitlePosition
    property alias subtitleAlignment: video.subtitleAlignment
    property alias subtitleDelay: video.subtitleDelay
    property alias loopA: video.loopA
    property alias loopB: video.loopB
    property alias defaultSubtitleLanguage: video.defaultSubtitleLanguage
    property alias subtitlesVisible: video.subtitlesVisible
    property alias subtitleTracks: video.subtitleTracks
    property alias subtitleTrack: video.subtitleTrack
    property alias mediaLoaded: video.mediaLoaded
    property bool hintsReady: false
    Component.onCompleted: hintsReady = true
    function showHint(message: string) {
        if (!hintsReady)
            return
        hint.text = message
        hintTimer.restart()
    }
    function addSubtitle(url: url): bool { return video.addSubtitle(url) }
    function cycleSubtitle() { video.cycleSubtitle() }
    function toggleSpeed() { video.toggleSpeed() }
    function resetVideoAdjustments() { video.resetVideoAdjustments() }
    function stepFrame(forward: bool) { video.stepFrame(forward) }
    function setLoopPoint(end: bool, seconds: real) { video.setLoopPoint(end, seconds) }
    function clearLoop() { video.clearLoop() }
    function captureFrame(file: url) { video.captureFrame(file) }
    signal moveRequested()
    signal fullscreenRequested()
    signal playPauseRequested()
    signal endOfFile()
    signal contextMenuRequested(real x, real y)

    function togglePause() { video.togglePause() }
    function play() { video.play() }
    function stop() { video.stop() }
    function seek(seconds: real) { video.seek(seconds) }
    function seekRelative(seconds: real) { video.seekRelative(seconds) }

    color: Theme.videoBackground
    MpvVideo {
        id: video
        anchors.fill: parent
        onEndOfFile: root.endOfFile()
        onFrameCaptured: (file) => root.showHint(qsTr("Frame saved: %1").arg(file.toString()))
        onSpeedChanged: root.showHint(speed.toFixed(1) + "x")
        onAspectRatioChanged: root.showHint(aspectRatio === "no"
            ? qsTr("Original aspect ratio") : qsTr("Aspect ratio: %1").arg(aspectRatio))
        onVolumeChanged: root.showHint(qsTr("Volume: %1%").arg(Math.round(volume)))
        onMutedChanged: root.showHint(muted ? qsTr("Muted") : qsTr("Unmuted"))
        onSubtitlesVisibleChanged: root.showHint(subtitlesVisible
            ? qsTr("Subtitles on") : qsTr("Subtitles off"))
        onSubtitleTrackChanged: {
            const track = subtitleTracks.find(track => track.id === subtitleTrack)
            root.showHint(track ? qsTr("Subtitle: %1").arg(track.label) : qsTr("No subtitles"))
        }
    }
    Connections {
        target: AppLanguage
        function onLanguageChanged() { video.retranslateSubtitles() }
    }
    Row {
        visible: video.stopped
        anchors.centerIn: parent
        spacing: 12
        Rectangle {
            width: Theme.watermarkSize
            height: Theme.watermarkSize
            radius: width / 2
            color: Theme.watermark
            PlayerIcon {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: 3
                width: 38
                height: 38
                name: "play"
                color: Theme.videoBackground
            }
        }
        Text {
            height: Theme.watermarkSize
            verticalAlignment: Text.AlignVCenter
            text: "KPlayer"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.watermarkFontSize
            color: Theme.watermark
        }
    }

    Column {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 16
        width: Math.max(0, parent.width - 32)
        spacing: 8
        Text {
            id: hint
            objectName: "playbackHint"
            width: parent.width
            wrapMode: Text.Wrap
            visible: hintTimer.running
            color: Theme.hintText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            style: Text.Outline
            styleColor: Theme.videoBackground
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: video.errorString
            visible: text.length > 0
            color: Theme.hintText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            style: Text.Outline
            styleColor: Theme.videoBackground
        }
    }
    Timer { id: hintTimer; interval: 2000 }

    DragHandler {
        target: null
        acceptedButtons: Qt.LeftButton
        onActiveChanged: if (active) root.moveRequested()
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        onClicked: (mouse) => {
            root.forceActiveFocus()
            if (mouse.button === Qt.RightButton) {
                root.contextMenuRequested(mouse.x, mouse.y)
                return
            }
            if (mouse.button === Qt.MiddleButton)
                root.fullscreenRequested()
        }
        onDoubleClicked: (mouse) => {
            mouse.accepted = true
            // Middle clicks switch fullscreen immediately; their second click
            // must not undo that switch or also change playback.
            if (mouse.button !== Qt.LeftButton)
                return
            root.forceActiveFocus()
            root.playPauseRequested()
        }
    }
}
