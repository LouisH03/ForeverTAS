import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property string title
    property var settings: ({})
    property var updateSetting
    property bool running: false
    property string xKey: "x"
    property string yKey: "y"
    property string zKey: "z"
    property real dragStep: 0.1
    property int decimals: 3
    property real minimum: -Number.MAX_VALUE
    property real maximum: Number.MAX_VALUE

    Layout.fillWidth: true
    spacing: 4

    Label {
        visible: root.title.length > 0
        text: root.title
        font.weight: Font.Medium
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 3
        columnSpacing: 6

        ScrubNumberField {
            Layout.fillWidth: true
            scrubLabel: "X"
            value: root.settings[root.xKey] ?? ""
            enabled: !root.running
            integer: false
            dragStep: root.dragStep
            decimals: root.decimals
            minimum: root.minimum
            maximum: root.maximum
            onEdited: value => root.updateSetting(root.xKey, value)
        }
        ScrubNumberField {
            Layout.fillWidth: true
            scrubLabel: "Y"
            value: root.settings[root.yKey] ?? ""
            enabled: !root.running
            integer: false
            dragStep: root.dragStep
            decimals: root.decimals
            minimum: root.minimum
            maximum: root.maximum
            onEdited: value => root.updateSetting(root.yKey, value)
        }
        ScrubNumberField {
            Layout.fillWidth: true
            scrubLabel: "Z"
            value: root.settings[root.zKey] ?? ""
            enabled: !root.running
            integer: false
            dragStep: root.dragStep
            decimals: root.decimals
            minimum: root.minimum
            maximum: root.maximum
            onEdited: value => root.updateSetting(root.zKey, value)
        }
    }
}
