pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

Window {
    id: root
    required property var player
    property int currentTab: 0
    property bool pinned: false
    property bool positioned: false
    property real panelOpacity: 1
    readonly property var tabNames: [qsTr("Audio"), qsTr("Video"), qsTr("Subtitle"), qsTr("Playback")]
    readonly property var frequencies: ["60", "170", "310", "600", "1K", "3K", "6K", "12K", "14K", "16K"]
    readonly property var presets: [
        {
            name: qsTr("(Default)"),
            bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        },
        {
            name: qsTr("Rock"),
            bands: [5, 3, -2, -3, -1, 2, 4, 5, 5, 4]
        },
        {
            name: qsTr("Pop"),
            bands: [-1, 2, 4, 4, 2, -1, -2, -2, -1, -1]
        },
        {
            name: qsTr("Classical"),
            bands: [4, 3, 2, 0, -1, -1, 0, 2, 3, 4]
        }
    ]
    width: 300
    height: 224
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    title: qsTr("Control Panel")
    color: Theme.transparent
    opacity: panelOpacity
    flags: Qt.Tool | Qt.FramelessWindowHint | (pinned ? Qt.WindowStaysOnTopHint : 0)

    function open() {
        show();
        raise();
        requestActivate();
    }
    function formatTime(seconds: real): string {
        const total = Math.max(0, Math.floor(seconds));
        return [Math.floor(total / 3600), Math.floor(total / 60) % 60, total % 60].map(value => value.toString().padStart(2, "0")).join(":");
    }
    function adjustBand(index: int, value: real) {
        const bands = Array.from(player.equalizerBands);
        bands[index] = value;
        player.equalizerBands = bands;
        presetChoice.currentIndex = -1;
    }

    component PanelLabel: Label {
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: 11
        verticalAlignment: Text.AlignVCenter
    }
    component PanelButton: Button {
        id: button
        implicitWidth: 54
        implicitHeight: 22
        padding: 3
        font.family: Theme.fontFamily
        font.pixelSize: 11
        opacity: enabled ? 1 : 0.45
        contentItem: PanelLabel {
            text: button.text
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.controlRadius
            color: button.down || button.checked ? Theme.hintText : button.hovered ? Theme.buttonHover : Theme.buttonBackground
            border.width: button.activeFocus ? 1 : 0
            border.color: Theme.textPrimary
        }
    }
    component PanelSlider: Slider {
        id: slider
        property real backendValue: 0
        property color fillColor: Theme.hintText
        property string valueSuffix: ""
        implicitHeight: 22
        implicitWidth: 100
        padding: 4
        hoverEnabled: true
        wheelEnabled: true
        opacity: enabled ? 1 : 0.45
        onBackendValueChanged: if (!pressed)
            value = backendValue
        Component.onCompleted: value = backendValue
        ToolTip.visible: hovered || pressed || activeFocus
        ToolTip.text: Math.round(value * 10) / 10 + valueSuffix
        ToolTip.delay: pressed ? 0 : 600
        background: Rectangle {
            x: slider.horizontal ? slider.leftPadding : slider.leftPadding + (slider.availableWidth - width) / 2
            y: slider.horizontal ? slider.topPadding + (slider.availableHeight - height) / 2 : slider.topPadding
            width: slider.horizontal ? slider.availableWidth : 2
            height: slider.horizontal ? 2 : slider.availableHeight
            color: Theme.chromeBackground
            Rectangle {
                y: slider.horizontal ? 0 : slider.visualPosition * parent.height
                width: slider.horizontal ? slider.position * parent.width : parent.width
                height: slider.horizontal ? parent.height : slider.position * parent.height
                color: slider.fillColor
            }
        }
        handle: Rectangle {
            x: slider.horizontal ? slider.leftPadding + slider.visualPosition * (slider.availableWidth - width) : slider.leftPadding + (slider.availableWidth - width) / 2
            y: slider.horizontal ? slider.topPadding + (slider.availableHeight - height) / 2 : slider.topPadding + slider.visualPosition * (slider.availableHeight - height)
            width: slider.horizontal && slider.fillColor === Theme.accent ? 12 : 8
            height: slider.horizontal && slider.fillColor === Theme.accent ? 5 : 8
            radius: height / 2
            color: Theme.textPrimary
            border.width: slider.activeFocus ? 1 : 0
            border.color: slider.fillColor
        }
    }
    component PanelCheck: CheckBox {
        id: check
        implicitHeight: 22
        spacing: 3
        padding: 0
        font.pixelSize: 11
        indicator: Rectangle {
            x: 0
            y: (check.height - height) / 2
            width: 12
            height: 12
            radius: 3
            color: check.checked ? Theme.hintText : Theme.buttonBackground
            border.width: check.activeFocus ? 1 : 0
            border.color: Theme.textPrimary
            Text {
                anchors.centerIn: parent
                text: check.checked ? "✓" : ""
                color: Theme.textPrimary
                font.pixelSize: 10
            }
        }
        contentItem: PanelLabel {
            text: check.text
            leftPadding: 15
        }
    }
    component PanelCombo: ComboBox {
        id: combo
        implicitHeight: 22
        font.family: Theme.fontFamily
        font.pixelSize: 11
        leftPadding: 5
        rightPadding: 20
        palette.text: Theme.textPrimary
        palette.base: Theme.buttonBackground
        palette.highlight: Theme.hintText
        palette.highlightedText: Theme.textPrimary
        contentItem: PanelLabel {
            text: combo.displayText
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: Theme.buttonBackground
            radius: Theme.controlRadius
        }
        indicator: PlayerIcon {
            x: combo.width - width - 6
            y: (combo.height - height) / 2
            width: 9
            height: 9
            name: "chevronDown"
            color: Theme.textMuted
        }
        delegate: ItemDelegate {
            id: entry
            required property var modelData
            required property int index
            width: combo.width
            height: 24
            highlighted: combo.highlightedIndex === index
            contentItem: PanelLabel {
                text: combo.textRole ? entry.modelData[combo.textRole] : entry.modelData
            }
            background: Rectangle {
                color: entry.highlighted ? Theme.hintText : Theme.panelBackground
            }
        }
        popup: Popup {
            y: combo.height
            width: combo.width
            padding: 1
            implicitHeight: Math.min(180, combo.count * 24 + 2)
            background: Rectangle {
                color: Theme.panelBackground
                border.color: Theme.border
            }
            contentItem: ListView {
                clip: true
                model: combo.delegateModel
                currentIndex: combo.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
        }
    }
    component Divider: Rectangle {
        width: parent.width
        height: 1
        color: Theme.menuSeparator
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.tabBackground
        border.color: Theme.border
        border.width: 2
        focus: true
        Keys.onEscapePressed: root.hide()

        Rectangle {
            id: titleBar
            x: 2
            y: 2
            width: parent.width - 4
            height: 30
            color: Theme.chromeBackground
            MouseArea {
                objectName: "controlPanelDragHandle"
                anchors.fill: parent
                onPressed: root.startSystemMove()
            }
            PanelLabel {
                anchors.left: parent.left
                anchors.leftMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                text: root.title
                font.bold: true
            }
            PanelSlider {
                objectName: "controlPanelOpacitySlider"
                anchors.right: pinButton.left
                anchors.rightMargin: 5
                anchors.verticalCenter: parent.verticalCenter
                width: 55
                from: 0.25
                to: 1
                stepSize: 0.01
                backendValue: root.panelOpacity
                fillColor: Theme.accent
                Accessible.name: qsTr("Panel opacity")
                ToolTip.text: qsTr("Opacity: %1%").arg(Math.round(value * 100))
                onMoved: root.panelOpacity = value
            }
            IconButton {
                id: pinButton
                objectName: "controlPanelPinButton"
                anchors.right: closeButton.left
                width: 25
                height: parent.height
                iconSize: 12
                iconName: "pin"
                iconColor: root.pinned ? Theme.accent : Theme.textMuted
                label: qsTr("Always on top")
                onClicked: root.pinned = !root.pinned
            }
            IconButton {
                id: closeButton
                objectName: "controlPanelCloseButton"
                anchors.right: parent.right
                width: 25
                height: parent.height
                iconSize: 12
                iconName: "close"
                label: qsTr("Close")
                onClicked: root.hide()
            }
        }
        Row {
            id: tabs
            x: 2
            y: 33
            width: parent.width - 4
            height: 30
            Repeater {
                model: root.tabNames
                Button {
                    id: tab
                    required property string modelData
                    required property int index
                    objectName: "controlPanelTab" + index
                    width: tabs.width / 4
                    height: tabs.height
                    padding: 0
                    onClicked: root.currentTab = index
                    contentItem: PanelLabel {
                        text: tab.modelData
                        font.pixelSize: 12
                        color: root.currentTab === tab.index ? Theme.textPrimary : Theme.textMuted
                        horizontalAlignment: Text.AlignHCenter
                    }
                    background: Rectangle {
                        color: root.currentTab === tab.index ? Theme.tabBackground : Theme.chromeBackground
                        border.color: tab.activeFocus ? Theme.hintText : Theme.border
                    }
                }
            }
        }
        StackLayout {
            x: 12
            y: 74
            width: parent.width - 24
            height: parent.height - y - 12
            currentIndex: root.currentTab
            Item {
                RowLayout {
                    id: audioTools
                    width: parent.width
                    height: 30
                    spacing: 5
                    PanelCheck {
                        objectName: "panelEqualizerCheck"
                        text: qsTr("Equalizer")
                        checked: root.player.equalizerEnabled
                        onToggled: root.player.equalizerEnabled = checked
                    }
                    PanelCombo {
                        id: presetChoice
                        objectName: "panelEqualizerPreset"
                        Layout.fillWidth: true
                        model: root.presets
                        textRole: "name"
                        displayText: currentIndex < 0 ? qsTr("(Custom)") : currentText
                        onActivated: {
                            root.player.equalizerBands = root.presets[currentIndex].bands;
                            root.player.preamp = 0;
                        }
                    }
                    PanelButton {
                        text: qsTr("Preset")
                        implicitWidth: 46
                        onClicked: {
                            if (presetChoice.popup.visible)
                                presetChoice.popup.close();
                            else
                                presetChoice.popup.open();
                        }
                    }
                    PanelButton {
                        text: qsTr("VOL")
                        implicitWidth: 33
                        checkable: true
                        checked: root.player.muted
                        Accessible.name: qsTr("Mute")
                        onClicked: root.player.muted = !root.player.muted
                    }
                }
                Row {
                    anchors.top: audioTools.bottom
                    anchors.topMargin: 10
                    width: parent.width
                    height: 94
                    Repeater {
                        model: root.frequencies
                        Column {
                            id: band
                            required property int index
                            required property string modelData
                            width: 23
                            spacing: 2
                            PanelLabel {
                                width: parent.width
                                height: 12
                                text: band.modelData
                                font.pixelSize: 7
                                color: Theme.textMuted
                                horizontalAlignment: Text.AlignHCenter
                            }
                            PanelSlider {
                                objectName: "panelEqualizerBand" + band.index
                                width: parent.width
                                height: 78
                                orientation: Qt.Vertical
                                from: -12
                                to: 12
                                stepSize: 0.5
                                backendValue: root.player.equalizerBands[band.index]
                                enabled: root.player.equalizerEnabled
                                valueSuffix: " dB"
                                Accessible.name: qsTr("Equalizer %1 Hz").arg(band.modelData)
                                onMoved: root.adjustBand(band.index, value)
                            }
                        }
                    }
                    Column {
                        width: 23
                        spacing: 2
                        PanelLabel {
                            width: parent.width
                            height: 12
                            text: qsTr("MST")
                            font.pixelSize: 7
                            color: Theme.textMuted
                            horizontalAlignment: Text.AlignHCenter
                        }
                        PanelSlider {
                            objectName: "panelPreampSlider"
                            width: parent.width
                            height: 78
                            orientation: Qt.Vertical
                            from: -12
                            to: 12
                            stepSize: 0.5
                            backendValue: root.player.preamp
                            enabled: root.player.equalizerEnabled
                            valueSuffix: " dB"
                            Accessible.name: qsTr("Preamp")
                            onMoved: root.player.preamp = value
                        }
                    }
                    Column {
                        width: 23
                        spacing: 2
                        PanelLabel {
                            width: parent.width
                            height: 12
                            text: qsTr("VOL")
                            font.pixelSize: 7
                            color: Theme.textMuted
                            horizontalAlignment: Text.AlignHCenter
                        }
                        PanelSlider {
                            objectName: "panelVolumeSlider"
                            width: parent.width
                            height: 78
                            orientation: Qt.Vertical
                            from: 0
                            to: 100
                            stepSize: 1
                            backendValue: root.player.volume
                            valueSuffix: "%"
                            Accessible.name: qsTr("Volume")
                            onMoved: {
                                root.player.volume = value;
                                if (value > 0)
                                    root.player.muted = false;
                            }
                        }
                    }
                }
            }
            Item {
                Column {
                    y: 8
                    width: parent.width
                    spacing: 1
                    Repeater {
                        model: [
                            {
                                label: qsTr("Brightness"),
                                key: "brightness"
                            },
                            {
                                label: qsTr("Contrast"),
                                key: "contrast"
                            },
                            {
                                label: qsTr("Saturation"),
                                key: "saturation"
                            },
                            {
                                label: qsTr("Color"),
                                key: "hue"
                            }
                        ]
                        RowLayout {
                            id: adjustment
                            required property var modelData
                            width: parent.width
                            height: 23
                            spacing: 5
                            PanelLabel {
                                Layout.preferredWidth: 56
                                horizontalAlignment: Text.AlignRight
                                text: adjustment.modelData.label
                            }
                            PanelSlider {
                                objectName: "panel" + adjustment.modelData.key + "Slider"
                                Layout.fillWidth: true
                                from: -100
                                to: 100
                                stepSize: 1
                                backendValue: root.player[adjustment.modelData.key]
                                Accessible.name: adjustment.modelData.label
                                onMoved: root.player[adjustment.modelData.key] = Math.round(value)
                            }
                            PanelButton {
                                text: qsTr("Default")
                                implicitWidth: 48
                                onClicked: root.player[adjustment.modelData.key] = 0
                            }
                        }
                    }
                    Divider {}
                    RowLayout {
                        width: parent.width
                        height: 35
                        spacing: 7
                        PanelCheck {
                            text: qsTr("Soften")
                            checked: root.player.soften
                            onToggled: root.player.soften = checked
                        }
                        PanelCheck {
                            text: qsTr("Sharpen")
                            checked: root.player.sharpen
                            onToggled: root.player.sharpen = checked
                        }
                        PanelCheck {
                            text: qsTr("Deblock")
                            checked: root.player.deblock
                            onToggled: root.player.deblock = checked
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        PanelButton {
                            objectName: "panelCaptureButton"
                            text: qsTr("Capture")
                            implicitWidth: 50
                            enabled: root.player.mediaLoaded
                            background: Rectangle {
                                radius: Theme.controlRadius
                                color: Theme.hintText
                            }
                            onClicked: captureDialog.open()
                        }
                    }
                }
            }
            Column {
                spacing: 11
                RowLayout {
                    width: parent.width
                    height: 33
                    spacing: 5
                    PanelLabel {
                        Layout.preferredWidth: 49
                        text: qsTr("Font")
                        horizontalAlignment: Text.AlignRight
                    }
                    PanelCombo {
                        id: fontChoice
                        objectName: "panelSubtitleFont"
                        Layout.fillWidth: true
                        model: Qt.fontFamilies()
                        currentIndex: Math.max(0, model.indexOf(root.player.subtitleFont))
                        onActivated: root.player.subtitleFont = currentText
                    }
                    SpinBox {
                        id: fontSize
                        objectName: "panelSubtitleSize"
                        Layout.preferredWidth: 51
                        implicitHeight: 22
                        from: 8
                        to: 72
                        value: root.player.subtitleFontSize
                        editable: true
                        padding: 3
                        rightPadding: 15
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        onValueModified: root.player.subtitleFontSize = value
                        contentItem: TextInput {
                            text: fontSize.textFromValue(fontSize.value, fontSize.locale)
                            font: fontSize.font
                            color: Theme.textPrimary
                            verticalAlignment: Text.AlignVCenter
                            validator: fontSize.validator
                            readOnly: !fontSize.editable
                            inputMethodHints: Qt.ImhDigitsOnly
                        }
                        background: Rectangle {
                            color: Theme.buttonBackground
                            radius: Theme.controlRadius
                        }
                        up.indicator: Text {
                            x: fontSize.width - 14
                            width: 14
                            height: 11
                            text: "▴"
                            color: Theme.textMuted
                            horizontalAlignment: Text.AlignHCenter
                            font.pixelSize: 10
                        }
                        down.indicator: Text {
                            x: fontSize.width - 14
                            y: 11
                            width: 14
                            height: 11
                            text: "▾"
                            color: Theme.textMuted
                            horizontalAlignment: Text.AlignHCenter
                            font.pixelSize: 10
                        }
                    }
                }
                Divider {}
                RowLayout {
                    width: parent.width
                    height: 24
                    spacing: 4
                    PanelLabel {
                        Layout.preferredWidth: 49
                        text: qsTr("Position")
                        horizontalAlignment: Text.AlignRight
                    }
                    PanelButton {
                        text: qsTr("▲Up")
                        Layout.fillWidth: true
                        implicitWidth: 0
                        onClicked: root.player.subtitlePosition -= 2
                    }
                    PanelButton {
                        text: qsTr("▼Down")
                        Layout.fillWidth: true
                        implicitWidth: 0
                        onClicked: root.player.subtitlePosition += 2
                    }
                    PanelButton {
                        text: qsTr("◀Left")
                        Layout.fillWidth: true
                        implicitWidth: 0
                        onClicked: root.player.subtitleAlignment -= 1
                    }
                    PanelButton {
                        text: qsTr("▶Right")
                        Layout.fillWidth: true
                        implicitWidth: 0
                        onClicked: root.player.subtitleAlignment += 1
                    }
                }
                Divider {}
                RowLayout {
                    width: parent.width
                    height: 32
                    spacing: 4
                    PanelLabel {
                        Layout.preferredWidth: 49
                        text: qsTr("Sync")
                        horizontalAlignment: Text.AlignRight
                    }
                    PanelButton {
                        objectName: "panelSubtitleSlower"
                        text: qsTr("Slower")
                        Layout.fillWidth: true
                        onClicked: root.player.subtitleDelay = Math.round((root.player.subtitleDelay + 0.1) * 10) / 10
                    }
                    PanelButton {
                        objectName: "panelSubtitleFaster"
                        text: qsTr("Faster")
                        Layout.fillWidth: true
                        onClicked: root.player.subtitleDelay = Math.round((root.player.subtitleDelay - 0.1) * 10) / 10
                    }
                    PanelButton {
                        text: qsTr("Default")
                        Layout.fillWidth: true
                        onClicked: root.player.subtitleDelay = 0
                    }
                }
            }
            Column {
                spacing: 11
                RowLayout {
                    width: parent.width
                    height: 33
                    spacing: 4
                    PanelLabel {
                        Layout.preferredWidth: 52
                        text: qsTr("Seek")
                        horizontalAlignment: Text.AlignRight
                    }
                    Repeater {
                        model: [
                            {
                                symbol: "◀◀",
                                seconds: -30
                            },
                            {
                                symbol: "◀",
                                seconds: -5
                            },
                            {
                                symbol: "▶",
                                seconds: 5
                            },
                            {
                                symbol: "▶▶",
                                seconds: 30
                            }
                        ]
                        PanelButton {
                            id: seekButton
                            required property var modelData
                            Layout.fillWidth: true
                            implicitWidth: 0
                            enabled: root.player.seekable
                            Accessible.name: qsTr("Seek %1 seconds").arg(modelData.seconds)
                            contentItem: PanelLabel {
                                text: seekButton.modelData.symbol
                                color: Theme.textMuted
                                font.pixelSize: 9
                                horizontalAlignment: Text.AlignHCenter
                            }
                            onClicked: root.player.seekRelative(modelData.seconds)
                        }
                    }
                }
                Divider {}
                RowLayout {
                    width: parent.width
                    height: 24
                    spacing: 4
                    PanelLabel {
                        Layout.preferredWidth: 52
                        text: qsTr("Speed")
                        horizontalAlignment: Text.AlignRight
                    }
                    PanelButton {
                        objectName: "panelSpeedSlower"
                        text: qsTr("Slower")
                        Layout.fillWidth: true
                        onClicked: root.player.speed = Math.round((root.player.speed - 0.1) * 10) / 10
                    }
                    PanelButton {
                        objectName: "panelSpeedFaster"
                        text: qsTr("Faster")
                        Layout.fillWidth: true
                        onClicked: root.player.speed = Math.round((root.player.speed + 0.1) * 10) / 10
                    }
                    PanelButton {
                        objectName: "panelSpeedDefault"
                        text: qsTr("Default")
                        Layout.fillWidth: true
                        onClicked: root.player.speed = 1
                    }
                }
                Divider {}
                RowLayout {
                    width: parent.width
                    height: 32
                    spacing: 4
                    PanelLabel {
                        Layout.preferredWidth: 52
                        text: qsTr("A-B")
                        horizontalAlignment: Text.AlignRight
                    }
                    PanelButton {
                        objectName: "panelLoopA"
                        text: root.formatTime(root.player.loopA) + "   A"
                        Layout.fillWidth: true
                        implicitWidth: 78
                        enabled: root.player.seekable
                        checked: root.player.loopA >= 0
                        Accessible.name: qsTr("Set loop start at current position")
                        onClicked: root.player.setLoopPoint(false, root.player.position)
                    }
                    PanelLabel {
                        text: "~"
                        color: Theme.textMuted
                    }
                    PanelButton {
                        objectName: "panelLoopB"
                        text: root.formatTime(root.player.loopB) + "   B"
                        Layout.fillWidth: true
                        implicitWidth: 78
                        enabled: root.player.seekable
                        checked: root.player.loopB >= 0
                        Accessible.name: qsTr("Set loop end at current position")
                        onClicked: root.player.setLoopPoint(true, root.player.position)
                    }
                    PanelButton {
                        objectName: "panelLoopUndo"
                        text: qsTr("Undo")
                        implicitWidth: 37
                        onClicked: root.player.clearLoop()
                    }
                }
            }
        }
    }
    FileDialog {
        id: captureDialog
        title: qsTr("Save captured frame")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG image (*.png)")]
        onAccepted: root.player.captureFrame(selectedFile)
    }
}
