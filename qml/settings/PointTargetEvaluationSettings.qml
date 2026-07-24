import QtQuick
import QtQuick.Layouts

ColumnLayout {
    objectName: "pointTargetEvaluationSettings"
    property var controller

    Layout.fillWidth: true
    spacing: 8

    TimeWindowSettings {
        settings: controller.evaluationTargetSettings
        updateSetting: controller.setEvaluationTargetSetting
        running: controller.running
    }

    Vector3Settings {
        title: qsTr("Target point")
        settings: controller.evaluationTargetSettings
        updateSetting: controller.setEvaluationTargetSetting
        running: controller.running
    }
}
