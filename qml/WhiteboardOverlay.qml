import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
    property bool drawingListOpen: false
    property bool imageExportInProgress: false
    property int pendingImageBoardIndex: -1
    property string pendingImageMode: ""
    readonly property color toolbarControlText:
        AppTheme.dark ? AppTheme.viewerOverlayText : AppTheme.text
    property var captureViewpoint: function() { return ({}) }
    property var restoreViewpoint: function(board) {}
    property var exportBackgroundImage: function(index, fileUrl) {
        return false
    }

    function chooseImageExport(index, mode) {
        if (imageExportInProgress
                || index < 0 || index >= model.boardCount)
            return
        pendingImageBoardIndex = index
        pendingImageMode = mode
        imageExportDialog.open()
    }

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

    function focusBoard(index) {
        if (!model.selectBoard(index))
            return
        const board = model.boards[index]
        if (board)
            restoreViewpoint(board)
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
            color: AppTheme.viewerOverlayText
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
                color: AppTheme.viewerOverlayStrong
                border.width: 1
                border.color: textEditor.activeFocus
                              ? "#dce75c" : AppTheme.viewerOverlayBorder
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
               : 198
        height: root.model.active ? 84 : 46
        radius: 6
        color: AppTheme.viewerOverlayStrong
        border.width: 1
        border.color: root.model.active
                      ? (AppTheme.dark ? "#8b978d" : "#758176")
                      : AppTheme.viewerOverlayBorder
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

                    contentItem: Label {
                        objectName: "whiteboardModeToggleLabel"
                        text: modeToggle.text
                        color: modeToggle.checked
                               ? "#111513"
                               : root.toolbarControlText
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: modeToggle.checked
                               ? "#dce75c"
                               : (AppTheme.dark ? AppTheme.control : "#e8ebe8")
                        border.width: 1
                        border.color: modeToggle.checked
                                      ? "#f4f7a0" : AppTheme.borderStrong
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
                    color: AppTheme.viewerOverlayBorder
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

                        contentItem: Label {
                            text: toolButton.text
                            color: toolButton.checked
                                   ? "#111513"
                                   : root.toolbarControlText
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 3
                            color: toolButton.checked
                                   ? "#dce75c"
                                   : (AppTheme.dark ? AppTheme.control : "#e8ebe8")
                            border.width: 1
                            border.color: toolButton.checked
                                          ? "#f4f7a0" : AppTheme.borderStrong
                        }
                    }
                }

                Button {
                    objectName: "whiteboardInactiveListButton"
                    visible: !root.model.active
                    width: 76
                    height: 32
                    text: qsTr("Drawings")
                    onClicked:
                        root.drawingListOpen = !root.drawingListOpen
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
                        border.color: AppTheme.viewerOverlayMuted
                    }

                    ToolTip.visible: hovered
                    ToolTip.delay: 350
                    ToolTip.text: qsTr("Drawing color")
                }

                Label {
                    visible: root.model.active
                    text: qsTr("Size")
                    color: AppTheme.viewerOverlayMuted
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

                Button {
                    objectName: "whiteboardPlaceButton"
                    visible: root.model.active
                    enabled: root.model.count > 0
                             && root.model.mapKey.length > 0
                    width: 58
                    height: 32
                    text: qsTr("Place")
                    onClicked: {
                        const index = root.model.captureCurrentBoard(
                                        qsTr("Drawing %1").arg(
                                            root.model.boardCount + 1),
                                        root.captureViewpoint())
                        if (index >= 0)
                            root.drawingListOpen = true
                    }
                    ToolTip.visible: hovered
                    ToolTip.delay: 350
                    ToolTip.text: qsTr(
                                      "Place this drawing at the current camera view")
                }

                Button {
                    objectName: "whiteboardActiveListButton"
                    visible: root.model.active
                    width: 72
                    height: 32
                    text: qsTr("Drawings")
                    onClicked:
                        root.drawingListOpen = !root.drawingListOpen
                }
            }
        }
    }

    Rectangle {
        id: drawingList
        objectName: "whiteboardDrawingList"
        z: 25
        visible: root.drawingListOpen
        width: Math.min(310, root.width - 28)
        height: Math.max(
                    250,
                    Math.min(450, root.height - y - 104))
        x: root.width - width - 14
        y: root.boardTop + 10
        radius: 6
        color: AppTheme.viewerOverlayStrong
        border.width: 1
        border.color: AppTheme.viewerOverlayBorder
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Drawings")
                    color: AppTheme.viewerOverlayText
                    font.pixelSize: 16
                    font.bold: true
                }

                ToolButton {
                    objectName: "whiteboardCloseListButton"
                    text: "\u00d7"
                    font.pixelSize: 20
                    Accessible.name: qsTr("Close drawing list")
                    onClicked: root.drawingListOpen = false
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Label {
                    anchors.centerIn: parent
                    width: parent.width - 24
                    visible: root.model.boardCount === 0
                    text: qsTr("No placed drawings")
                    color: AppTheme.viewerOverlayMuted
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                ListView {
                    id: boardListView
                    objectName: "whiteboardBoardListView"
                    anchors.fill: parent
                    visible: root.model.boardCount > 0
                    clip: true
                    spacing: 2
                    model: root.model.boards

                    delegate: Item {
                        id: boardRow
                        required property var modelData
                        required property int index

                        width: ListView.view.width
                        height: 64

                        Rectangle {
                            anchors.fill: parent
                            color: boardRow.modelData.selected
                                   ? (AppTheme.dark ? "#3a443e" : "#303a34")
                                   : "transparent"
                        }

                        Button {
                            anchors.left: parent.left
                            anchors.right: imageExportButton.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.rightMargin: 6
                            flat: true
                            enabled: boardRow.modelData.isCurrentMap
                            onClicked: root.focusBoard(boardRow.index)

                            contentItem: Column {
                                spacing: 2

                                Label {
                                    width: parent.width
                                    text: boardRow.modelData.name
                                    color: boardRow.modelData.isCurrentMap
                                           ? AppTheme.viewerOverlayText
                                           : AppTheme.viewerOverlayMuted
                                    elide: Text.ElideRight
                                }

                                Label {
                                    width: parent.width
                                    text: boardRow.modelData.isCurrentMap
                                          ? qsTr("Current map")
                                          : qsTr("Other map")
                                    color: AppTheme.viewerOverlayMuted
                                    font.pixelSize: 10
                                }
                            }
                        }

                        ToolButton {
                            id: imageExportButton
                            objectName: "whiteboardBoardImageExportButton"
                            anchors.right: visibilityToggle.left
                            anchors.rightMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: 34
                            height: 34
                            enabled: !root.imageExportInProgress
                            text: "\u2193"
                            font.pixelSize: 18
                            Accessible.name: qsTr("Export %1 as an image")
                                             .arg(boardRow.modelData.name)
                            onClicked: {
                                imageExportMenu.boardIndex = boardRow.index
                                imageExportMenu.currentMap =
                                        boardRow.modelData.isCurrentMap
                                imageExportMenu.popup(
                                            imageExportButton,
                                            0,
                                            imageExportButton.height)
                            }
                            ToolTip.visible: hovered
                            ToolTip.delay: 350
                            ToolTip.text: qsTr("Export image")
                        }

                        CheckBox {
                            id: visibilityToggle
                            objectName: "whiteboardBoardVisibilityToggle"
                            anchors.right: removeBoardButton.left
                            anchors.rightMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: 36
                            enabled: boardRow.modelData.isCurrentMap
                            checked: boardRow.modelData.visible
                            Accessible.name: qsTr(
                                                 "Show %1 in the current map")
                                             .arg(boardRow.modelData.name)
                            onClicked: root.model.setBoardVisible(
                                           boardRow.index, checked)
                        }

                        ToolButton {
                            id: removeBoardButton
                            objectName: "whiteboardRemoveBoardButton"
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 34
                            height: 34
                            text: "\u00d7"
                            font.pixelSize: 20
                            Accessible.name: qsTr("Delete %1")
                                             .arg(boardRow.modelData.name)
                            onClicked:
                                root.model.removeBoard(boardRow.index)
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: AppTheme.viewerOverlayBorder
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: root.model.operationMessage.length > 0
                text: root.model.operationMessage
                color: AppTheme.viewerOverlayMuted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    objectName: "whiteboardImportButton"
                    Layout.fillWidth: true
                    text: qsTr("Import")
                    onClicked: importDialog.open()
                }

                Button {
                    objectName: "whiteboardExportButton"
                    Layout.fillWidth: true
                    enabled: root.model.boardCount > 0
                    text: qsTr("Export set")
                    onClicked: exportDialog.open()
                }
            }
        }
    }

    Menu {
        id: imageExportMenu
        objectName: "whiteboardImageExportMenu"
        property int boardIndex: -1
        property bool currentMap: false

        MenuItem {
            objectName: "whiteboardExportBackgroundMenuItem"
            text: qsTr("Image with full background")
            enabled: imageExportMenu.currentMap
                     && !root.imageExportInProgress
            onTriggered: root.chooseImageExport(
                             imageExportMenu.boardIndex, "background")
        }

        MenuItem {
            objectName: "whiteboardExportTransparentMenuItem"
            text: qsTr("Transparent drawing only")
            enabled: !root.imageExportInProgress
            onTriggered: root.chooseImageExport(
                             imageExportMenu.boardIndex, "transparent")
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
            color: AppTheme.viewerOverlayStrong
            border.width: 1
            border.color: AppTheme.viewerOverlayBorder
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

    FileDialog {
        id: importDialog
        objectName: "whiteboardImportDialog"
        title: qsTr("Import whiteboard set")
        fileMode: FileDialog.OpenFile
        options: FileDialog.DontUseNativeDialog
        nameFilters: [qsTr("ForeverTAS whiteboards (*.json)")]
        onAccepted: root.model.importBoardSet(selectedFile)
    }

    FileDialog {
        id: exportDialog
        objectName: "whiteboardExportDialog"
        title: qsTr("Export named whiteboard set")
        fileMode: FileDialog.SaveFile
        options: FileDialog.DontUseNativeDialog
        defaultSuffix: "json"
        nameFilters: [qsTr("ForeverTAS whiteboards (*.json)")]
        onAccepted: root.model.exportBoardSet(selectedFile)
    }

    FileDialog {
        id: imageExportDialog
        objectName: "whiteboardImageExportDialog"
        title: root.pendingImageMode === "background"
               ? qsTr("Export drawing with full background")
               : qsTr("Export transparent drawing")
        fileMode: FileDialog.SaveFile
        options: FileDialog.DontUseNativeDialog
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG images (*.png)")]
        onAccepted: {
            const index = root.pendingImageBoardIndex
            const mode = root.pendingImageMode
            root.pendingImageBoardIndex = -1
            root.pendingImageMode = ""
            if (mode === "background") {
                if (!root.exportBackgroundImage(
                            index, selectedFile)) {
                    root.model.finishBoardImageExport(false, true)
                }
            } else {
                root.model.exportBoardContentImage(
                            index, selectedFile)
            }
        }
        onRejected: {
            root.pendingImageBoardIndex = -1
            root.pendingImageMode = ""
        }
    }
}
