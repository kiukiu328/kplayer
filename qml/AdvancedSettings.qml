import QtQuick
import QtQuick.Controls.Basic

Dialog {
    id: root
    property string subtitleLanguage: ""
    property real maximumSpeed: 16
    property real playbackSpeed: 1
    signal speedSaved(real speed, real maximum)
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
        speedLimit.value = Math.round(maximumSpeed * 10)
        customSpeed.value = Math.round(playbackSpeed * 10)
        interfaceLanguage.currentIndex = AppLanguage.language === "en" ? 1 : 0
        languageCodes.text = subtitleLanguage
        languageChoice.currentIndex = 0
        for (let i = 0; i < languageChoice.model.length; ++i) {
            if (languageChoice.model[i].code === subtitleLanguage) {
                languageChoice.currentIndex = i
                break
            }
        }
    }
    onAccepted: {
        speedSaved(customSpeed.value / 10, speedLimit.value / 10)
        AppLanguage.language = interfaceLanguage.currentIndex === 1 ? "en" : "zh_TW"
        languageSaved(languageCodes.text.trim().replace(/\s+/g, "").toLowerCase())
    }

    contentItem: ScrollView {
        id: settingsScroll
        implicitHeight: settingsColumn.implicitHeight
        contentWidth: availableWidth
        clip: true
        Column {
            id: settingsColumn
            width: settingsScroll.availableWidth
            spacing: 12
            Label { text: qsTr("最高播放速度 (×)"); color: Theme.textPrimary }
            SpinBox {
                id: speedLimit
                objectName: "maximumSpeedInput"
                width: parent.width
                from: 10
                to: 1000
                stepSize: 10
                editable: true
                validator: DoubleValidator { bottom: 1; top: 100; decimals: 1; locale: speedLimit.locale.name }
                textFromValue: (value, locale) => Number(value / 10).toLocaleString(locale, 'f', 1)
                valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * 10)
                Accessible.name: qsTr("最高播放速度 (×)")
            }
            Label { text: qsTr("播放速度 (×)"); color: Theme.textPrimary }
            SpinBox {
                id: customSpeed
                objectName: "customSpeedInput"
                width: parent.width
                from: 2
                to: speedLimit.value
                stepSize: 1
                editable: true
                validator: DoubleValidator { bottom: 0.2; top: speedLimit.value / 10; decimals: 1; locale: customSpeed.locale.name }
                textFromValue: (value, locale) => Number(value / 10).toLocaleString(locale, 'f', 1)
                valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * 10)
                Accessible.name: qsTr("播放速度 (×)")
            }
            Label {
                text: qsTr("介面語言")
                color: Theme.textPrimary
            }
            ComboBox {
                id: interfaceLanguage
                objectName: "interfaceLanguageChoice"
                width: parent.width
                model: ["繁體中文", "English"]
                Accessible.name: qsTr("介面語言")
            }
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
}
