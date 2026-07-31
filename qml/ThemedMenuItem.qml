import QtQuick
import QtQuick.Controls

MenuItem {
    id: control

    readonly property bool themedControl: true
    readonly property color effectiveBackgroundColor:
        !enabled ? AppTheme.disabledSurface
        : down ? AppTheme.accentPressed
        : highlighted || hovered ? AppTheme.accent : AppTheme.surface
    readonly property color effectiveTextColor:
        !enabled ? AppTheme.disabledText
        : highlighted || hovered || down
          ? AppTheme.textOnAccent : AppTheme.text

    hoverEnabled: true
    implicitHeight: 34
    leftPadding: 12
    rightPadding: 12

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
