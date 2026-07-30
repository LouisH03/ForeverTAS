import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    readonly property bool slotStyled: true

    implicitHeight: 36
    leftPadding: 12
    rightPadding: 38

    delegate: ItemDelegate {
        id: optionDelegate

        required property int index

        width: control.popup.width - 2
        height: 36
        leftPadding: 11
        rightPadding: 11
        text: control.textAt(index)
        highlighted: control.highlightedIndex === index
        hoverEnabled: true
        onClicked: {
            control.currentIndex = index
            control.activated(index)
            control.popup.close()
        }

        contentItem: Text {
            text: optionDelegate.text
            color: optionDelegate.highlighted
                   ? AppTheme.textOnAccent : AppTheme.text
            font: optionDelegate.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: optionDelegate.highlighted
                   ? AppTheme.accent : AppTheme.surface
        }
    }

    contentItem: Text {
        objectName: control.objectName.length > 0
                    ? control.objectName + "Content"
                    : "styledComboContent"
        text: control.displayText
        color: control.enabled ? AppTheme.text : AppTheme.disabledText
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
            color: control.enabled ? AppTheme.textMuted : AppTheme.disabledText
        }

        Rectangle {
            width: 7
            height: 1.5
            x: 8.5
            y: 8
            radius: 1
            rotation: -42
            color: control.enabled ? AppTheme.textMuted : AppTheme.disabledText
        }
    }

    background: Rectangle {
        radius: 7
        color: control.enabled ? AppTheme.surface : AppTheme.disabledSurface
        border.width: control.popup.visible || control.activeFocus ? 2 : 1
        border.color: control.popup.visible
                      ? AppTheme.accent
                      : control.activeFocus
                        ? AppTheme.focus
                        : AppTheme.border
    }

    popup: Popup {
        y: control.height - 1
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2,
                                 control.Window.height
                                 - topMargin - bottomMargin)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: AppTheme.surface
            border.width: 1
            border.color: AppTheme.borderStrong
            radius: 6
        }
    }
}
