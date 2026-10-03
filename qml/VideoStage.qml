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
    signal moveRequested()
    signal fullscreenRequested()
    signal playPauseRequested()
    signal endOfFile()

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

    DragHandler {
        target: null
        acceptedButtons: Qt.LeftButton
        onActiveChanged: if (active) root.moveRequested()
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        onClicked: (mouse) => {
            root.forceActiveFocus()
            if (mouse.button === Qt.MiddleButton)
                middleClick.restart()
        }
        onDoubleClicked: (mouse) => {
            root.forceActiveFocus()
            mouse.accepted = true
            middleClick.stop()
            root.playPauseRequested()
        }
    }
    Timer {
        id: middleClick
        // Wait for the OS double-click interval before treating a click as single.
        // Qt exposes styleHints as QObject to qmllint, but this is a QStyleHints property.
        // qmllint disable missing-property
        interval: Qt.styleHints.mouseDoubleClickInterval
        // qmllint enable missing-property
        onTriggered: root.fullscreenRequested()
    }
}
