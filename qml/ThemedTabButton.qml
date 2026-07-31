import QtQuick
import QtQuick.Controls

TabButton {
    id: control

    readonly property bool themedControl: true
    readonly property color effectiveBackgroundColor:
        !enabled ? AppTheme.disabledSurface
        : down ? AppTheme.controlPressed
        : checked ? AppTheme.selection
        : hovered ? AppTheme.controlHover : AppTheme.surfaceAlternate
    readonly property color effectiveTextColor:
        enabled ? AppTheme.text : AppTheme.disabledText

    hoverEnabled: true
    implicitHeight: 38

    contentItem: Label {
        text: control.text
        font: control.font
        color: control.effectiveTextColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        color: control.effectiveBackgroundColor
        border.width: control.activeFocus ? 2 : 0
        border.color: AppTheme.focus

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            visible: control.checked
            color: AppTheme.accent
        }
    }
}
