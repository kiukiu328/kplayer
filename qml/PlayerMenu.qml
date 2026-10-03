import QtQuick
import QtQuick.Controls.Basic

Menu {
    id: root
    popupType: Popup.Item
    width: Theme.menuWidth
    height: Math.min(implicitHeight, parent ? parent.height - 16 : implicitHeight)
    padding: 1
    margins: 8
    overlap: 1
    cascade: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.menuFontSize
    palette.text: Theme.menuText
    palette.windowText: Theme.menuText
    palette.window: Theme.menuBackground
    palette.highlight: Theme.menuHover
    palette.highlightedText: Theme.accent
    delegate: PlayerMenuItem {}
    background: Rectangle {
        implicitWidth: Theme.menuWidth
        color: Theme.menuBackground
        border.color: Theme.border
        border.width: 1
    }
}
