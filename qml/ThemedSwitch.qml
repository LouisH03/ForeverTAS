import QtQuick
import QtQuick.Controls

Switch {
    id: control

    readonly property bool themedControl: true
    readonly property color effectiveTrackColor:
        !enabled ? AppTheme.disabledSurface
        : checked
          ? (down ? AppTheme.accentPressed
             : hovered ? AppTheme.accentHover : AppTheme.accent)
        : down ? AppTheme.controlPressed
        : hovered ? AppTheme.controlHover : AppTheme.control
    readonly property color effectiveBorderColor:
        !enabled ? AppTheme.border
        : activeFocus ? AppTheme.focus
        : checked ? AppTheme.accentBorder : AppTheme.borderStrong
    readonly property color effectiveTextColor:
        enabled ? AppTheme.text : AppTheme.disabledText

    hoverEnabled: true
    spacing: 9
    implicitHeight: 32

    indicator: Rectangle {
        implicitWidth: 42
        implicitHeight: 22
        x: control.leftPadding
        y: Math.round((control.height - height) / 2)
        radius: height / 2
        color: control.effectiveTrackColor
        border.width: control.activeFocus ? 2 : 1
        border.color: control.effectiveBorderColor

        Rectangle {
            width: 16
            height: 16
            radius: 8
            y: 3
            x: control.checked ? parent.width - width - 3 : 3
            color: control.enabled
                   ? (control.checked ? AppTheme.textOnAccent : AppTheme.text)
                   : AppTheme.disabledText

            Behavior on x {
                NumberAnimation {
                    duration: 90
                }
            }
        }
    }

    contentItem: Label {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.effectiveTextColor
        verticalAlignment: Text.AlignVCenter
    }
}
