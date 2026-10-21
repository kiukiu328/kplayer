pragma ComponentBehavior: Bound
import QtQuick
import "SubtitleRules.js" as SubtitleRules

PlayerMenu {
    id: root
    required property var player
    required property var shortcutBindings
    function shortcutLabel(command: string): string {
        const entries = shortcutBindings.entries
        return shortcutBindings.label(command)
    }
    property string preferredLanguages: ""
    property int preferredTrackNumber: 0
    property string preferredTrackName: ""
    readonly property var currentTrack: player.subtitleTracks.find(
        track => track.id === player.subtitleTrack)
    readonly property string trackLanguage: SubtitleRules.valueForTrack(currentTrack, "language")
    readonly property string trackName: SubtitleRules.trackName(currentTrack)
    readonly property bool usableLanguage: trackLanguage.length > 0
    signal addRequested()
    signal addCurrentRuleRequested(string kind)
    signal preferLanguageRequested(string language)
    signal preferTrackNumberRequested(int track)
    signal preferTrackNameRequested(string name)
    signal automaticPreferenceRequested()
    title: qsTr("字幕")

    PlayerMenuItem {
        objectName: "subtitleVisibilityMenuItem"
        text: qsTr("顯示字幕")
        shortcutLabel: root.shortcutLabel("subtitlesVisible")
        checkable: true
        checked: root.player.subtitlesVisible
        onTriggered: root.player.subtitlesVisible = !root.player.subtitlesVisible
    }
    PlayerMenuItem {
        objectName: "addSubtitleMenuItem"
        text: qsTr("新增字幕…")
        shortcutLabel: root.shortcutLabel("openSubtitles")
        enabled: root.player.mediaLoaded
        onTriggered: root.addRequested()
    }
    PlayerMenuItem {
        text: qsTr("切換字幕")
        shortcutLabel: root.shortcutLabel("cycleSubtitle")
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
    PlayerMenuSeparator {}
    PlayerMenu {
        objectName: "subtitlePreferenceMenu"
        title: qsTr("選擇字幕依據")
        enabled: root.player.mediaLoaded

        PlayerMenuItem {
            objectName: "subtitlePreferenceAutomaticMenuItem"
            text: qsTr("自動")
            checkable: true
            radioIndicator: true
            checked: root.preferredLanguages.length === 0
                && root.preferredTrackNumber === 0 && root.preferredTrackName.length === 0
            onTriggered: root.automaticPreferenceRequested()
        }
        PlayerMenuSeparator {}
        PlayerMenuItem {
            objectName: "subtitlePreferenceLanguageMenuItem"
            text: root.usableLanguage ? qsTr("目前語言：%1").arg(root.trackLanguage)
                                      : qsTr("目前語言：未知")
            enabled: root.usableLanguage
            checkable: true
            radioIndicator: true
            checked: root.preferredLanguages.length > 0
                && root.preferredTrackNumber === 0 && root.preferredTrackName.length === 0
            onTriggered: root.preferLanguageRequested(root.trackLanguage)
        }
        PlayerMenuItem {
            objectName: "subtitlePreferenceTrackNumberMenuItem"
            text: qsTr("目前字幕軌編號：%1").arg(root.player.subtitleTrack)
            enabled: root.currentTrack !== undefined
            checkable: true
            radioIndicator: true
            checked: root.preferredTrackNumber > 0
                && root.preferredLanguages.length === 0 && root.preferredTrackName.length === 0
            onTriggered: root.preferTrackNumberRequested(root.player.subtitleTrack)
        }
        PlayerMenuItem {
            objectName: "subtitlePreferenceTrackNameMenuItem"
            text: root.trackName.length > 0 ? qsTr("目前字幕軌名稱：%1").arg(root.trackName)
                                            : qsTr("目前字幕軌名稱：未知")
            enabled: root.trackName.length > 0
            checkable: true
            radioIndicator: true
            checked: root.preferredTrackName.length > 0
                && root.preferredLanguages.length === 0 && root.preferredTrackNumber === 0
            onTriggered: root.preferTrackNameRequested(root.trackName)
        }
    }
    PlayerMenuItem {
        objectName: "addCurrentSubtitleRuleMenuItem"
        text: qsTr("Add current subtitle to rules…")
        enabled: root.player.mediaLoaded && root.currentTrack !== undefined
        onTriggered: root.addCurrentRuleRequested("")
    }
    PlayerMenuItem {
        objectName: "addCurrentSubtitleLanguageRuleMenuItem"
        text: qsTr("Add current language to rules…")
        enabled: root.player.mediaLoaded && root.usableLanguage
        onTriggered: root.addCurrentRuleRequested("language")
    }
}
