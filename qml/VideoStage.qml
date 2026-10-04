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
    property alias speed: video.speed
    property alias subtitlesVisible: video.subtitlesVisible
    property alias subtitleTracks: video.subtitleTracks
    property alias subtitleTrack: video.subtitleTrack
    property alias mediaLoaded: video.mediaLoaded
    function addSubtitle(url: url): bool { return video.addSubtitle(url) }
    function cycleSubtitle() { video.cycleSubtitle() }
    function stepFrame(forward: bool) { video.stepFrame(forward) }
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

    Text {
        anchors.centerIn: parent
        width: parent.width - 40
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: video.errorString
        visible: text.length > 0
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Text {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        text: video.speed.toFixed(1) + "×"
        visible: video.speed !== 1.0
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        style: Text.Outline
        styleColor: "black"
    }

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
