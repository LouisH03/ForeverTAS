import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    objectName: "basicBruteForceSearchSettings"
    property var controller
    readonly property var settings: controller.searchAlgorithmSettings

    Layout.fillWidth: true

    SettingSwitch {
        objectName: "autoPromoteBestSwitch"
        label: qsTr("Promote each best result to baseline")
        checked: root.settings["autoPromoteBest"] === "true"
        running: root.controller.running
        onToggled: checked =>
            root.controller.setSearchAlgorithmSetting(
                "autoPromoteBest", checked ? "true" : "false")
    }
}
