import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property var controller
    property var options: []
    property var passes: []
    readonly property int passCount: passes.length
    readonly property int renderedPassCount: passRepeater.count
    readonly property int passModelCount: passModel.count
    readonly property var firstRenderedPass:
        passRepeater.count > 0 ? passRepeater.itemAt(0) : null
    readonly property int firstPassOptionCount:
        firstRenderedPass ? firstRenderedPass.optionCount : -1
    readonly property string firstPassSelectedId:
        firstRenderedPass ? firstRenderedPass.selectedId : ""
    readonly property bool firstPassSettingsLoaded:
        firstRenderedPass ? firstRenderedPass.settingsLoaded : false
    readonly property string firstPassSettingsObjectName:
        firstRenderedPass ? firstRenderedPass.settingsObjectName : ""
    readonly property var firstPassSettingsItem:
        firstRenderedPass ? firstRenderedPass.settingsItem : null
    readonly property bool firstPassSlotStyled:
        firstRenderedPass ? firstRenderedPass.slotStyled : false
    property int rebuildCount: 0

    objectName: "modifierComposition"
    Layout.fillWidth: true
    spacing: 8

    function rebuildPassModel() {
        ++rebuildCount
        passModel.clear()
        for (let index = 0; index < passes.length; ++index) {
            passModel.append({
                passId: passes[index].id,
                passSettings: passes[index].settings
            })
        }
    }

    function synchronizePassModel() {
        if (passModel.count !== passes.length) {
            rebuildPassModel()
            return
        }

        for (let index = 0; index < passes.length; ++index) {
            passModel.setProperty(index, "passId", passes[index].id)
            passModel.setProperty(
                index, "passSettings", passes[index].settings)
        }
    }

    onPassesChanged: Qt.callLater(synchronizePassModel)
    Component.onCompleted: synchronizePassModel()

    ListModel {
        id: passModel
        dynamicRoles: true
    }

    function optionIndex(id) {
        for (let index = 0; index < options.length; ++index) {
            if (options[index].id === id)
                return index
        }
        return -1
    }

    function option(id) {
        const index = optionIndex(id)
        return index >= 0 ? options[index] : null
    }

    Label {
        text: qsTr("Input modifier passes")
        font.weight: Font.Medium
    }

    Repeater {
        id: passRepeater
        model: passModel

        delegate: Rectangle {
            id: passDelegate

            required property int index
            required property string passId
            required property var passSettings

            readonly property int optionCount: passTypeCombo.count
            readonly property string selectedId:
                passTypeCombo.currentValue.toString()
            readonly property bool settingsLoaded:
                passSettingsLoader.status === Loader.Ready
                && passSettingsLoader.item !== null
            readonly property string settingsObjectName:
                settingsLoaded ? passSettingsLoader.item.objectName : ""
            readonly property var settingsItem: passSettingsLoader.item
            readonly property bool slotStyled: passTypeCombo.slotStyled

            objectName: "modifierPass" + index
            Layout.fillWidth: true
            implicitHeight: passLayout.implicitHeight + 16
            radius: 8
            color: "#f3f5f1"
            border.width: 1
            border.color: "#cbd1c8"

            ColumnLayout {
                id: passLayout
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Label {
                        text: qsTr("Pass %1").arg(index + 1)
                        font.weight: Font.DemiBold
                    }

                    StyledComboBox {
                        id: passTypeCombo
                        objectName: "modifierPassCombo" + index
                        Layout.fillWidth: true
                        model: root.options
                        textRole: "label"
                        valueRole: "id"
                        currentIndex: root.optionIndex(passId)
                        enabled: !root.controller.running
                        onActivated: selectedIndex =>
                            root.controller.setModifierPassId(
                                passDelegate.index,
                                valueAt(selectedIndex).toString())
                    }

                    ToolButton {
                        text: "↑"
                        enabled: index > 0 && !root.controller.running
                        onClicked:
                            root.controller.moveModifierPass(index, index - 1)
                    }
                    ToolButton {
                        text: "↓"
                        enabled: index + 1 < root.passes.length
                                 && !root.controller.running
                        onClicked:
                            root.controller.moveModifierPass(index, index + 1)
                    }
                    ToolButton {
                        text: "×"
                        enabled: !root.controller.running
                        onClicked: root.controller.removeModifierPass(index)
                    }
                }

                Loader {
                    id: passSettingsLoader
                    Layout.fillWidth: true
                    readonly property var descriptor: root.option(passId)
                    active: descriptor !== null
                    source: active ? descriptor.settingsComponent : ""
                    onLoaded: {
                        if (!item)
                            return
                        item.settings = passSettings
                        item.running = root.controller.running
                        item.updateSetting = function(key, value) {
                            root.controller.setModifierPassSetting(
                                index, key, value)
                        }
                    }

                    onDescriptorChanged: {
                        if (item)
                            item.settings = passSettings
                    }
                }

            }

            onPassSettingsChanged: {
                if (passSettingsLoader.item)
                    passSettingsLoader.item.settings = passSettings
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        StyledComboBox {
            id: addModifierCombo
            objectName: "addModifierCombo"
            Layout.fillWidth: true
            model: root.options
            textRole: "label"
            valueRole: "id"
            enabled: !root.controller.running
        }

        Button {
            objectName: "addModifierButton"
            text: qsTr("Add pass")
            enabled: addModifierCombo.currentIndex >= 0
                     && !root.controller.running
            onClicked:
                root.controller.addModifierPass(
                    addModifierCombo.valueAt(
                        addModifierCombo.currentIndex).toString())
        }
    }
}
