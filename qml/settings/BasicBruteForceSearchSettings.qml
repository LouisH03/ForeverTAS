import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridLayout {
    objectName: "basicBruteForceSearchSettings"
    property var controller

    columns: 2
    columnSpacing: 12
    rowSpacing: 9

    Label {
        text: qsTr("Attempt count")
    }
    TextField {
        Layout.fillWidth: true
        horizontalAlignment: TextInput.AlignRight
        text: controller.searchAlgorithmSettings["attemptCount"] ?? ""
        enabled: !controller.running
        selectByMouse: true
        validator: RegularExpressionValidator {
            regularExpression: /[0-9]*/
        }
        onTextEdited:
            controller.setSearchAlgorithmSetting("attemptCount", text)
    }
}
