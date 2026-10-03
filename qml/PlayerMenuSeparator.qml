import QtQuick
import QtQuick.Controls.Basic

MenuSeparator {
    padding: 0
    verticalPadding: 3
    implicitHeight: 7
    contentItem: Rectangle {
        implicitWidth: Theme.menuWidth - 2
        implicitHeight: 1
        color: Theme.menuSeparator
    }
}
