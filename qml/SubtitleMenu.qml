import QtQuick
import QtQuick.Controls.Basic

PlayerMenu {
    id: root
    required property var player
    signal addRequested()
    title: qsTr("字幕")

    PlayerMenuItem {
        objectName: "subtitleVisibilityMenuItem"
        text: qsTr("顯示字幕")
        shortcutLabel: "Alt+H"
        checkable: true
        checked: root.player.subtitlesVisible
        onTriggered: root.player.subtitlesVisible = !root.player.subtitlesVisible
    }
    PlayerMenuItem {
        objectName: "addSubtitleMenuItem"
        text: qsTr("新增字幕…")
        shortcutLabel: "Alt+O"
        enabled: root.player.mediaLoaded
        onTriggered: root.addRequested()
    }
    PlayerMenuItem {
        text: qsTr("切換字幕")
        shortcutLabel: "Alt+L"
        enabled: root.player.mediaLoaded && root.player.subtitleTracks.length > 0
        onTriggered: root.player.cycleSubtitle()
    }
    PlayerMenuSeparator {}
    PlayerMenuItem {
        objectName: "noSubtitleMenuItem"
        text: qsTr("不選擇字幕")
        enabled: root.player.mediaLoaded
        checkable: true
        radioIndicator: true
        checked: root.player.subtitleTrack < 0
        onTriggered: root.player.subtitleTrack = -1
    }
    Instantiator {
        model: root.player.subtitleTracks
        delegate: PlayerMenuItem {
            required property var modelData
            objectName: "subtitleTrackMenuItem" + modelData.id
            text: modelData.label
            enabled: root.player.mediaLoaded
            checkable: true
            radioIndicator: true
            checked: root.player.subtitleTrack === modelData.id
            onTriggered: root.player.subtitleTrack = modelData.id
        }
        onObjectAdded: (index, object) => root.insertItem(5 + index, object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
}
