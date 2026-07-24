import QtQuick
import QtQuick.Layouts

ColumnLayout {
    objectName: "velocityEvaluationSettings"
    property var controller
    readonly property var settings: controller.evaluationTargetSettings

    Layout.fillWidth: true
    spacing: 8

    TimeWindowSettings {
        settings: parent.settings
        updateSetting: controller.setEvaluationTargetSetting
        running: controller.running
    }

    SettingCombo {
        label: qsTr("Velocity objective")
        options: [
            { label: qsTr("Total speed"), value: "total" },
            { label: qsTr("Projected direction"), value: "projected" }
        ]
        value: parent.settings["mode"] ?? "total"
        running: controller.running
        onSelected: value =>
            controller.setEvaluationTargetSetting("mode", value)
    }

    SettingSwitch {
        label: qsTr("Require direction alignment")
        checked: (parent.settings["alignmentEnabled"] ?? "false")
                 === "true"
        running: controller.running
        onToggled: checked =>
            controller.setEvaluationTargetSetting(
                "alignmentEnabled", checked ? "true" : "false")
    }

    Vector3Settings {
        title: qsTr("Direction")
        settings: parent.settings
        updateSetting: controller.setEvaluationTargetSetting
        running: controller.running
        xKey: "directionX"
        yKey: "directionY"
        zKey: "directionZ"
    }

    SettingTextField {
        label: qsTr("Minimum alignment (%)")
        value: parent.settings["minAlignmentPercent"] ?? ""
        running: controller.running
        onEdited: value =>
            controller.setEvaluationTargetSetting(
                "minAlignmentPercent", value)
    }
}
