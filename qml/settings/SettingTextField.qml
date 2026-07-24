import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root

    property string label
    property string value
    property bool running: false
    property string fieldObjectName: ""
    signal edited(string value)

    Layout.fillWidth: true
    spacing: 12

    Label {
        Layout.fillWidth: true
        text: root.label
        wrapMode: Text.WordWrap
    }

    TextField {
        objectName: root.fieldObjectName
        Layout.preferredWidth: 126
        horizontalAlignment: TextInput.AlignRight
        text: root.value
        enabled: !root.running
        selectByMouse: true
        onEditingFinished: {
            if (text !== root.value)
                root.edited(text)
        }
    }
}
