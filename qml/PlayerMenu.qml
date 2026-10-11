import QtQuick
import QtQuick.Controls.Basic

Menu {
    id: root
    popupType: Popup.Item
    width: Math.min(420, parent && parent.Window.window
        ? parent.Window.window.width - 16 : 420)
    // A submenu's visual parent can be its menu row, rather than the window.
    height: Math.min(implicitHeight, parent && parent.Window.window
        ? parent.Window.window.height - 16 : implicitHeight)
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
