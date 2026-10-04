pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Dialog {
    id: root
    required property var player
    title: qsTr("影像調整")
    modal: true
    focus: true
    standardButtons: Dialog.Close
    font.family: Theme.fontFamily
    palette.window: Theme.chromeBackground
    palette.windowText: Theme.textPrimary
    palette.text: Theme.textPrimary
    palette.button: Theme.buttonBackground
    palette.buttonText: Theme.textPrimary
    contentItem: Column {
        spacing: 12
        Repeater {
            model: [
                { label: qsTr("亮度"), key: "brightness" },
                { label: qsTr("對比"), key: "contrast" },
                { label: qsTr("飽和度"), key: "saturation" }
            ]
            delegate: Column {
                id: adjustment
                required property var modelData
                width: parent.width
                Label {
                    text: adjustment.modelData.label + ": " + root.player[adjustment.modelData.key]
                    color: Theme.textPrimary
                }
                PlayerSlider {
                    objectName: adjustment.modelData.key + "Slider"
                    width: parent.width
                    from: -100
                    to: 100
                    stepSize: 1
                    backendValue: root.player[adjustment.modelData.key]
                    Accessible.name: adjustment.modelData.label
                    onMoved: root.player[adjustment.modelData.key] = Math.round(value)
                }
            }
        }
        Button {
            objectName: "resetVideoAdjustmentsButton"
            text: qsTr("重設")
            onClicked: root.player.resetVideoAdjustments()
        }
    }
}
