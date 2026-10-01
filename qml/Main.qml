import QtQuick

Window {
    id: window
    required property url videoSource

    width: 960
    height: 540
    visible: true
    title: "KPlayer"
    color: "black"

    MpvVideo {
        id: video
        anchors.fill: parent
        source: window.videoSource
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 48
        text: video.errorString
        visible: text.length > 0
        color: "white"
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }
}
