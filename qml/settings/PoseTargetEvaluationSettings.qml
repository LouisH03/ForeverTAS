import QtQuick
import QtQuick.Layouts

ColumnLayout {
    objectName: "poseTargetEvaluationSettings"
    property var controller
    readonly property var settings: controller.evaluationTargetSettings

    Layout.fillWidth: true
    spacing: 8

    TimeWindowSettings {
        settings: parent.settings
        updateSetting: controller.setEvaluationTargetSetting
        running: controller.running
    }

    Vector3Settings {
        title: qsTr("Target position")
        settings: parent.settings
        updateSetting: controller.setEvaluationTargetSetting
        running: controller.running
    }

    Vector3Settings {
        title: qsTr("Target rotation (degrees)")
        settings: parent.settings
        updateSetting: controller.setEvaluationTargetSetting
        running: controller.running
        xKey: "yawDegrees"
        yKey: "pitchDegrees"
        zKey: "rollDegrees"
    }

    SettingSlider {
        sliderObjectName: "rotationWeightSlider"
        label: qsTr("Rotation weight (%)")
        value: parent.settings["rotationWeightPercent"] ?? ""
        running: controller.running
        from: 0
        to: 100
        stepSize: 1
        suffix: "%"
        onEdited: value =>
            controller.setEvaluationTargetSetting(
                "rotationWeightPercent", value)
    }
}
