pragma Singleton
import QtQuick

QtObject {
    // All colors are centralized here; components use theme tokens exclusively.
    readonly property color border: "#050505"
    readonly property color videoBackground: "#101010"
    readonly property color chromeBackground: "#1e1e1e"
    readonly property color panelBackground: "#393939"
    readonly property color tabBackground: "#303030"
    readonly property color footerBackground: "#303030"
    readonly property color buttonBackground: "#505050"
    readonly property color buttonHover: "#606060"
    readonly property color chromeHover: "#303030"
    readonly property color searchBackground: "#252525"
    readonly property color textPrimary: "#eeeeee"
    readonly property color textMuted: "#888888"
    readonly property color iconPrimary: "#cccccc"
    readonly property color iconMuted: "#737373"
    readonly property color watermark: "#303030"
    readonly property color accent: "#f3df00"
    readonly property color menuBackground: "#444444"
    readonly property color menuHover: "#191919"
    readonly property color menuText: "#c5c5c5"
    readonly property color menuDisabledText: "#858585"
    readonly property color menuSeparator: "#292929"
    readonly property color menuCheckBackground: "#626672"
    readonly property color menuRadioBackground: "#171e28"
    readonly property color menuRadioAccent: "#2489ee"
    readonly property color transparent: "transparent"

    readonly property string fontFamily: Qt.platform.os === "windows" ? "Segoe UI" : "Noto Sans"
    readonly property int fontSize: 18
    readonly property int smallFontSize: 16
    readonly property int menuFontSize: 13
    readonly property int menuShortcutFontSize: 12
    readonly property int menuWidth: 193
    readonly property int playlistMenuWidth: 220
    readonly property int menuRowHeight: 22
    readonly property int watermarkFontSize: 42
    readonly property int windowBorder: 3
    readonly property int dividerWidth: 1
    readonly property int panelDivider: 5
    readonly property int titleHeight: 46
    readonly property int titleMenuWidth: 135
    readonly property int windowButtonWidth: 36
    readonly property int playlistWidth: 472
    readonly property real playlistRatio: 0.387
    readonly property int playlistHeaderHeight: 47
    readonly property int playlistTabsHeight: 47
    readonly property int playlistFooterHeight: 51
    readonly property int seekHeight: 32
    readonly property int transportHeight: 61
    readonly property int transportButtonWidth: 62
    readonly property int toolButtonWidth: 48
    readonly property int iconSize: 22
    readonly property int compactButtonSize: 30
    readonly property int compactButtonHeight: 27
    readonly property int controlRadius: 2
    readonly property int watermarkSize: 64
}
