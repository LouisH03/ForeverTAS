import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ForeverTAS.Viewer 1.0

Item {
    id: root

    required property var model
    property bool available: false
    readonly property real boardTop: 52
    property int editingIndex: -2
    property real pendingTextX: 0
    property real pendingTextY: 0

    function beginTextEntry(index, value, normalizedX, normalizedY) {
        editingIndex = index
        pendingTextX = normalizedX
        pendingTextY = normalizedY
        textEditor.text = value
        textEditor.visible = true
        textEditor.x = Math.max(8, Math.min(
                                    boardArea.width - textEditor.width - 8,
                                    normalizedX * boardArea.width))
        textEditor.y = Math.max(8, Math.min(
                                    boardArea.height - textEditor.height - 8,
                                    normalizedY * boardArea.height))
        Qt.callLater(function() {
            textEditor.forceActiveFocus()
            textEditor.selectAll()
        })
    }

    function commitTextEntry() {
        if (!textEditor.visible)
            return
        const value = textEditor.text.trim()
        if (value.length > 0) {
            if (editingIndex >= 0)
                model.setText(editingIndex, value)
            else
                model.addText(pendingTextX, pendingTextY, value)
        }
        cancelTextEntry()
    }

    function cancelTextEntry() {
        textEditor.visible = false
        editingIndex = -2
        boardArea.forceActiveFocus()
    }

    onAvailableChanged: {
        if (!available && model.active)
            model.active = false
    }

    Connections {
        target: root.model

        function onActiveChanged() {
            if (!root.model.active) {
                root.cancelTextEntry()
                colorPopup.close()
            }
        }
    }

    Item {
        id: boardArea
        objectName: "whiteboardBoardArea"
        anchors.top: parent.top
        anchors.topMargin: root.boardTop
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true

        MouseArea {
            id: drawingInput
            objectName: "whiteboardDrawingInput"
            anchors.fill: parent
            z: 0
            enabled: root.model.active
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true

            onPressed: mouse => {
                if (root.model.tool === "text") {
                    root.beginTextEntry(
                                -1,
                                "",
                                mouse.x / Math.max(1, width),
                                mouse.y / Math.max(1, height))
                } else if (root.model.tool === "select"
                           || root.model.tool === "eraser") {
                    root.model.clearSelection()
                } else {
                    root.model.beginItem(
                                mouse.x / Math.max(1, width),
                                mouse.y / Math.max(1, height))
                }
            }
            onPositionChanged: mouse => {
                if ((mouse.buttons & Qt.LeftButton)
                        && root.model.drawing) {
                    root.model.updateItem(
                                mouse.x / Math.max(1, width),
                                mouse.y / Math.max(1, height))
                }
            }
            onReleased: {
                if (root.model.drawing)
                    root.model.finishItem()
            }
            onCanceled: root.model.cancelItem()
        }

        Repeater {
            id: drawingRepeater
            objectName: "whiteboardDrawingRepeater"
            model: root.model.items

            delegate: Item {
                id: drawingDelegate
                objectName: "whiteboardDrawingItem"
                required property var modelData
                required property int index
                property real lastBoardX: 0
                property real lastBoardY: 0

                x: modelData.x * boardArea.width
                y: modelData.y * boardArea.height
                width: Math.max(2, modelData.width * boardArea.width)
                height: Math.max(2, modelData.height * boardArea.height)
                z: modelData.selected ? 2 : 1

                WhiteboardCanvasItem {
                    objectName: "whiteboardCanvasItem"
                    anchors.fill: parent
                    drawing: drawingDelegate.modelData
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    color: "transparent"
                    border.width: 1
                    border.color: "#dce75c"
                    visible: root.model.active
                             && drawingDelegate.modelData.selected
                             && root.model.tool !== "eraser"
                }

                MouseArea {
                    id: itemInput
                    objectName: "whiteboardItemInput"
                    anchors.fill: parent
                    enabled: root.model.active
                             && (root.model.tool === "select"
                                 || root.model.tool === "eraser")
                    acceptedButtons: Qt.LeftButton
                    hoverEnabled: true
                    cursorShape: root.model.tool === "eraser"
                                 ? Qt.CrossCursor
                                 : Qt.SizeAllCursor

                    onPressed: mouse => {
                        root.model.selectItem(drawingDelegate.index)
                        const point = drawingDelegate.mapToItem(
                                        boardArea, mouse.x, mouse.y)
                        drawingDelegate.lastBoardX = point.x
                        drawingDelegate.lastBoardY = point.y
                        if (root.model.tool === "eraser") {
                            root.model.eraseSelected(
                                        point.x / Math.max(1,
                                                           boardArea.width),
                                        point.y / Math.max(1,
                                                           boardArea.height),
                                        root.model.size
                                        / Math.max(
                                            1,
                                            Math.min(boardArea.width,
                                                     boardArea.height)))
                        }
                    }
                    onPositionChanged: mouse => {
                        if (!(mouse.buttons & Qt.LeftButton))
                            return
                        const point = drawingDelegate.mapToItem(
                                        boardArea, mouse.x, mouse.y)
                        if (root.model.tool === "eraser") {
                            root.model.eraseSelected(
                                        point.x / Math.max(1,
                                                           boardArea.width),
                                        point.y / Math.max(1,
                                                           boardArea.height),
                                        root.model.size
                                        / Math.max(
                                            1,
                                            Math.min(boardArea.width,
                                                     boardArea.height)))
                        } else {
                            root.model.moveSelected(
                                        (point.x
                                         - drawingDelegate.lastBoardX)
                                        / Math.max(1, boardArea.width),
                                        (point.y
                                         - drawingDelegate.lastBoardY)
                                        / Math.max(1, boardArea.height))
                        }
                        drawingDelegate.lastBoardX = point.x
                        drawingDelegate.lastBoardY = point.y
                    }
                    onDoubleClicked: {
                        if (drawingDelegate.modelData.type === "text") {
                            root.beginTextEntry(
                                        drawingDelegate.index,
                                        drawingDelegate.modelData.text,
                                        drawingDelegate.modelData.x,
                                        drawingDelegate.modelData.y)
                        }
                    }
                }

                Rectangle {
                    id: resizeHandle
                    objectName: "whiteboardResizeHandle"
                    width: 13
                    height: 13
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: -7
                    anchors.bottomMargin: -7
                    radius: 2
                    color: "#dce75c"
                    border.width: 1
                    border.color: "#111513"
                    visible: root.model.active
                             && drawingDelegate.modelData.selected
                             && root.model.tool === "select"
                    z: 4

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.SizeFDiagCursor
                        onPressed: mouse => {
                            const point = resizeHandle.mapToItem(
                                            boardArea, mouse.x, mouse.y)
                            drawingDelegate.lastBoardX = point.x
                            drawingDelegate.lastBoardY = point.y
                        }
                        onPositionChanged: mouse => {
                            if (!(mouse.buttons & Qt.LeftButton))
                                return
                            const point = resizeHandle.mapToItem(
                                            boardArea, mouse.x, mouse.y)
                            root.model.resizeSelected(
                                        (point.x
                                         - drawingDelegate.lastBoardX)
                                        / Math.max(1, boardArea.width),
                                        (point.y
                                         - drawingDelegate.lastBoardY)
                                        / Math.max(1, boardArea.height))
                            drawingDelegate.lastBoardX = point.x
                            drawingDelegate.lastBoardY = point.y
                        }
                    }
                }
            }
        }

        TextField {
            id: textEditor
            objectName: "whiteboardTextEditor"
            z: 12
            width: Math.min(300, boardArea.width - 16)
            height: 42
            visible: false
            placeholderText: qsTr("Annotation text")
            color: "#f4f7f4"
            selectByMouse: true
            maximumLength: 500
            onAccepted: root.commitTextEntry()
            onEditingFinished: {
                if (visible)
                    root.commitTextEntry()
            }
            Keys.onEscapePressed: root.cancelTextEntry()

            background: Rectangle {
                radius: 4
                color: "#f0111513"
                border.width: 1
                border.color: textEditor.activeFocus
                              ? "#dce75c" : "#667169"
            }
        }
    }

    Rectangle {
        id: toolbar
        objectName: "whiteboardToolbar"
        x: 14
        y: root.boardTop + 10
        z: 20
        width: root.model.active
               ? Math.min(parent.width - 28, 522)
               : 116
        height: root.model.active ? 84 : 46
        radius: 6
        color: "#ee111513"
        border.width: 1
        border.color: root.model.active ? "#758176" : "#465049"
        clip: true

        Column {
            id: toolbarContent
            objectName: "whiteboardToolbarContent"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            Row {
                spacing: 4

                Button {
                    id: modeToggle
                    objectName: "whiteboardModeToggle"
                    width: 98
                    height: 34
                    checkable: true
                    checked: root.model.active
                    enabled: root.available
                    text: qsTr("Whiteboard")
                    onToggled: root.model.active = checked

                    background: Rectangle {
                        radius: 4
                        color: modeToggle.checked
                               ? "#dce75c" : "#e8ebe8"
                        border.width: 1
                        border.color: modeToggle.checked
                                      ? "#f4f7a0" : "#88938b"
                        opacity: modeToggle.enabled ? 1 : 0.55
                    }

                    ToolTip.visible: hovered
                    ToolTip.delay: 350
                    ToolTip.text: root.available
                                  ? qsTr("Draw over the 3D viewer")
                                  : qsTr("Load a map to use the whiteboard")
                }

                Rectangle {
                    visible: root.model.active
                    width: 1
                    height: 24
                    color: "#465049"
                    anchors.verticalCenter: parent.verticalCenter
                }

                Repeater {
                    objectName: "whiteboardToolRepeater"
                    model: [
                        { "id": "select", "label": qsTr("Select"),
                          "buttonWidth": 56 },
                        { "id": "pen", "label": qsTr("Pen"),
                          "buttonWidth": 48 },
                        { "id": "line", "label": qsTr("Line"),
                          "buttonWidth": 48 },
                        { "id": "rectangle", "label": qsTr("Rect"),
                          "buttonWidth": 48 },
                        { "id": "ellipse", "label": qsTr("Ellipse"),
                          "buttonWidth": 58 },
                        { "id": "text", "label": qsTr("Text"),
                          "buttonWidth": 48 },
                        { "id": "eraser", "label": qsTr("Erase"),
                          "buttonWidth": 52 }
                    ]

                    delegate: Button {
                        id: toolButton
                        objectName: "whiteboardToolButton_" + modelData.id
                        visible: root.model.active
                        height: 32
                        width: modelData.buttonWidth
                        checkable: true
                        autoExclusive: true
                        checked: root.model.tool === modelData.id
                        text: modelData.label
                        onClicked: root.model.tool = modelData.id

                        background: Rectangle {
                            radius: 3
                            color: toolButton.checked
                                   ? "#dce75c" : "#e8ebe8"
                            border.width: 1
                            border.color: toolButton.checked
                                          ? "#f4f7a0" : "#88938b"
                        }
                    }
                }
            }

            Row {
                visible: root.model.active
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 4

                ToolButton {
                    id: colorButton
                    objectName: "whiteboardColorButton"
                    visible: root.model.active
                    width: 34
                    height: 34
                    Accessible.name: qsTr("Drawing color")
                    onClicked: colorPopup.open()

                    contentItem: Rectangle {
                        width: 18
                        height: 18
                        radius: 3
                        anchors.centerIn: parent
                        color: root.model.color
                        border.width: 1
                        border.color: "#aeb8b0"
                    }

                    ToolTip.visible: hovered
                    ToolTip.delay: 350
                    ToolTip.text: qsTr("Drawing color")
                }

                Label {
                    visible: root.model.active
                    text: qsTr("Size")
                    color: "#aeb8b0"
                    font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                }

                Slider {
                    id: sizeSlider
                    objectName: "whiteboardSizeSlider"
                    visible: root.model.active
                    width: 70
                    height: 34
                    Accessible.name: qsTr("Drawing size")
                    from: 1
                    to: 24
                    stepSize: 1
                    value: root.model.size
                    onValueChanged: {
                        if (Math.abs(root.model.size - value) > 0.001)
                            root.model.size = value
                    }
                    ToolTip.visible: hovered || pressed
                    ToolTip.text: qsTr("%1 px").arg(Math.round(value))
                }

                ToolButton {
                    objectName: "whiteboardDeleteButton"
                    visible: root.model.active
                    enabled: root.model.selectedIndex >= 0
                    width: 34
                    height: 34
                    Accessible.name: qsTr("Delete selected item")
                    text: "\u00d7"
                    font.pixelSize: 22
                    onClicked: root.model.removeSelected()
                    ToolTip.visible: hovered
                    ToolTip.delay: 350
                    ToolTip.text: qsTr("Delete selected item")
                }
            }
        }
    }

    Popup {
        id: colorPopup
        objectName: "whiteboardColorPopup"
        parent: root
        x: Math.min(root.width - width - 12,
                    toolbar.x + 8)
        y: toolbar.y + toolbar.height + 6
        z: 30
        width: 212
        height: 92
        padding: 10
        closePolicy: Popup.CloseOnEscape
                     | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 6
            color: "#f0111513"
            border.width: 1
            border.color: "#667169"
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            RowLayout {
                spacing: 6
                Repeater {
                    model: [
                        "#f8faf9", "#dce75c", "#42d3c6",
                        "#ff785a", "#ff4f86", "#7ca9ff"
                    ]
                    delegate: ToolButton {
                        objectName: "whiteboardColorSwatch"
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 26
                        onClicked: {
                            root.model.color = modelData
                            colorPopup.close()
                        }
                        contentItem: Rectangle {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            radius: 3
                            color: modelData
                            border.width: root.model.color.toString()
                                          === modelData ? 2 : 1
                            border.color: "#f8faf9"
                        }
                    }
                }
            }

            TextField {
                id: customColor
                objectName: "whiteboardCustomColor"
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                text: root.model.color.toString()
                placeholderText: qsTr("#RRGGBB")
                selectByMouse: true
                onAccepted: {
                    root.model.color = text
                    text = root.model.color.toString()
                    colorPopup.close()
                }
            }
        }
    }
}
