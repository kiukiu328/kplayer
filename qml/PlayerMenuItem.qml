import QtQuick
import QtQuick.Controls.Basic

MenuItem {
    id: root
    property string shortcutLabel: action && action.shortcut ? action.shortcut.toString() : ""
    property bool radioIndicator: false
    readonly property color textColor: !enabled ? Theme.menuDisabledText
        : highlighted ? Theme.accent : Theme.menuText
    implicitWidth: Theme.menuWidth - 2
    implicitHeight: Theme.menuRowHeight
    padding: 0
    leftPadding: 27
    rightPadding: 16
    font.family: Theme.fontFamily
    font.pixelSize: Theme.menuFontSize
    contentItem: Item {
        implicitWidth: Theme.menuWidth - 45
        Text {
            anchors.left: parent.left
            anchors.right: shortcutText.left
            anchors.rightMargin: root.shortcutLabel.length > 0 ? 8 : 0
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            text: root.text
            color: root.textColor
            font: root.font
            elide: Text.ElideRight
        }
        Text {
            id: shortcutText
            anchors.right: parent.right
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            text: root.shortcutLabel
            color: root.textColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.menuShortcutFontSize
        }
    }
    indicator: Rectangle {
        x: 6
        y: (root.height - height) / 2
        width: root.radioIndicator ? 11 : 13
        height: width
        radius: root.radioIndicator ? width / 2 : 2
        visible: root.checked
        color: root.radioIndicator ? Theme.menuRadioBackground : Theme.menuCheckBackground
        Rectangle {
            anchors.centerIn: parent
            width: 5
            height: width
            radius: width / 2
            visible: root.radioIndicator
            color: Theme.menuRadioAccent
        }
        Text {
            anchors.centerIn: parent
            visible: !root.radioIndicator
            text: "✓"
            font.pixelSize: 12
            color: Theme.textPrimary
        }
    }
    arrow: Text {
        x: root.width - width - 10
        y: (root.height - height) / 2
        width: 8
        height: root.height
        text: ">"
        visible: root.subMenu !== null
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.menuShortcutFontSize
        color: root.textColor
    }
    background: Rectangle {
        color: root.highlighted ? Theme.menuHover : Theme.transparent
    }
}
