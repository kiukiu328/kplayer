pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.VectorImage
import QtQuick.Effects

Item {
    id: root
    required property string name
    property color color: Theme.iconPrimary
    implicitWidth: Theme.iconSize
    implicitHeight: Theme.iconSize

    VectorImage {
        anchors.fill: parent
        source: Qt.resolvedUrl("icons/" + root.name + ".svg")
        fillMode: VectorImage.PreserveAspectFit
        preferredRendererType: VectorImage.CurveRenderer
        layer.enabled: true
        layer.effect: MultiEffect {
            colorization: 1
            colorizationColor: root.color
        }
    }
}
