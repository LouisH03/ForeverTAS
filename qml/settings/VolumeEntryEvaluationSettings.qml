import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    objectName: "volumeEntryEvaluationSettings"
    property var controller
    property var viewer
    readonly property var cuboids: controller.cuboidTargets
    readonly property var selected: cuboids.selectedTarget
    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        StyledComboBox {
            id: targetSelector

            objectName: "cuboidTargetSelector"
            Layout.fillWidth: true
            model: root.cuboids.targets
            textRole: "name"
            valueRole: "id"
            currentIndex: root.cuboids.selectedIndex
            enabled: !root.controller.running
            onActivated: index => root.cuboids.selectTarget(index)
        }

        Button {
            objectName: "addCuboidButton"
            text: "+"
            enabled: !root.controller.running
                     && root.cuboids.count < root.cuboids.maximumCount
            Layout.preferredWidth: 38
            onClicked: {
                const position = root.viewer && root.viewer.loaded
                                 ? root.viewer.carPosition
                                 : root.selected.center
                root.cuboids.addTarget(position.x,
                                       position.y,
                                       position.z)
            }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Place cuboid")
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Button {
            objectName: "duplicateCuboidButton"
            Layout.fillWidth: true
            text: qsTr("Duplicate")
            enabled: !root.controller.running
                     && root.cuboids.count < root.cuboids.maximumCount
            onClicked: root.cuboids.duplicateSelected()
        }

        Button {
            objectName: "focusCuboidButton"
            Layout.fillWidth: true
            text: qsTr("Focus")
            enabled: root.cuboids.count > 0
            onClicked: root.controller.focusSelectedCuboid()
        }

        Button {
            objectName: "removeCuboidButton"
            Layout.fillWidth: true
            text: qsTr("Remove")
            enabled: !root.controller.running && root.cuboids.count > 1
            onClicked:
                root.cuboids.removeTarget(root.cuboids.selectedIndex)
        }
    }

    TextField {
        id: targetName

        objectName: "cuboidNameField"
        Layout.fillWidth: true
        text: root.selected.name ?? ""
        enabled: !root.controller.running
        selectByMouse: true
        maximumLength: 80
        placeholderText: qsTr("Cuboid name")
        onEditingFinished: {
            root.cuboids.setName(root.cuboids.selectedIndex, text)
            text = root.cuboids.selectedTarget.name
        }
    }

    Vector3Settings {
        objectName: "cuboidCenterSettings"
        title: qsTr("Center")
        settings: root.selected
        running: root.controller.running
        xKey: "centerX"
        yKey: "centerY"
        zKey: "centerZ"
        updateSetting: (key, value) => {
            const axis = key.slice(-1).toLowerCase()
            root.cuboids.setCenterComponent(
                root.cuboids.selectedIndex, axis, value)
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
            const axis = key.slice(-1).toLowerCase()
            root.cuboids.setSizeComponent(
                root.cuboids.selectedIndex, axis, value)
        }
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("Active brute-force target")
        color: "#4f6f58"
        font.pixelSize: 11
        font.weight: Font.Medium
    }
}
