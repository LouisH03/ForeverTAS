pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var viewer
    readonly property var debuggerModel: viewer.simulationDebugger
    property var editingLine: null
    property bool hasDraftEdit: false

    implicitHeight: 760

    Binding {
        target: root.debuggerModel
        property: "darkMode"
        value: AppTheme.dark
    }

    function commitActiveEdit() {
        if (root.editingLine)
            root.editingLine.commitEdit()
    }

    function cancelActiveEdit() {
        if (root.editingLine)
            root.editingLine.cancelEdit()
    }

    function revealExecutingLine() {
        Qt.callLater(function() {
            if (root.debuggerModel.selectedFilePath
                    === root.debuggerModel.activeFilePath
                    && root.debuggerModel.activeLine > 0) {
                codeList.forceLayout()
                const centeredY =
                    (root.debuggerModel.activeLine - 0.5) * 28
                    - codeList.height / 2
                codeList.contentY = Math.max(
                    0,
                    Math.min(centeredY,
                             Math.max(0,
                                      codeList.contentHeight
                                      - codeList.height)))
            }
        })
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Reference source")
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    color: AppTheme.text
                }

                Label {
                    Layout.fillWidth: true
                    text: root.debuggerModel.running
                          ? qsTr("native execution running")
                          : (root.debuggerModel.compiling
                             ? qsTr("compiling edited C++")
                             : qsTr("native execution paused"))
                    font.family: "monospace"
                    font.pixelSize: 10
                    color: AppTheme.textMuted
                    elide: Text.ElideRight
                }
            }

            Button {
                objectName: "restartLiveSimulationButton"
                text: qsTr("Restart")
                enabled: root.viewer.loaded && !root.viewer.loading
                         && root.debuggerModel.available
                onClicked: {
                    root.commitActiveEdit()
                    root.viewer.startSimulationDebugger()
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Restart the reference engine from tick zero")
            }

            Button {
                objectName: "resetLiveEditsButton"
                text: qsTr("Reset")
                enabled: root.debuggerModel.hasEdits || root.hasDraftEdit
                onClicked: {
                    root.cancelActiveEdit()
                    root.debuggerModel.resetEdits()
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Restore all in-memory source edits")
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.debuggerModel.statusText
            color: root.debuggerModel.editError.length > 0
                   ? AppTheme.error : AppTheme.textMuted
            wrapMode: Text.WordWrap
            font.pixelSize: 11
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 118
            color: AppTheme.surface
            border.width: 1
            border.color: AppTheme.border
            radius: 6
            clip: true

            ListView {
                id: sourceTree

                objectName: "simulationSourceTree"
                anchors.fill: parent
                anchors.margins: 3
                model: root.debuggerModel.fileEntries
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    id: sourceRow

                    required property var modelData

                    width: sourceTree.width
                    height: 25
                    leftPadding: 7 + sourceRow.modelData.depth * 13
                    rightPadding: 6
                    highlighted: false
                    onClicked: {
                        root.commitActiveEdit()
                        if (sourceRow.modelData.directory)
                            root.debuggerModel.toggleFolder(
                                sourceRow.modelData.path)
                        else
                            root.debuggerModel.selectFile(
                                sourceRow.modelData.path)
                    }

                    contentItem: RowLayout {
                        spacing: 5

                        Label {
                            text: sourceRow.modelData.directory
                                  ? (sourceRow.modelData.expanded ? "v" : ">")
                                  : "{}"
                            color: sourceRow.modelData.modified
                                   ? AppTheme.success : AppTheme.textMuted
                            font.family: "monospace"
                            font.pixelSize: 10
                        }

                        Label {
                            Layout.fillWidth: true
                            text: String(sourceRow.modelData.name || "")
                            color: sourceRow.modelData.modified
                                   ? AppTheme.success : AppTheme.text
                            font.weight: sourceRow.modelData.modified
                                         ? Font.DemiBold : Font.Normal
                            font.family: sourceRow.modelData.directory
                                         ? "sans-serif" : "monospace"
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                    }

                    background: Rectangle {
                        color: sourceRow.modelData.selected
                               ? AppTheme.selection : "transparent"
                        radius: 3
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: root.debuggerModel.selectedFilePath
                color: AppTheme.textMuted
                font.family: "monospace"
                font.pixelSize: 10
                elide: Text.ElideMiddle
            }

            Label {
                visible: root.debuggerModel.active
                text: qsTr("tick %1").arg(
                          root.debuggerModel.executionTick)
                color: AppTheme.info
                font.family: "monospace"
                font.pixelSize: 10
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 250
            color: AppTheme.codeSurface
            border.width: 1
            border.color: AppTheme.border
            radius: 6
            clip: true

            ListView {
                id: codeList

                objectName: "simulationCodeViewer"
                anchors.fill: parent
                anchors.margins: 1
                model: root.debuggerModel.lines
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ScrollBar.horizontal: ScrollBar {}

                delegate: Rectangle {
                    id: codeLine

                    required property var modelData
                    required property int index
                    property bool editing: false
                    function commitEdit() {
                        if (!codeLine.editing)
                            return
                        const committedText = liveEdit.text
                        codeLine.editing = false
                        if (root.editingLine === codeLine)
                            root.editingLine = null
                        root.debuggerModel.updateLine(
                            codeLine.modelData.number, committedText)
                        root.hasDraftEdit = false
                    }
                    function cancelEdit() {
                        if (!codeLine.editing)
                            return
                        liveEdit.text = codeLine.modelData.text
                        codeLine.editing = false
                        if (root.editingLine === codeLine)
                            root.editingLine = null
                        root.hasDraftEdit = false
                    }
                    readonly property bool draftModified:
                        codeLine.editing
                        ? liveEdit.text !== codeLine.modelData.original
                        : codeLine.modelData.modified

                    width: Math.max(codeList.width, codeRow.implicitWidth + 12)
                    height: 28
                    color: codeLine.modelData.active
                           ? AppTheme.codeActive
                           : (index % 2 === 0
                              ? AppTheme.codeSurface
                              : AppTheme.codeAlternate)

                    Rectangle {
                        anchors.left: parent.left
                        width: codeLine.draftModified ? 3 : 0
                        height: parent.height
                        color: AppTheme.success
                    }

                    RowLayout {
                        id: codeRow

                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4

                        Item {
                            Layout.preferredWidth: 19
                            Layout.preferredHeight: 24

                            Rectangle {
                                anchors.centerIn: parent
                                width: 9
                                height: 9
                                radius: 5
                                visible: codeLine.modelData.breakpoint
                                color: AppTheme.codeBreakpoint
                            }

                            TapHandler {
                                enabled: codeLine.modelData.editable
                                onTapped: root.debuggerModel.toggleBreakpoint(
                                              root.debuggerModel.selectedFilePath,
                                              codeLine.modelData.number)
                            }
                        }

                        Label {
                            Layout.preferredWidth: 23
                            text: codeLine.modelData.number
                            horizontalAlignment: Text.AlignRight
                            color: codeLine.modelData.active
                                   ? AppTheme.info : AppTheme.codeLineNumber
                            font.family: "monospace"
                            font.pixelSize: 10
                        }

                        TextField {
                            id: liveEdit

                            objectName: "liveCodeEditableLine"
                            visible: codeLine.editing
                            Layout.preferredWidth: Math.max(
                                                       255,
                                                       contentWidth + 18)
                            Layout.preferredHeight: 25
                            text: codeLine.modelData.text
                            selectByMouse: true
                            font.family: "monospace"
                            font.pixelSize: 11
                            color: codeLine.draftModified
                                   ? AppTheme.success : AppTheme.text
                            leftPadding: 5
                            rightPadding: 5
                            topPadding: 2
                            bottomPadding: 2
                            background: Rectangle {
                                color: parent.activeFocus
                                       ? AppTheme.surface : "transparent"
                                border.width: parent.activeFocus ? 1 : 0
                                border.color: AppTheme.focus
                                radius: 3
                            }
                            onTextChanged: {
                                if (codeLine.editing) {
                                    root.hasDraftEdit =
                                        liveEdit.text
                                        !== codeLine.modelData.original
                                }
                            }
                            onEditingFinished: codeLine.commitEdit()
                            Keys.onEscapePressed: codeLine.cancelEdit()
                        }

                        Text {
                            visible: !codeLine.editing
                            Layout.preferredWidth: Math.max(
                                                       255,
                                                       implicitWidth)
                            text: codeLine.modelData.highlighted
                            textFormat: Text.RichText
                            color: AppTheme.text
                            font.family: "monospace"
                            font.pixelSize: 11
                            elide: Text.ElideNone

                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onDoubleTapped: {
                                    if (codeLine.modelData.editable) {
                                        if (root.debuggerModel.running) {
                                            root.viewer.pause()
                                            return
                                        }
                                        root.commitActiveEdit()
                                        codeLine.editing = true
                                        root.editingLine = codeLine
                                        root.hasDraftEdit =
                                            liveEdit.text
                                            !== codeLine.modelData.original
                                        liveEdit.forceActiveFocus()
                                        liveEdit.selectAll()
                                    }
                                }
                            }
                        }

                        Label {
                            visible: codeLine.modelData.inlineValue.length > 0
                            text: codeLine.modelData.inlineValue
                            color: AppTheme.codeLineNumber
                            font.family: "monospace"
                            font.pixelSize: 9
                            leftPadding: 8
                        }
                    }
                }
            }
        }

        Label {
            visible: root.debuggerModel.editError.length > 0
            Layout.fillWidth: true
            text: root.debuggerModel.editError
            color: AppTheme.error
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Pinned variables")
            font.weight: Font.DemiBold
            font.pixelSize: 11
            color: AppTheme.textMuted
        }

        Flow {
            Layout.fillWidth: true
            spacing: 5

            Repeater {
                model: root.debuggerModel.pinnedVariables

                delegate: Button {
                    id: pinnedButton

                    required property var modelData
                    text: pinnedButton.modelData.name + "  "
                          + pinnedButton.modelData.value
                    width: Math.min(implicitWidth, root.width - 8)
                    font.family: "monospace"
                    font.pixelSize: 9
                    onClicked:
                        root.debuggerModel.togglePinned(
                            pinnedButton.modelData.name)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Unpin variable")

                    contentItem: Label {
                        text: pinnedButton.text
                        font: pinnedButton.font
                        color: pinnedButton.palette.buttonText
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }

            Label {
                visible: root.debuggerModel.pinnedVariables.length === 0
                text: qsTr("Select a variable below to pin it.")
                color: AppTheme.textFaint
                font.pixelSize: 10
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Variables")
            font.weight: Font.DemiBold
            font.pixelSize: 11
            color: AppTheme.textMuted
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 86
            color: AppTheme.surface
            border.width: 1
            border.color: AppTheme.border
            radius: 6
            clip: true

            GridView {
                id: variableGrid

                objectName: "simulationVariables"
                anchors.fill: parent
                anchors.margins: 3
                model: root.debuggerModel.variables
                cellWidth: Math.max(155, width / 2)
                cellHeight: 25
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    id: variableButton

                    required property var modelData

                    width: variableGrid.cellWidth
                    height: variableGrid.cellHeight
                    leftPadding: 6
                    rightPadding: 6
                    onClicked:
                        root.debuggerModel.togglePinned(
                            variableButton.modelData.name)

                    contentItem: RowLayout {
                        spacing: 4

                        Label {
                            Layout.fillWidth: true
                            text: variableButton.modelData.name
                            color: variableButton.modelData.pinned
                                   ? AppTheme.success : AppTheme.textMuted
                            font.family: "monospace"
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.preferredWidth:
                                Math.max(54, variableGrid.cellWidth * 0.42)
                            text: variableButton.modelData.value
                            color: AppTheme.text
                            font.family: "monospace"
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: root.debuggerModel.variables.length === 0
                    text: root.debuggerModel.active
                          ? qsTr("Pause on a source line to inspect variables.")
                          : qsTr("Variables appear during a debug session.")
                    color: AppTheme.textFaint
                    font.pixelSize: 10
                }
            }
        }
    }

    Connections {
        target: root.debuggerModel

        function onExecutionChanged() {
            root.revealExecutingLine()
        }

        function onLinesChanged() {
            root.revealExecutingLine()
        }
    }

    onVisibleChanged: {
        if (!visible)
            root.commitActiveEdit()
    }
}
