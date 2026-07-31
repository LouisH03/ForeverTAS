import QtQuick
import QtQuick.Controls

ItemDelegate {
    id: control

    readonly property bool themedControl: true
    readonly property color effectiveBackgroundColor:
        !enabled ? AppTheme.disabledSurface
        : down ? AppTheme.controlPressed
        : highlighted || hovered ? AppTheme.selection : "transparent"
    readonly property color effectiveTextColor:
        enabled ? AppTheme.text : AppTheme.disabledText

    hoverEnabled: true

    contentItem: Label {
        text: control.text
        font: control.font
        color: control.effectiveTextColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        color: control.effectiveBackgroundColor
        border.width: control.activeFocus ? 2 : 0
        border.color: AppTheme.focus
    }
}
