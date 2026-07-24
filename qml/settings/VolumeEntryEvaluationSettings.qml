import QtQuick
import QtQuick.Layouts

ColumnLayout {
    objectName: "volumeEntryEvaluationSettings"
    property var controller

    Layout.fillWidth: true
    spacing: 8

    Vector3Settings {
        title: qsTr("Cuboid center")
        settings: controller.evaluationTargetSettings
        updateSetting: controller.setEvaluationTargetSetting
        running: controller.running
        xKey: "centerX"
        yKey: "centerY"
        zKey: "centerZ"
    }

    Vector3Settings {
        title: qsTr("Cuboid size")
        settings: controller.evaluationTargetSettings
        updateSetting: controller.setEvaluationTargetSetting
        running: controller.running
        xKey: "sizeX"
        yKey: "sizeY"
        zKey: "sizeZ"
        minimum: 0.001
    }
}
