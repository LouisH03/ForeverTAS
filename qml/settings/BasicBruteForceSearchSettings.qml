import QtQuick
import QtQuick.Layouts

ColumnLayout {
    objectName: "basicBruteForceSearchSettings"
    property var controller

    Layout.fillWidth: true

    SettingTextField {
        fieldObjectName: "attemptCountField"
        label: qsTr("Attempt count")
        value: controller.searchAlgorithmSettings["attemptCount"] ?? ""
        running: controller.running
        minimum: 1
        onEdited: value =>
            controller.setSearchAlgorithmSetting("attemptCount", value)
    }
}
