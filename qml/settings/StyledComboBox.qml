import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    readonly property bool slotStyled: true

    implicitHeight: 36
    leftPadding: 12
    rightPadding: 38

    contentItem: Text {
        leftPadding: control.leftPadding
        rightPadding: control.rightPadding
        text: control.displayText
        color: control.enabled ? "#20251f" : "#7b8278"
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Item {
        width: 18
        height: 18
        x: control.width - width - 11
        y: Math.round((control.height - height) / 2)

        Rectangle {
            width: 7
            height: 1.5
            x: 2.5
            y: 8
            radius: 1
            rotation: 42
            color: control.enabled ? "#4f594d" : "#92988f"
        }

        Rectangle {
            width: 7
            height: 1.5
            x: 8.5
            y: 8
            radius: 1
            rotation: -42
            color: control.enabled ? "#4f594d" : "#92988f"
        }
    }

    background: Rectangle {
        radius: 7
        color: control.enabled ? "#ffffff" : "#ecefe9"
        border.width: control.popup.visible || control.activeFocus ? 2 : 1
        border.color: control.popup.visible
                      ? "#6d7b69"
                      : control.activeFocus
                        ? "#899686"
                        : "#c5ccc1"
    }
}
