import QtQuick

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

    // Visual shell only: media and window actions are intentionally unconnected.
    Item {
        anchors.fill: parent
        anchors.margins: Theme.windowBorder

        TitleBar {
            id: titleBar
            anchors.top: parent.top
            width: parent.width
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
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: playback.top
                }
                PlaybackBar {
                    id: playback
                    anchors.bottom: parent.bottom
                    width: parent.width
                }
            }

            PlaylistPanel {
                id: playlist
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Math.min(Theme.playlistWidth, parent.width * Theme.playlistRatio)
            }
        }
    }
}
