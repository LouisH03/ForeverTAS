import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    required property var controller

    width: 1180
    height: 720
    minimumWidth: 900
    minimumHeight: 580
    visible: true
    title: qsTr("ForeverTAS")
    color: "#eceeeb"

    palette {
        window: "#eceeeb"
        windowText: "#202421"
        base: "#ffffff"
        alternateBase: "#f4f5f2"
        text: "#202421"
        button: "#e1e5df"
        buttonText: "#202421"
        highlight: "#26734d"
        highlightedText: "#ffffff"
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Rectangle {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 500
            color: "#181b19"

            Label {
                anchors.centerIn: parent
                text: qsTr("Race Viewer")
                color: "#d9ded9"
                font.pixelSize: 28
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            SplitView.preferredWidth: 390
            SplitView.minimumWidth: 340
            SplitView.maximumWidth: 480
            color: "#f4f5f2"

            ScrollView {
                id: settingsScroll
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    width: settingsScroll.availableWidth
                    spacing: 14

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 12
                    }

                    Label {
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        text: qsTr("Search Settings")
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        spacing: 6

                        Label {
                            text: qsTr("Packs directory")
                            font.weight: Font.Medium
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            TextField {
                                Layout.fillWidth: true
                                text: window.controller.packsDirectory
                                enabled: !window.controller.running
                                placeholderText: qsTr("Select installed Packs directory")
                                selectByMouse: true
                                onTextEdited:
                                    window.controller.packsDirectory = text
                            }

                            Button {
                                text: qsTr("Browse")
                                enabled: !window.controller.running
                                onClicked:
                                    window.controller.browseForPacksDirectory()
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        spacing: 6

                        Label {
                            text: qsTr("Replay")
                            font.weight: Font.Medium
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            TextField {
                                Layout.fillWidth: true
                                text: window.controller.replayPath
                                enabled: !window.controller.running
                                placeholderText: qsTr("Select replay file")
                                selectByMouse: true
                                onTextEdited: window.controller.replayPath = text
                            }

                            Button {
                                text: qsTr("Browse")
                                enabled: !window.controller.running
                                onClicked:
                                    window.controller.browseForReplay()
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        color: "#d3d8d1"
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 9

                        Label {
                            text: qsTr("Minimum mutation (ms)")
                        }
                        TextField {
                            Layout.fillWidth: true
                            horizontalAlignment: TextInput.AlignRight
                            text: window.controller.minMutateMs
                            enabled: !window.controller.running
                            selectByMouse: true
                            validator: RegularExpressionValidator {
                                regularExpression: /[0-9]*/
                            }
                            onTextEdited: window.controller.minMutateMs = text
                        }

                        Label {
                            text: qsTr("Maximum mutation (ms)")
                        }
                        TextField {
                            Layout.fillWidth: true
                            horizontalAlignment: TextInput.AlignRight
                            text: window.controller.maxMutateMs
                            enabled: !window.controller.running
                            selectByMouse: true
                            validator: RegularExpressionValidator {
                                regularExpression: /[0-9]*/
                            }
                            onTextEdited: window.controller.maxMutateMs = text
                        }

                        Label {
                            text: qsTr("Minimum evaluation (ms)")
                        }
                        TextField {
                            Layout.fillWidth: true
                            horizontalAlignment: TextInput.AlignRight
                            text: window.controller.minEvalTimeMs
                            enabled: !window.controller.running
                            selectByMouse: true
                            validator: RegularExpressionValidator {
                                regularExpression: /[0-9]*/
                            }
                            onTextEdited: window.controller.minEvalTimeMs = text
                        }

                        Label {
                            text: qsTr("Maximum evaluation (ms)")
                        }
                        TextField {
                            Layout.fillWidth: true
                            horizontalAlignment: TextInput.AlignRight
                            text: window.controller.maxEvalTimeMs
                            enabled: !window.controller.running
                            selectByMouse: true
                            validator: RegularExpressionValidator {
                                regularExpression: /[0-9]*/
                            }
                            onTextEdited: window.controller.maxEvalTimeMs = text
                        }

                        Label {
                            text: qsTr("Attempt count")
                        }
                        TextField {
                            Layout.fillWidth: true
                            horizontalAlignment: TextInput.AlignRight
                            text: window.controller.attemptCount
                            enabled: !window.controller.running
                            selectByMouse: true
                            validator: RegularExpressionValidator {
                                regularExpression: /[0-9]*/
                            }
                            onTextEdited: window.controller.attemptCount = text
                        }

                        Label {
                            text: qsTr("Mutation seed")
                        }
                        TextField {
                            Layout.fillWidth: true
                            horizontalAlignment: TextInput.AlignRight
                            text: window.controller.mutationSeed
                            enabled: !window.controller.running
                            selectByMouse: true
                            validator: RegularExpressionValidator {
                                regularExpression: /[0-9]*/
                            }
                            onTextEdited: window.controller.mutationSeed = text
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        visible: text.length > 0 && !window.controller.running
                        text: window.controller.validationMessage
                        color: "#a23434"
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        spacing: 8

                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Start")
                            highlighted: true
                            enabled: window.controller.canStart
                            onClicked: window.controller.startSearch()
                        }

                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Cancel")
                            enabled: window.controller.running
                                     && !window.controller.cancelling
                            onClicked: window.controller.cancelSearch()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        color: "#d3d8d1"
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: window.controller.statusText
                            font.weight: Font.Medium
                            wrapMode: Text.WordWrap
                        }

                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            value: window.controller.progressValue
                            indeterminate:
                                window.controller.progressIndeterminate
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: window.controller.resultText
                            wrapMode: Text.WordWrap
                            color: window.controller.statusText
                                           === qsTr("Search failed")
                                   ? "#a23434"
                                   : "#3c443f"
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 16
                    }
                }
            }
        }
    }
}
