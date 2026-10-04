import QtQuick
import QtQuick.Controls.Basic

Dialog {
    id: root
    property string subtitleLanguage: ""
    signal languageSaved(string language)
    title: qsTr("進階設定")
    modal: true
    focus: true
    standardButtons: Dialog.Save | Dialog.Cancel
    closePolicy: Popup.CloseOnEscape
    font.family: Theme.fontFamily
    palette.window: Theme.chromeBackground
    palette.windowText: Theme.textPrimary
    palette.text: Theme.textPrimary
    palette.base: Theme.searchBackground
    palette.button: Theme.buttonBackground
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.hintText
    palette.highlightedText: Theme.textPrimary

    onOpened: {
        languageCodes.text = subtitleLanguage
        languageChoice.currentIndex = 0
        for (let i = 0; i < languageChoice.model.length; ++i) {
            if (languageChoice.model[i].code === subtitleLanguage) {
                languageChoice.currentIndex = i
                break
            }
        }
    }
    onAccepted: languageSaved(languageCodes.text.trim().replace(/\s+/g, "").toLowerCase())

    contentItem: Column {
        spacing: 12
        Label {
            text: qsTr("預設字幕語言")
            color: Theme.textPrimary
            font.pixelSize: Theme.smallFontSize
        }
        ComboBox {
            id: languageChoice
            objectName: "subtitleLanguageChoice"
            width: parent.width
            textRole: "label"
            model: [
                { label: qsTr("自動 / 自訂"), code: "" },
                { label: qsTr("中文"), code: "zh,zho,chi" },
                { label: "English", code: "en,eng" },
                { label: "日本語", code: "ja,jpn" },
                { label: "한국어", code: "ko,kor" },
                { label: "Français", code: "fr,fra,fre" },
                { label: "Deutsch", code: "de,deu,ger" },
                { label: "Español", code: "es,spa" },
                { label: "Português", code: "pt,por" },
                { label: "Italiano", code: "it,ita" }
            ]
            onActivated: languageCodes.text = model[currentIndex].code
        }
        TextField {
            id: languageCodes
            objectName: "subtitleLanguageCodes"
            width: parent.width
            placeholderText: qsTr("語言代碼，例如 zh,en 或 jpn")
            selectByMouse: true
            Accessible.name: qsTr("預設字幕語言代碼")
        }
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            text: qsTr("選擇語言或輸入語言代碼，以逗號分隔優先順序。留空使用自動選擇。儲存後會在下次開啟影片時套用。")
        }
    }
}
