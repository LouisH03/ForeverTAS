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
        text: qsTr("Minimum mutation (ms)")
    }
    TextField {
        Layout.fillWidth: true
        horizontalAlignment: TextInput.AlignRight
        text: controller.searchAlgorithmSettings["minMutateMs"] ?? ""
        enabled: !controller.running
        selectByMouse: true
        validator: RegularExpressionValidator {
            regularExpression: /[0-9]*/
        }
        onTextEdited:
            controller.setSearchAlgorithmSetting("minMutateMs", text)
    }

    Label {
        text: qsTr("Maximum mutation (ms)")
    }
    TextField {
        Layout.fillWidth: true
        horizontalAlignment: TextInput.AlignRight
        text: controller.searchAlgorithmSettings["maxMutateMs"] ?? ""
        enabled: !controller.running
        selectByMouse: true
        validator: RegularExpressionValidator {
            regularExpression: /[0-9]*/
        }
        onTextEdited:
            controller.setSearchAlgorithmSetting("maxMutateMs", text)
    }

    Label {
        text: qsTr("Minimum evaluation (ms)")
    }
    TextField {
        Layout.fillWidth: true
        horizontalAlignment: TextInput.AlignRight
        text: controller.searchAlgorithmSettings["minEvalTimeMs"] ?? ""
        enabled: !controller.running
        selectByMouse: true
        validator: RegularExpressionValidator {
            regularExpression: /[0-9]*/
        }
        onTextEdited:
            controller.setSearchAlgorithmSetting("minEvalTimeMs", text)
    }

    Label {
        text: qsTr("Maximum evaluation (ms)")
    }
    TextField {
        Layout.fillWidth: true
        horizontalAlignment: TextInput.AlignRight
        text: controller.searchAlgorithmSettings["maxEvalTimeMs"] ?? ""
        enabled: !controller.running
        selectByMouse: true
        validator: RegularExpressionValidator {
            regularExpression: /[0-9]*/
        }
        onTextEdited:
            controller.setSearchAlgorithmSetting("maxEvalTimeMs", text)
    }

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
