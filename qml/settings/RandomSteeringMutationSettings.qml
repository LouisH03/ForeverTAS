import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root

    objectName: "randomSteeringMutationSettings"
    property var settings: ({})
    property var updateSetting
    property bool running: false

    Layout.fillWidth: true
    spacing: 8

    TimeWindowSettings {
        settings: root.settings
        updateSetting: root.updateSetting
        running: root.running
    }

    SettingTextField {
        label: qsTr("Seed")
        value: root.settings["seed"] ?? ""
        running: root.running
        onEdited: value => root.updateSetting("seed", value)
    }
}
