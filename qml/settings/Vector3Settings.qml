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

        TextField {
            Layout.fillWidth: true
            placeholderText: "X"
            text: root.settings[root.xKey] ?? ""
            enabled: !root.running
            horizontalAlignment: TextInput.AlignRight
            selectByMouse: true
            onTextEdited: root.updateSetting(root.xKey, text)
        }
        TextField {
            Layout.fillWidth: true
            placeholderText: "Y"
            text: root.settings[root.yKey] ?? ""
            enabled: !root.running
            horizontalAlignment: TextInput.AlignRight
            selectByMouse: true
            onTextEdited: root.updateSetting(root.yKey, text)
        }
        TextField {
            Layout.fillWidth: true
            placeholderText: "Z"
            text: root.settings[root.zKey] ?? ""
            enabled: !root.running
            horizontalAlignment: TextInput.AlignRight
            selectByMouse: true
            onTextEdited: root.updateSetting(root.zKey, text)
        }
    }
}
