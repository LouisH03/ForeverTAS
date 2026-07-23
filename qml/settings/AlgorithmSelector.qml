import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property string title
    property string comboObjectName
    property var options: []
    property string selectedId
    property var controller
    readonly property var selectedOption:
        optionCombo.currentIndex >= 0
        && optionCombo.currentIndex < options.length
        ? options[optionCombo.currentIndex]
        : null

    signal selectionRequested(string id)

    spacing: 6

    function optionIndex(id) {
        for (let index = 0; index < options.length; ++index) {
            if (options[index].id === id)
                return index
        }
        return -1
    }

    Label {
        text: root.title
        font.weight: Font.Medium
    }

    ComboBox {
        id: optionCombo
        objectName: root.comboObjectName
        Layout.fillWidth: true
        model: root.options
        textRole: "label"
        valueRole: "id"
        currentIndex: root.optionIndex(root.selectedId)
        enabled: !root.controller.running
        onActivated: root.selectionRequested(currentValue.toString())
    }

    Loader {
        id: settingsLoader
        objectName: root.comboObjectName + "SettingsLoader"
        Layout.fillWidth: true
        visible: active
        active: root.selectedOption !== null
                && root.selectedOption.settingsComponent.length > 0
        source: active ? root.selectedOption.settingsComponent : ""
        onLoaded: {
            if (item)
                item.controller = root.controller
        }
    }
}
