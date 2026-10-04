pragma ComponentBehavior: Bound
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
    required property Action pinAction
    required property Action mediaInfoAction
    required property Action aboutAction
    required property Action exitAction
    required property Action searchAction
    required property Action findVideosAction
    required property Action findFolderVideosAction
    required property Action stopVideoSearchAction
    required property Action associateVideosAction
    required property var player
    property string preferredSubtitleLanguages: ""
    signal preferSubtitleLanguageRequested(string language)
    required property Favorites favorites
    signal favoriteRequested(url source)
    signal addSubtitleRequested()
    signal videoAdjustmentsRequested()
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
        PlayerMenuItem { objectName: "findVideosMenuItem"; action: root.findVideosAction }
        PlayerMenuItem { objectName: "findFolderVideosMenuItem"; action: root.findFolderVideosAction }
        PlayerMenuItem { objectName: "stopVideoSearchMenuItem"; action: root.stopVideoSearchAction }
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
    PlayerMenu {
        id: favoritesMenu
        objectName: "favoritesMenu"
        title: qsTr("我的最愛")
        PlayerMenuItem {
            objectName: "toggleFavoriteMenuItem"
            text: qsTr("目前檔案加入我的最愛")
            enabled: root.hasMedia
            checkable: true
            checked: root.favorites.entries.length > 0 && root.favorites.contains(root.player.source)
            onTriggered: root.favorites.toggle(root.player.source)
        }
        PlayerMenuItem {
            objectName: "clearFavoritesMenuItem"
            text: qsTr("清空我的最愛")
            enabled: root.favorites.entries.length > 0
            onTriggered: root.favorites.clear()
        }
        PlayerMenuSeparator {}
        Instantiator {
            model: root.favorites.entries
            delegate: PlayerMenuItem {
                required property var modelData
                required property int index
                objectName: "favoriteMenuItem" + index
                text: modelData.title
                onTriggered: root.favoriteRequested(modelData.source)
            }
            onObjectAdded: (index, object) => favoritesMenu.insertItem(3 + index, object)
            onObjectRemoved: (index, object) => favoritesMenu.removeItem(object)
        }
    }
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
            shortcutLabel: "PgUp"
            enabled: root.hasMedia
            onTriggered: root.skipTrackRequested(-1)
        }
        PlayerMenuItem {
            text: qsTr("下一個檔案")
            shortcutLabel: "PgDown"
            enabled: root.hasMedia
            onTriggered: root.skipTrackRequested(1)
        }
        PlayerMenuSeparator {}
        PlayerMenu {
            title: qsTr("播放速度") + " (" + root.player.speed.toFixed(1) + "×)"
            PlayerMenuItem {
                text: qsTr("切換正常 / 上次速度")
                shortcutLabel: "Z"
                onTriggered: root.player.toggleSpeed()
            }
            PlayerMenuItem {
                text: qsTr("減速")
                shortcutLabel: "X"
                onTriggered: root.player.speed = Math.round((root.player.speed - 0.1) * 10) / 10
            }
            PlayerMenuItem {
                text: qsTr("加速")
                shortcutLabel: "C"
                onTriggered: root.player.speed = Math.round((root.player.speed + 0.1) * 10) / 10
            }
            PlayerMenuItem {
                text: qsTr("自訂播放速度…")
                onTriggered: root.preferencesAction.trigger()
            }
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
    SubtitleMenu {
        objectName: "subtitleSubmenu"
        player: root.player
        preferredLanguages: root.preferredSubtitleLanguages
        onPreferLanguageRequested: (language) => root.preferSubtitleLanguageRequested(language)
        onAddRequested: root.addSubtitleRequested()
    }
    PlayerMenu {
        title: qsTr("視訊")
        PlayerMenuItem { action: root.fullscreenAction; shortcutLabel: "Return" }
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
    PlayerMenu {
        title: qsTr("濾鏡")
        PlayerMenuItem {
            objectName: "videoAdjustmentsMenuItem"
            text: qsTr("影像調整…")
            onTriggered: root.videoAdjustmentsRequested()
        }
        PlayerMenuItem {
            text: qsTr("重設影像調整")
            onTriggered: root.player.resetVideoAdjustments()
        }
    }
    PlayerMenu {
        title: qsTr("面板")
        PlayerMenuItem { action: root.playlistAction }
        PlayerMenuItem { action: root.controlsAction }
        PlayerMenuItem { action: root.searchAction }
    }
    PlayerMenu {
        title: qsTr("配置/語言/其他")
        PlayerMenuItem { action: root.preferencesAction }
        PlayerMenuItem { objectName: "associateVideosMenuItem"; action: root.associateVideosAction }
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
    PlayerMenu {
        id: aspectMenu
        objectName: "aspectRatioMenu"
        title: qsTr("比例")
        Instantiator {
            model: ["no", "4:3", "16:9", "16:10", "1:1", "2.35:1"]
            delegate: PlayerMenuItem {
                required property string modelData
                objectName: "aspectRatio_" + modelData
                text: modelData === "no" ? qsTr("原始比例") : modelData
                checkable: true
                radioIndicator: true
                checked: root.player.aspectRatio === modelData
                onTriggered: root.player.aspectRatio = modelData
            }
            onObjectAdded: (index, object) => aspectMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => aspectMenu.removeItem(object)
        }
    }
    PlayerMenu {
        title: qsTr("畫面大小")
        PlayerMenuItem { text: qsTr("預設大小"); onTriggered: root.windowSizeRequested(false) }
        PlayerMenuItem { text: qsTr("最大化"); onTriggered: root.windowSizeRequested(true) }
    }
    PlayerMenuItem {
        objectName: "fullscreenMenuItem"
        action: root.fullscreenAction
        shortcutLabel: "Return"
    }
    PlayerMenuSeparator {}
    PlayerMenuItem { action: root.preferencesAction }
    PlayerMenuItem { objectName: "playlistMenuItem"; action: root.playlistAction }
    PlayerMenuItem { objectName: "controlsMenuItem"; action: root.controlsAction }
    PlayerMenuItem { action: root.mediaInfoAction }
    PlayerMenuItem { action: root.aboutAction }
    PlayerMenuSeparator {}
    PlayerMenuItem { action: root.exitAction }
}
