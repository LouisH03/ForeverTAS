import QtQuick
import QtQuick.Controls

Slider {
    id: control

    readonly property bool themedControl: true
    readonly property color effectiveTrackColor:
        enabled ? AppTheme.control : AppTheme.disabledSurface
    readonly property color effectiveHandleColor:
        !enabled ? AppTheme.disabledText
        : pressed ? AppTheme.accentPressed
        : hovered ? AppTheme.accentHover : AppTheme.accent

    hoverEnabled: true
    implicitWidth: 132
    implicitHeight: 32

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 5
        radius: 3
        color: control.effectiveTrackColor
        border.width: 1
        border.color: !control.enabled
                      ? AppTheme.border
                      : control.activeFocus
                      ? AppTheme.focus : AppTheme.borderStrong

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: control.enabled ? AppTheme.accent : AppTheme.disabledText
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 18
        implicitHeight: 18
        radius: 9
        color: control.effectiveHandleColor
        border.width: control.activeFocus ? 2 : 1
        border.color: !control.enabled
                      ? AppTheme.border
                      : control.activeFocus
                      ? AppTheme.focus : AppTheme.accentBorder
    }
}
