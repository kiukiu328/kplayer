import QtQuick

Rectangle {
    color: Theme.videoBackground
    Row {
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
}
