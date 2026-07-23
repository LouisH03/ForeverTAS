import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridLayout {
    objectName: "randomSteeringMutationSettings"
    property var controller

    columns: 2
    columnSpacing: 12
    rowSpacing: 9

    Label {
        text: qsTr("Mutation seed")
    }
    TextField {
        Layout.fillWidth: true
        horizontalAlignment: TextInput.AlignRight
        text: controller.mutationAlgorithmSettings["seed"] ?? ""
        enabled: !controller.running
        selectByMouse: true
        validator: RegularExpressionValidator {
            regularExpression: /[0-9]*/
        }
        onTextEdited:
            controller.setMutationAlgorithmSetting("seed", text)
    }
}
