import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root

    property var settings: ({})
    property var updateSetting
    property bool running: false
    property string minimumKey: "minTimeMs"
    property string maximumKey: "maxTimeMs"
    property string minimumLabel: qsTr("Minimum time (ms)")
    property string maximumLabel: qsTr("Maximum time (ms)")

    Layout.fillWidth: true
    spacing: 6

    SettingTextField {
        fieldObjectName: "minimumTimeField"
        label: root.minimumLabel
        value: root.settings[root.minimumKey] ?? ""
        running: root.running
        dragStep: 10
        minimum: 0
        onEdited: value => root.updateSetting(root.minimumKey, value)
    }

    SettingTextField {
        fieldObjectName: "maximumTimeField"
        label: root.maximumLabel
        value: root.settings[root.maximumKey] ?? ""
        running: root.running
        dragStep: 10
        minimum: 0
        onEdited: value => root.updateSetting(root.maximumKey, value)
    }
}
