import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string title
    property string description
    default property alias sectionContent: contentColumn.data

    Layout.fillWidth: true
    implicitHeight: sectionLayout.implicitHeight + 28
    radius: 10
    color: "#f7f8f5"
    border.width: 1
    border.color: "#d4dad1"

    ColumnLayout {
        id: sectionLayout
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: root.title
            color: "#20251f"
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.description
            color: "#667064"
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            id: contentColumn
            Layout.fillWidth: true
            spacing: 8
        }
    }
}
