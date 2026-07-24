import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root

    property string label
    property bool checked: false
    property bool running: false
    signal toggled(bool checked)

    Layout.fillWidth: true
    spacing: 12

    Label {
        Layout.fillWidth: true
        text: root.label
        wrapMode: Text.WordWrap
    }

    Switch {
        checked: root.checked
        enabled: !root.running
        onToggled: root.toggled(checked)
    }
}
