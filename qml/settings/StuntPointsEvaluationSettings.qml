import QtQuick
import QtQuick.Layouts

ColumnLayout {
    objectName: "stuntPointsEvaluationSettings"
    property var controller
    readonly property var settings: controller.evaluationTargetSettings

    Layout.fillWidth: true
    spacing: 8

    SettingTextField {
        fieldObjectName: "stuntPointsTimeField"
        label: qsTr("Target time (ms)")
        value: parent.settings["targetTimeMs"] ?? ""
        running: controller.running
        dragStep: 10
        minimum: 0
        onEdited: value =>
            controller.setEvaluationTargetSetting("targetTimeMs", value)
    }
}
