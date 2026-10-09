pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: root
    property string subtitleLanguage: ""
    property real maximumSpeed: 16
    property real playbackSpeed: 1
    signal speedSaved(real speed, real maximum)
    signal languageSaved(string language)
    property int currentPage: 0
    property point dragOffset: Qt.point(0, 0)
    x: Math.max(0, Math.min(parent.width - width,
                           (parent.width - width) / 2 + dragOffset.x))
    y: Math.max(0, Math.min(parent.height - height,
                           (parent.height - height) / 2 + dragOffset.y))
    onAboutToShow: dragOffset = Qt.point(0, 0)
    readonly property var pageTitles: [qsTr("一般"), qsTr("播放"), qsTr("字幕")]
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
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.chromeBackground

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

    padding: 0
    background: Rectangle {
        color: Theme.chromeBackground
        border.color: Theme.buttonBackground
        radius: 12
    }
    header: Item {
        implicitHeight: 60
        MouseArea {
            objectName: "settingsDragHandle"
            anchors.fill: parent
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            preventStealing: true
            property point pressPosition
            property point startPosition
            onPressed: (mouse) => {
                pressPosition = mapToItem(root.parent, mouse.x, mouse.y)
                startPosition = Qt.point(root.x, root.y)
            }
            onPositionChanged: (mouse) => {
                if (pressed) {
                    const position = mapToItem(root.parent, mouse.x, mouse.y)
                    root.dragOffset = Qt.point(
                        startPosition.x + position.x - pressPosition.x
                            - (root.parent.width - root.width) / 2,
                        startPosition.y + position.y - pressPosition.y
                            - (root.parent.height - root.height) / 2)
                }
            }
        }
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: Theme.textPrimary
            font.pixelSize: 20
            font.bold: true
        }
    }

    component SettingsCard: Rectangle {
        id: card
        default property alias contents: cardColumn.data
        property string heading
        implicitHeight: cardColumn.implicitHeight + 40
        color: Theme.tabBackground
        border.color: Theme.buttonBackground
        radius: 12
        Column {
            id: cardColumn
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
            spacing: 14
            Label {
                text: card.heading
                color: Theme.textPrimary
                font.pixelSize: 18
                font.bold: true
            }
            Rectangle { width: parent.width; height: 1; color: Theme.buttonBackground }
        }
    }

    component SettingsComboBox: ComboBox {
        id: combo
        delegate: ItemDelegate {
            id: entry
            required property var model
            required property int index
            objectName: combo.objectName + "Option" + index
            width: ListView.view.width
            text: model[combo.textRole]
            font.weight: combo.currentIndex === index ? Font.DemiBold : Font.Normal
            highlighted: combo.highlightedIndex === index
            hoverEnabled: combo.hoverEnabled
            palette.text: Theme.textPrimary
            palette.highlightedText: Theme.accent
            background: Rectangle {
                color: entry.highlighted || entry.hovered || entry.down
                    ? Theme.chromeHover : Theme.searchBackground
            }
        }
    }

    contentItem: RowLayout {
        spacing: 0
        Rectangle {
            Layout.preferredWidth: root.width < 650 ? 130 : 190
            Layout.fillHeight: true
            color: Theme.searchBackground
            Column {
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                spacing: 8
                Label {
                    text: "KPlayer"
                    color: Theme.textPrimary
                    font.pixelSize: 22
                    font.bold: true
                    leftPadding: 12
                    bottomPadding: 20
                    topPadding: 8
                }
                Repeater {
                    model: root.pageTitles
                    delegate: Button {
                        id: navigationButton
                        objectName: "settingsPage" + index
                        required property int index
                        required property string modelData
                        width: parent.width
                        height: 48
                        text: modelData
                        checkable: true
                        checked: root.currentPage === navigationButton.index
                        onClicked: root.currentPage = navigationButton.index
                        contentItem: Label {
                            text: navigationButton.text
                            leftPadding: 16
                            verticalAlignment: Text.AlignVCenter
                            color: root.currentPage === navigationButton.index ? Theme.accent : Theme.textPrimary
                            font.pixelSize: 16
                        }
                        background: Rectangle {
                            radius: 8
                            color: root.currentPage === navigationButton.index ? Theme.panelBackground
                                  : navigationButton.hovered ? Theme.chromeHover : Theme.transparent
                            Rectangle {
                                width: 3
                                height: 22
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                radius: 1
                                color: Theme.accent
                                visible: root.currentPage === navigationButton.index
                            }
                        }
                    }
                }
            }
        }
        ScrollView {
            id: settingsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            Column {
                width: settingsScroll.availableWidth
                padding: root.width < 650 ? 16 : 28
                spacing: 20
                Label {
                    text: root.pageTitles[root.currentPage]
                    color: Theme.textPrimary
                    font.pixelSize: 28
                    font.bold: true
                }
                SettingsCard {
                    width: parent.width - parent.padding * 2
                    visible: root.currentPage === 0
                    heading: qsTr("語言")
                    GridLayout {
                        width: parent.width
                        columns: root.width < 650 ? 1 : 2
                        columnSpacing: 20
                        rowSpacing: 18
                        Label {
                            text: qsTr("介面語言")
                            color: Theme.textPrimary
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        SettingsComboBox {
                            id: interfaceLanguage
                            objectName: "interfaceLanguageChoice"
                            Layout.preferredWidth: root.width < 650 ? 150 : 220
                            model: ["繁體中文", "English"]
                            Accessible.name: qsTr("介面語言")
                        }
                    }
                }
                SettingsCard {
                    width: parent.width - parent.padding * 2
                    visible: root.currentPage === 1
                    heading: qsTr("播放速度")
                    GridLayout {
                        width: parent.width
                        columns: root.width < 650 ? 1 : 2
                        columnSpacing: 20
                        rowSpacing: 18
                        Label { text: qsTr("最高播放速度 (×)"); color: Theme.textPrimary; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        SpinBox {
                            id: speedLimit
                            objectName: "maximumSpeedInput"
                            Layout.preferredWidth: root.width < 650 ? 150 : 220
                            from: 10
                            to: 1000
                            stepSize: 10
                            editable: true
                            validator: DoubleValidator { bottom: 1; top: 100; decimals: 1; locale: speedLimit.locale.name }
                            textFromValue: (value, locale) => Number(value / 10).toLocaleString(locale, 'f', 1)
                            valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * 10)
                            Accessible.name: qsTr("最高播放速度 (×)")
                        }
                        Label { text: qsTr("播放速度 (×)"); color: Theme.textPrimary; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        SpinBox {
                            id: customSpeed
                            objectName: "customSpeedInput"
                            Layout.preferredWidth: root.width < 650 ? 150 : 220
                            from: 2
                            to: speedLimit.value
                            stepSize: 1
                            editable: true
                            validator: DoubleValidator { bottom: 0.2; top: speedLimit.value / 10; decimals: 1; locale: customSpeed.locale.name }
                            textFromValue: (value, locale) => Number(value / 10).toLocaleString(locale, 'f', 1)
                            valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * 10)
                            Accessible.name: qsTr("播放速度 (×)")
                        }
                    }
                }
                SettingsCard {
                    width: parent.width - parent.padding * 2
                    visible: root.currentPage === 2
                    heading: qsTr("字幕偏好")
                    Label {
                        text: qsTr("預設字幕語言")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.smallFontSize
                    }
                    SettingsComboBox {
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
    }
}
