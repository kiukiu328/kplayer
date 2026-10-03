pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root
    property bool maximized: false
    property bool fullscreen: false
    property bool alwaysOnTop: false
    property bool menuOpen: false
    signal menuRequested()
    signal pinRequested()
    signal minimizeRequested()
    signal maximizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    height: Theme.titleHeight
    color: Theme.chromeBackground

    Rectangle {
        id: appButton
        objectName: "appMenuButton"
        width: Theme.titleMenuWidth
        height: parent.height
        color: appHover.hovered || activeFocus || root.menuOpen ? Theme.chromeHover : Theme.transparent
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Application menu")
        Accessible.onPressAction: root.menuRequested()
        Keys.onSpacePressed: root.menuRequested()
        Keys.onReturnPressed: root.menuRequested()
        ToolTip.text: qsTr("Application menu")
        ToolTip.visible: !root.menuOpen && (appHover.hovered || activeFocus)
        ToolTip.delay: 600
        ToolTip.timeout: 5000
        HoverHandler { id: appHover }
        TapHandler {
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onTapped: root.menuRequested()
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: "KPlayer"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            color: Theme.textPrimary
        }
        PlayerIcon {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            name: "chevronDown"
            color: Theme.iconMuted
        }
        Rectangle {
            anchors.right: parent.right
            height: parent.height
            width: Theme.dividerWidth
            color: Theme.border
        }
    }

    Row {
        id: windowControls
        anchors.right: parent.right
        anchors.rightMargin: 8
        height: parent.height
        Repeater {
            model: [
                { icon: "pin", label: root.alwaysOnTop ? qsTr("Disable always on top") : qsTr("Always on top") },
                { icon: "minimize", label: qsTr("Minimize") },
                { icon: root.maximized ? "restore" : "maximize",
                  label: root.maximized ? qsTr("Restore") : qsTr("Maximize") },
                { icon: root.fullscreen ? "exitFullscreen" : "fullscreen",
                  label: root.fullscreen ? qsTr("Exit full screen") : qsTr("Full screen") },
                { icon: "close", label: qsTr("Close") }
            ]
            IconButton {
                required property var modelData
                required property int index
                width: Theme.windowButtonWidth
                height: windowControls.height
                iconName: modelData.icon
                objectName: index === 0 ? "pinButton" : modelData.icon + "WindowButton"
                label: modelData.label
                iconColor: index === 0 && root.alwaysOnTop ? Theme.accent : Theme.iconMuted
                backgroundColor: index === 0 && root.alwaysOnTop ? Theme.chromeHover : Theme.transparent
                iconSize: 20
                onClicked: {
                    switch (index) {
                    case 0: root.pinRequested(); break;
                    case 1: root.minimizeRequested(); break;
                    case 2: root.maximizeRequested(); break;
                    case 3: root.fullscreenRequested(); break;
                    case 4: root.closeRequested(); break;
                    }
                }
            }
        }
    }
    Rectangle {
        anchors.right: windowControls.left
        anchors.rightMargin: 7
        height: parent.height
        width: Theme.dividerWidth
        color: Theme.border
    }
}
