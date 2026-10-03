import QtQuick
import QtQuick.Controls.Basic

PlayerMenu {
    id: root
    required property Action openAction
    required property Action closeAction
    required property Action preferencesAction
    required property Action playlistAction
    required property Action controlsAction
    required property Action fullscreenAction
    required property Action fullscreenPlusAction
    required property Action pinAction
    required property Action mediaInfoAction
    required property Action aboutAction
    required property Action exitAction
    required property Action searchAction
    property bool hasMedia: false
    property bool playing: false
    property bool seekable: false
    property bool muted: false
    property int repeatMode: 0
    signal addMediaRequested()
    signal clearPlaylistRequested()
    signal playPauseRequested()
    signal stopRequested()
    signal skipTrackRequested(int offset)
    signal seekRequested(real seconds)
    signal muteRequested()
    signal volumeRequested(int offset)
    signal repeatRequested(int mode)
    signal copyPositionRequested()
    signal windowSizeRequested(bool maximized)

    objectName: "appMenu"
    onOpened: currentIndex = 0

    PlayerMenuItem { objectName: "openMediaMenuItem"; action: root.openAction }
    PlayerMenu {
        objectName: "openSubmenu"
        title: qsTr("開啟")
        PlayerMenuItem { action: root.openAction }
        PlayerMenuItem {
            objectName: "addMediaMenuItem"
            text: qsTr("新增至播放清單…")
            onTriggered: root.addMediaRequested()
        }
        PlayerMenuItem {
            objectName: "clearPlaylistMenuItem"
            text: qsTr("清空播放清單")
            enabled: root.hasMedia
            onTriggered: root.clearPlaylistRequested()
        }
    }
    PlayerMenu {
        title: qsTr("工具列")
        PlayerMenuItem { action: root.controlsAction }
        PlayerMenuItem { action: root.playlistAction }
        PlayerMenuItem { action: root.searchAction }
    }
    PlayerMenu { title: qsTr("我的最愛"); enabled: false }
    PlayerMenuItem { objectName: "closeMediaMenuItem"; action: root.closeAction }
    PlayerMenuSeparator {}
    PlayerMenu {
        objectName: "playbackSubmenu"
        title: qsTr("播放")
        PlayerMenuItem {
            objectName: "playPauseMenuItem"
            text: root.playing ? qsTr("暫停") : qsTr("播放")
            shortcutLabel: "Space"
            onTriggered: root.playPauseRequested()
        }
        PlayerMenuItem {
            text: qsTr("停止")
            enabled: root.hasMedia
            onTriggered: root.stopRequested()
        }
        PlayerMenuItem {
            text: qsTr("上一個檔案")
            enabled: root.hasMedia
            onTriggered: root.skipTrackRequested(-1)
        }
        PlayerMenuItem {
            text: qsTr("下一個檔案")
            enabled: root.hasMedia
            onTriggered: root.skipTrackRequested(1)
        }
        PlayerMenuSeparator {}
        PlayerMenuItem {
            text: qsTr("不重複")
            checkable: true
            checked: root.repeatMode === 0
            onTriggered: root.repeatRequested(0)
        }
        PlayerMenuItem {
            text: qsTr("重複目前檔案")
            checkable: true
            checked: root.repeatMode === 1
            onTriggered: root.repeatRequested(1)
        }
        PlayerMenuItem {
            text: qsTr("重複播放清單")
            checkable: true
            checked: root.repeatMode === 2
            onTriggered: root.repeatRequested(2)
        }
    }
    PlayerMenu { title: qsTr("字幕"); enabled: false }
    PlayerMenu {
        title: qsTr("視訊")
        PlayerMenuItem { action: root.fullscreenAction; shortcutLabel: "Enter" }
        PlayerMenuItem { action: root.pinAction }
    }
    PlayerMenu {
        objectName: "audioSubmenu"
        title: qsTr("音訊")
        PlayerMenuItem {
            objectName: "muteMenuItem"
            text: qsTr("靜音")
            shortcutLabel: "M"
            checkable: true
            checked: root.muted
            onTriggered: root.muteRequested()
        }
        PlayerMenuItem { text: qsTr("提高音量"); onTriggered: root.volumeRequested(5) }
        PlayerMenuItem { text: qsTr("降低音量"); onTriggered: root.volumeRequested(-5) }
    }
    PlayerMenu { title: qsTr("濾鏡"); enabled: false }
    PlayerMenu {
        title: qsTr("面板")
        PlayerMenuItem { action: root.playlistAction }
        PlayerMenuItem { action: root.controlsAction }
        PlayerMenuItem { action: root.searchAction }
    }
    PlayerMenu {
        title: qsTr("配置/語言/其他")
        PlayerMenuItem { action: root.preferencesAction }
        PlayerMenuItem { action: root.pinAction }
        PlayerMenuItem { action: root.aboutAction }
    }
    PlayerMenuSeparator {}
    PlayerMenu {
        title: qsTr("取得位置")
        enabled: root.seekable
        PlayerMenuItem { text: qsTr("複製目前時間"); onTriggered: root.copyPositionRequested() }
        PlayerMenuItem { text: qsTr("回到開頭"); onTriggered: root.seekRequested(0) }
    }
    PlayerMenu { title: qsTr("比例"); enabled: false }
    PlayerMenu {
        title: qsTr("畫面大小")
        PlayerMenuItem { text: qsTr("預設大小"); onTriggered: root.windowSizeRequested(false) }
        PlayerMenuItem { text: qsTr("最大化"); onTriggered: root.windowSizeRequested(true) }
    }
    PlayerMenuItem {
        objectName: "fullscreenMenuItem"
        action: root.fullscreenAction
        shortcutLabel: "Enter"
    }
    PlayerMenuItem { action: root.fullscreenPlusAction }
    PlayerMenuSeparator {}
    PlayerMenuItem { action: root.preferencesAction }
    PlayerMenuItem { objectName: "playlistMenuItem"; action: root.playlistAction }
    PlayerMenuItem { objectName: "controlsMenuItem"; action: root.controlsAction }
    PlayerMenuItem { action: root.mediaInfoAction }
    PlayerMenuItem { action: root.aboutAction }
    PlayerMenuSeparator {}
    PlayerMenuItem { action: root.exitAction }
}
