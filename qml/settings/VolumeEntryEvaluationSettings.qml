import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".." as ThemeControls

ColumnLayout {
    id: root

    objectName: "volumeEntryEvaluationSettings"
    property var controller
    property var viewer
    readonly property bool customActive:
        controller.evaluationTargetId === "custom-volume-entry-time"
    readonly property var activeModel:
        customActive ? controller.customVolumeTargets
                     : controller.cuboidTargets
    readonly property var selected: activeModel.selectedTarget
    readonly property var targetOptions: {
        let result = []
        const cuboids = controller.cuboidTargets.targets
        for (let index = 0; index < cuboids.length; ++index) {
            result.push({
                            "name": cuboids[index].name,
                            "kind": "cuboid",
                            "index": index
                        })
        }
        const customVolumes = controller.customVolumeTargets.targets
        for (let index = 0; index < customVolumes.length; ++index) {
            result.push({
                            "name": customVolumes[index].name,
                            "kind": "custom",
                            "index": index
                        })
        }
        return result
    }
    readonly property int selectedOptionIndex:
        customActive
        ? controller.cuboidTargets.count
          + controller.customVolumeTargets.selectedIndex
        : controller.cuboidTargets.selectedIndex
    spacing: 8

    function synchronizeTargetSelector() {
        if (!controller)
            return
        targetSelector.model = targetOptions
        targetSelector.currentIndex = selectedOptionIndex
    }

    onControllerChanged: Qt.callLater(synchronizeTargetSelector)
    onTargetOptionsChanged: Qt.callLater(synchronizeTargetSelector)
    onSelectedOptionIndexChanged: Qt.callLater(synchronizeTargetSelector)

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        StyledComboBox {
            id: targetSelector

            objectName: "shapeTargetSelector"
            Layout.fillWidth: true
            model: root.targetOptions
            textRole: "name"
            currentIndex: root.selectedOptionIndex
            enabled: !root.controller.running
                     && !root.controller.customVolumeDrawing
            onActivated: index => {
                const option = root.targetOptions[index]
                if (option.kind === "custom") {
                    root.controller.customVolumeTargets.selectTarget(
                        option.index)
                    root.controller.evaluationTargetId =
                        "custom-volume-entry-time"
                } else {
                    root.controller.cuboidTargets.selectTarget(option.index)
                    root.controller.evaluationTargetId = "volume-entry-time"
                }
            }
        }

        ThemeControls.ThemedButton {
            id: addShapeButton

            objectName: "addShapeTargetButton"
            text: "+"
            enabled: !root.controller.running
                     && !root.controller.customVolumeDrawing
                     && (root.controller.cuboidTargets.count
                         < root.controller.cuboidTargets.maximumCount
                         || root.controller.customVolumeTargets.count
                            < root.controller.customVolumeTargets.maximumCount)
            Layout.preferredWidth: 38
            onClicked: addShapeMenu.open()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Add shape target")

            Menu {
                id: addShapeMenu

                ThemeControls.ThemedMenuItem {
                    text: qsTr("Cuboid")
                    enabled:
                        root.controller.cuboidTargets.count
                        < root.controller.cuboidTargets.maximumCount
                    onTriggered: {
                        const position = root.viewer && root.viewer.loaded
                                         ? root.viewer.carPosition
                                         : root.selected.origin
                                           ?? root.selected.center
                        root.controller.cuboidTargets.addTarget(
                            position.x, position.y, position.z)
                        root.controller.evaluationTargetId =
                            "volume-entry-time"
                    }
                }
                ThemeControls.ThemedMenuItem {
                    text: qsTr("Polygon volume")
                    enabled:
                        root.controller.customVolumeTargets.count
                        < root.controller.customVolumeTargets.maximumCount
                    onTriggered: {
                        const position = root.viewer && root.viewer.loaded
                                         ? root.viewer.carPosition
                                         : root.selected.origin
                                           ?? root.selected.center
                        root.controller.customVolumeTargets.addTarget(
                            "xz", position.x, position.y, position.z)
                        root.controller.evaluationTargetId =
                            "custom-volume-entry-time"
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        ThemeControls.ThemedButton {
            objectName: "duplicateShapeTargetButton"
            Layout.fillWidth: true
            text: qsTr("Duplicate")
            enabled: !root.controller.running
                     && !root.controller.customVolumeDrawing
                     && root.activeModel.count
                        < root.activeModel.maximumCount
            onClicked: root.activeModel.duplicateSelected()
        }

        ThemeControls.ThemedButton {
            objectName: "focusShapeTargetButton"
            Layout.fillWidth: true
            text: qsTr("Focus")
            enabled: root.activeModel.count > 0
            onClicked: {
                if (root.customActive)
                    root.controller.focusSelectedCustomVolume()
                else
                    root.controller.focusSelectedCuboid()
            }
        }

        ThemeControls.ThemedButton {
            objectName: "removeShapeTargetButton"
            Layout.fillWidth: true
            text: qsTr("Remove")
            enabled: !root.controller.running
                     && !root.controller.customVolumeDrawing
                     && root.activeModel.count > 1
            onClicked:
                root.activeModel.removeTarget(root.activeModel.selectedIndex)
        }
    }

    TextField {
        id: targetName

        objectName: "shapeTargetNameField"
        Layout.fillWidth: true
        text: root.selected.name ?? ""
        enabled: !root.controller.running
                 && !root.controller.customVolumeDrawing
        selectByMouse: true
        maximumLength: 80
        placeholderText: qsTr("Target name")
        onEditingFinished: {
            root.activeModel.setName(root.activeModel.selectedIndex, text)
            text = root.activeModel.selectedTarget.name
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: !root.customActive
        spacing: 8

        Vector3Settings {
            objectName: "cuboidCenterSettings"
            title: qsTr("Center")
            settings: root.selected
            running: root.controller.running
            xKey: "centerX"
            yKey: "centerY"
            zKey: "centerZ"
            updateSetting: (key, value) => {
                root.controller.cuboidTargets.setCenterComponent(
                    root.controller.cuboidTargets.selectedIndex,
                    key.slice(-1).toLowerCase(),
                    value)
            }
        }

        Vector3Settings {
            objectName: "cuboidSizeSettings"
            title: qsTr("Size")
            settings: root.selected
            running: root.controller.running
            xKey: "sizeX"
            yKey: "sizeY"
            zKey: "sizeZ"
            minimum: 0.001
            updateSetting: (key, value) => {
                root.controller.cuboidTargets.setSizeComponent(
                    root.controller.cuboidTargets.selectedIndex,
                    key.slice(-1).toLowerCase(),
                    value)
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: root.customActive
        spacing: 8

        SettingCombo {
            objectName: "customVolumePlaneSetting"
            label: qsTr("Drawing plane")
            value: root.selected.plane ?? "xz"
            options: [
                { "label": qsTr("Horizontal (XZ)"), "value": "xz" },
                { "label": qsTr("Vertical (XY)"), "value": "xy" },
                { "label": qsTr("Vertical (YZ)"), "value": "yz" }
            ]
            running: root.controller.running
                     || root.controller.customVolumeDrawing
            onSelected: value =>
                root.controller.customVolumeTargets.setPlane(
                    root.controller.customVolumeTargets.selectedIndex,
                    value)
        }

        Vector3Settings {
            objectName: "customVolumeOriginSettings"
            title: qsTr("Plane origin")
            settings: root.selected
            running: root.controller.running
                     || root.controller.customVolumeDrawing
            minimum: -10000000
            maximum: 10000000
            xKey: "originX"
            yKey: "originY"
            zKey: "originZ"
            updateSetting: (key, value) => {
                root.controller.customVolumeTargets.setOriginComponent(
                    root.controller.customVolumeTargets.selectedIndex,
                    key.slice(-1).toLowerCase(),
                    value)
            }
        }

        SettingTextField {
            objectName: "customVolumeDepthSetting"
            fieldObjectName: "customVolumeDepthField"
            label: qsTr("Extrusion depth")
            value: root.selected.depth ?? ""
            running: root.controller.running
                     || root.controller.customVolumeDrawing
            integer: false
            decimals: 3
            dragStep: 0.1
            minimum: 0.001
            maximum: 10000000
            onEdited: value =>
                root.controller.customVolumeTargets.setDepth(
                    root.controller.customVolumeTargets.selectedIndex,
                    value)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ThemeControls.ThemedButton {
                objectName: "drawCustomVolumeButton"
                Layout.fillWidth: true
                text: root.controller.customVolumeDrawing
                      ? qsTr("Finish polygon") : qsTr("Draw polygon")
                enabled: !root.controller.running
                         && root.viewer
                         && root.viewer.loaded
                         && (!root.controller.customVolumeDrawing
                             || root.selected.valid)
                highlighted: root.controller.customVolumeDrawing
                onClicked: {
                    if (root.controller.customVolumeDrawing)
                        root.controller.finishCustomVolumeDrawing()
                    else
                        root.controller.beginCustomVolumeDrawing()
                }
            }

            ThemeControls.ThemedButton {
                objectName: "cancelCustomVolumeDrawingButton"
                visible: root.controller.customVolumeDrawing
                text: qsTr("Cancel")
                onClicked: root.controller.cancelCustomVolumeDrawing()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.controller.customVolumeDrawing
            text: root.selected.vertexCount < 3
                  ? qsTr("%1 of 3 minimum vertices")
                        .arg(root.selected.vertexCount)
                  : qsTr("%1 vertices").arg(root.selected.vertexCount)
            color: root.selected.valid
                   ? ThemeControls.AppTheme.success
                   : ThemeControls.AppTheme.warning
            font.pixelSize: 11
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Polygon vertices")
            font.weight: Font.Medium
        }

        Repeater {
            model: root.selected.vertices ?? []

            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 6

                Label {
                    Layout.preferredWidth: 20
                    text: modelData.index + 1
                    color: ThemeControls.AppTheme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                }
                ScrubNumberField {
                    Layout.fillWidth: true
                    scrubLabel: "U"
                    value: modelData.u
                    enabled: !root.controller.running
                             && !root.controller.customVolumeDrawing
                    integer: false
                    decimals: 3
                    dragStep: 0.1
                    minimum: -10000000
                    maximum: 10000000
                    onEdited: value =>
                        root.controller.customVolumeTargets.setVertex(
                            root.controller.customVolumeTargets.selectedIndex,
                            modelData.index,
                            "u",
                            value)
                }
                ScrubNumberField {
                    Layout.fillWidth: true
                    scrubLabel: "V"
                    value: modelData.v
                    enabled: !root.controller.running
                             && !root.controller.customVolumeDrawing
                    integer: false
                    decimals: 3
                    dragStep: 0.1
                    minimum: -10000000
                    maximum: 10000000
                    onEdited: value =>
                        root.controller.customVolumeTargets.setVertex(
                            root.controller.customVolumeTargets.selectedIndex,
                            modelData.index,
                            "v",
                            value)
                }
                ThemeControls.ThemedToolButton {
                    text: "x"
                    enabled: !root.controller.running
                             && !root.controller.customVolumeDrawing
                             && root.selected.vertexCount > 3
                    onClicked:
                        root.controller.customVolumeTargets.removeVertex(
                            root.controller.customVolumeTargets.selectedIndex,
                            modelData.index)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Remove vertex")
                }
            }
        }
    }

    Label {
        Layout.fillWidth: true
        text: root.customActive
              ? qsTr("Active custom-volume search target")
              : qsTr("Active cuboid search target")
        color: ThemeControls.AppTheme.success
        font.pixelSize: 11
        font.weight: Font.Medium
    }
}
