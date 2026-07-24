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
    readonly property bool settingsLoaded:
        settingsLoader.status === Loader.Ready
        && settingsLoader.item !== null
    readonly property string settingsObjectName:
        settingsLoaded ? settingsLoader.item.objectName : ""

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
        visible: root.title.length > 0
        text: root.title
        font.weight: Font.Medium
    }

    StyledComboBox {
        id: optionCombo
        objectName: root.comboObjectName
        Layout.fillWidth: true
        model: root.options
        textRole: "label"
        valueRole: "id"
        currentIndex: root.optionIndex(root.selectedId)
        enabled: !root.controller.running
        onActivated: selectedIndex =>
            root.selectionRequested(valueAt(selectedIndex).toString())
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
