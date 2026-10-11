pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: root
    property bool playing: false
    property real position: 0
    property real duration: 0
    property bool seekable: false
    property bool muted: false
    property real volume: 100
    property url mediaSource
    property bool previewEnabled: true
    property bool playlistVisible: true
    property bool controlPanelVisible: false
    readonly property bool interacting: positionSlider.pressed || volumeSlider.pressed
    readonly property bool hovered: barHover.hovered
    HoverHandler { id: barHover; blocking: false }
    readonly property bool compact: width < 5 * Theme.transportButtonWidth
        + Theme.toolButtonWidth + 56 + Theme.transportButtonWidth + timestamps.width + 30
    signal playPauseRequested()
    signal stopRequested()
    signal openRequested()
    signal muteRequested()
    signal volumeRequested(real value)
    signal seekRequested(real seconds)
    signal seekRelativeRequested(real seconds)
    signal previousRequested()
    signal nextRequested()
    signal playlistRequested()
    signal searchRequested()
    signal settingsRequested()

    function formatTime(seconds: real): string {
        const total = Math.floor(Math.max(0, seconds))
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor(total / 60) % 60
        return hours.toString().padStart(2, "0") + ":"
            + minutes.toString().padStart(2, "0") + ":"
            + (total % 60).toString().padStart(2, "0")
    }
    implicitHeight: Theme.seekHeight + Theme.transportHeight + (compact ? Theme.seekHeight : 0)
    height: implicitHeight
    color: Theme.chromeBackground
    Rectangle {
        width: parent.width
        height: Theme.dividerWidth
        color: Theme.border
    }
    Item {
        id: seekBar
        width: parent.width
        height: Theme.seekHeight
        PlayerSlider {
            id: positionSlider
            objectName: "positionSlider"
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: volume.left
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height
            from: 0
            to: Math.max(1, root.duration)
            stepSize: 1
            enabled: root.seekable && root.duration > 0
            // Commit one exact seek on release; playback updates never fight a drag.
            live: false
            backendValue: root.position
            previewEnabled: root.previewEnabled
            onMoved: {
                if (!pressed)
                    root.seekRequested(value)
            }
            onPressedChanged: {
                if (!pressed)
                    root.seekRequested(value)
            }
            tooltipText: qsTr("Seek: %1 / %2")
                .arg(root.formatTime(pressed ? valueAt(position) : value))
                .arg(root.formatTime(root.duration))
            Accessible.name: qsTr("Playback position")
        }
        Rectangle {
            id: timelinePreview
            objectName: "timelinePreview"
            visible: positionSlider.previewVisible
            x: Math.max(8, Math.min(root.width - width - 8,
                positionSlider.x + positionSlider.previewX - width / 2))
            y: -height - 8
            width: 224
            height: 156
            radius: Theme.controlRadius
            color: Theme.chromeBackground
            border.width: 1
            border.color: Theme.iconMuted
            z: 10

            MpvPreview {
                id: previewVideo
                objectName: "previewVideo"
                x: 4
                y: 4
                width: parent.width - 8
                height: 122
                source: root.mediaSource
                active: timelinePreview.visible
                position: positionSlider.previewTime
            }
            Rectangle {
                x: previewVideo.x
                y: previewVideo.y
                width: previewVideo.width
                height: previewVideo.height
                visible: !previewVideo.ready
                color: Theme.videoBackground
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 16
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: previewVideo.errorString.length > 0
                        ? qsTr("Preview unavailable") : qsTr("Loading preview…")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallFontSize
                    color: Theme.textMuted
                }
            }
            Text {
                objectName: "previewTimestamp"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 5
                text: root.formatTime(positionSlider.previewTime)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFontSize
                color: Theme.textPrimary
            }
        }
        Item {
            id: volume
            anchors.right: parent.right
            anchors.rightMargin: 16
            height: parent.height
            width: 132
            IconButton {
                objectName: "muteButton"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 26
                height: parent.height
                iconSize: 20
                iconName: root.muted || root.volume === 0 ? "volumeMuted" : "volume"
                iconColor: root.muted || root.volume === 0 ? Theme.iconMuted : Theme.iconPrimary
                label: root.muted ? qsTr("Unmute audio") : qsTr("Mute audio")
                onClicked: root.muteRequested()
            }
            PlayerSlider {
                id: volumeSlider
                objectName: "volumeSlider"
                anchors.left: parent.left
                anchors.leftMargin: 28
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: parent.height
                from: 0
                to: 100
                stepSize: 1
                backendValue: root.volume
                onMoved: root.volumeRequested(value)
                tooltipText: qsTr("Volume: %1%2").arg(Math.round(value))
                    .arg(root.muted ? qsTr("% (muted)") : "%")
                Accessible.name: qsTr("Volume")
            }
        }
    }
    Rectangle {
        anchors.top: seekBar.bottom
        width: parent.width
        height: Theme.dividerWidth
        color: Theme.border
    }
    Row {
        id: transport
        objectName: "transportButtons"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        height: Theme.transportHeight - Theme.dividerWidth
        Repeater {
            model: [
                { icon: root.playing ? "pause" : "play",
                  label: root.playing ? qsTr("Pause") : qsTr("Play") },
                { icon: "stop", label: qsTr("Stop") },
                { icon: "previous", label: qsTr("Rewind 10 seconds") },
                { icon: "next", label: qsTr("Fast forward 10 seconds") },
                { icon: "eject", label: qsTr("Open media") }
            ]
            IconButton {
                required property var modelData
                required property int index
                width: root.compact ? 40 : Theme.transportButtonWidth
                height: transport.height
                iconName: modelData.icon
                objectName: modelData.icon + "Button"
                label: modelData.label
                iconSize: 20
                rightDivider: true
                acceptsRightClick: index === 2 || index === 3
                tooltipText: index === 2
                    ? qsTr("Left click: rewind 10 seconds\nRight click: previous track")
                    : index === 3
                        ? qsTr("Left click: fast forward 10 seconds\nRight click: next track")
                        : label
                onClicked: {
                    switch (index) {
                    case 0: root.playPauseRequested(); break;
                    case 1: root.stopRequested(); break;
                    case 2: root.seekRelativeRequested(-10); break;
                    case 3: root.seekRelativeRequested(10); break;
                    case 4: root.openRequested(); break;
                    }
                }
                onRightClicked: {
                    if (index === 2)
                        root.previousRequested()
                    else if (index === 3)
                        root.nextRequested()
                }
            }
        }
    }
    Row {
        id: timestamps
        objectName: "playbackTimestamps"
        x: root.compact ? (root.width - width) / 2 : transport.width + 15
        y: root.compact ? seekBar.height + (Theme.seekHeight - height) / 2
            : transport.y + (transport.height - height) / 2
        spacing: 8
        Text {
            text: root.formatTime(root.position)
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            color: Theme.textPrimary
        }
        Text {
            text: "/"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            color: Theme.textMuted
        }
        Text {
            text: root.formatTime(root.duration)
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            color: Theme.textMuted
        }
    }
    Row {
        id: tools
        objectName: "playbackTools"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: transport.height
        IconButton {
            objectName: "searchMediaButton"
            height: tools.height
            width: root.compact ? 40 : Theme.toolButtonWidth
            iconName: "searchList"
            label: qsTr("Search media")
            iconColor: Theme.accent
            iconSize: 26
            onClicked: root.searchRequested()
        }
        IconButton {
            height: tools.height
            width: root.compact ? 40 : 56
            objectName: "settingsButton"
            iconName: "settings"
            label: root.controlPanelVisible ? qsTr("Hide control panel") : qsTr("Show control panel")
            iconColor: root.controlPanelVisible ? Theme.accent : Theme.iconMuted
            backgroundColor: root.controlPanelVisible ? Theme.chromeHover : Theme.transparent
            rightDivider: true
            onClicked: root.settingsRequested()
        }
        IconButton {
            objectName: "playbackMenuButton"
            height: tools.height
            width: root.compact ? 40 : Theme.transportButtonWidth
            iconName: "menu"
            label: root.playlistVisible ? qsTr("Hide playlist") : qsTr("Show playlist")
            iconColor: root.playlistVisible ? Theme.accent : Theme.iconMuted
            backgroundColor: root.playlistVisible ? Theme.chromeHover : Theme.transparent
            iconSize: 24
            onClicked: root.playlistRequested()
        }
    }
}
