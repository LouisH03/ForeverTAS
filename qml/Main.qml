import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick3D
import "settings"
import ForeverTAS.Viewer 1.0

ApplicationWindow {
    id: window

    required property var controller
    required property var viewer

    property bool wireframeMode: false
    readonly property var settingsWheelRedirectorObject:
        settingsWheelRedirector

    ListModel {
        id: runPoseModel
        dynamicRoles: true
    }

    function synchronizeRunPoses() {
        const poses = window.viewer.runPoses
        while (runPoseModel.count < poses.length) {
            runPoseModel.append({
                                    "runId": "",
                                    "runName": "",
                                    "runIndex": 0,
                                    "runPosition": Qt.vector3d(0, 0, 0),
                                    "runRotation": Qt.quaternion(1, 0, 0, 0),
                                    "runSelected": false,
                                    "runGeometry": null
                                })
        }
        while (runPoseModel.count > poses.length)
            runPoseModel.remove(runPoseModel.count - 1)
        for (let index = 0; index < poses.length; ++index) {
            const pose = poses[index]
            runPoseModel.setProperty(index, "runId", pose.id)
            runPoseModel.setProperty(index, "runName", pose.name)
            runPoseModel.setProperty(index, "runIndex", pose.index)
            runPoseModel.setProperty(index, "runPosition", pose.position)
            runPoseModel.setProperty(index, "runRotation", pose.rotation)
            runPoseModel.setProperty(index, "runSelected", pose.selected)
            runPoseModel.setProperty(index, "runGeometry", pose.geometry)
        }
    }

    function runColor(index) {
        const colors = ["#ff8a3d", "#3d8dff", "#63c77b", "#c57aeb",
                        "#e7c24f", "#54c7c1"]
        return colors[index % colors.length]
    }

    function stepViewerTick(delta) {
        if (!window.viewer.loaded) {
            return
        }
        window.viewer.pause()
        window.viewer.currentTick = window.viewer.currentTick + delta
    }

    Component.onCompleted: Qt.callLater(synchronizeRunPoses)

    Connections {
        target: window.viewer

        function onRunsChanged() {
            window.synchronizeRunPoses()
        }
        function onPoseChanged() {
            window.synchronizeRunPoses()
        }
        function onSelectedRunChanged() {
            window.synchronizeRunPoses()
        }
    }

    width: 1420
    height: 820
    minimumWidth: 1050
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

    Shortcut {
        objectName: "stepBackwardShortcut"
        sequence: "Left"
        context: Qt.ApplicationShortcut
        autoRepeat: true
        enabled: window.viewer.loaded
        onActivated: window.stepViewerTick(-1)
    }

    Shortcut {
        objectName: "stepForwardShortcut"
        sequence: "Right"
        context: Qt.ApplicationShortcut
        autoRepeat: true
        enabled: window.viewer.loaded
        onActivated: window.stepViewerTick(1)
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Rectangle {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 680
            color: "#181b19"

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    id: timelinePanel
                    objectName: "timelinePanel"
                    Layout.preferredWidth: 252
                    Layout.minimumWidth: 220
                    Layout.maximumWidth: 300
                    Layout.fillHeight: true
                    color: "#101412"

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            color: "#151a17"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                anchors.topMargin: 8
                                anchors.bottomMargin: 8
                                spacing: 2

                                Label {
                                    objectName: "timelineTimeLabel"
                                    text: window.viewer.timeText
                                    color: "#f0f3ef"
                                    font.family: "monospace"
                                    font.pixelSize: 15
                                    font.weight: Font.Medium
                                }

                                Label {
                                    text: window.viewer.loaded
                                          ? qsTr("Tick %1 / %2 · 100 Hz")
                                                .arg(window.viewer.currentTick)
                                                .arg(Math.max(0,
                                                              window.viewer.tickCount - 1))
                                          : qsTr("100 physics ticks / second")
                                    color: "#747f77"
                                    font.pixelSize: 10
                                }
                            }
                        }

                        RaceTimeline {
                            id: raceTimeline
                            objectName: "raceTimeline"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            viewer: window.viewer
                            pixelsPerTick: 3
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 50
                            color: "#151a17"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 10

                                Rectangle {
                                    Layout.preferredWidth: 10
                                    Layout.preferredHeight: 10
                                    radius: 2
                                    color: "#4f9ddd"
                                }
                                Label {
                                    text: qsTr("Steer")
                                    color: "#9fa9a2"
                                    font.pixelSize: 10
                                }
                                Rectangle {
                                    Layout.preferredWidth: 10
                                    Layout.preferredHeight: 10
                                    radius: 2
                                    color: "#3dbd73"
                                }
                                Label {
                                    text: qsTr("Gas")
                                    color: "#9fa9a2"
                                    font.pixelSize: 10
                                }
                                Rectangle {
                                    Layout.preferredWidth: 10
                                    Layout.preferredHeight: 10
                                    radius: 2
                                    color: "#df5555"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Brake")
                                    color: "#9fa9a2"
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: "#2b322e"
                }

                Item {
                    id: viewport
                    objectName: "raceViewport"
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    property real orbitYaw: 35
                    property real orbitPitch: -20
                    property real orbitDistance: 38

                    View3D {
                        anchors.fill: parent

                        environment: SceneEnvironment {
                            backgroundMode: SceneEnvironment.Color
                            clearColor: "#181b19"
                            antialiasingMode: SceneEnvironment.MSAA
                            antialiasingQuality: SceneEnvironment.Medium
                        }

                        Node {
                            position: window.viewer.carPosition
                            eulerRotation.x: viewport.orbitPitch
                            eulerRotation.y: viewport.orbitYaw

                            PerspectiveCamera {
                                z: viewport.orbitDistance
                                clipNear: 0.05
                                clipFar: Math.max(5000,
                                                  window.viewer.sceneRadius * 3)
                                fieldOfView: 55
                            }
                        }

                        Model {
                            objectName: "trackFilledModel"
                            visible: window.viewer.loaded
                                     && !window.wireframeMode
                            geometry: window.viewer.loaded
                                      ? window.viewer.trackFilledGeometry
                                      : null
                            materials: DefaultMaterial {
                                lighting: DefaultMaterial.NoLighting
                                vertexColorsEnabled: true
                                diffuseColor: "white"
                                cullMode: Material.BackFaceCulling
                            }
                        }

                        Model {
                            objectName: "trackWireModel"
                            visible: window.viewer.loaded
                                     && window.wireframeMode
                            geometry: window.viewer.loaded
                                      ? window.viewer.trackWireGeometry
                                      : null
                            materials: DefaultMaterial {
                                lighting: DefaultMaterial.NoLighting
                                diffuseColor: "#b8d9c7"
                                cullMode: Material.NoCulling
                            }
                        }

                        Repeater3D {
                            model: runPoseModel

                            delegate: Node {
                                id: runCarRoot

                                required property string runId
                                required property string runName
                                required property int runIndex
                                required property var runPosition
                                required property var runRotation
                                required property bool runSelected
                                required property var runGeometry

                                objectName: "runCarRoot"
                                position: runPosition
                                rotation: runRotation

                                Repeater3D {
                                    model: window.viewer.carEllipsoids

                                    delegate: Node {
                                        required property var modelData

                                        position: modelData.position
                                        rotation: modelData.rotation
                                        scale: modelData.radii

                                        Model {
                                            objectName: "runCarFilledModel"
                                            visible: !window.wireframeMode
                                            geometry: runCarRoot.runGeometry
                                            materials: DefaultMaterial {
                                                objectName: "runCarFilledMaterial"
                                                lighting:
                                                    DefaultMaterial.NoLighting
                                                vertexColorsEnabled: true
                                                diffuseColor: "white"
                                                cullMode:
                                                    Material.BackFaceCulling
                                            }
                                        }

                                        Model {
                                            objectName: "runCarWireModel"
                                            visible: window.wireframeMode
                                            geometry:
                                                window.viewer.ellipsoidWireGeometry
                                            materials: DefaultMaterial {
                                                lighting:
                                                    DefaultMaterial.NoLighting
                                                diffuseColor: window.runColor(
                                                    runCarRoot.runIndex)
                                                cullMode: Material.NoCulling
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        hoverEnabled: true
                        property real previousX: 0
                        property real previousY: 0

                        onPressed: mouse => {
                            previousX = mouse.x
                            previousY = mouse.y
                        }
                        onPositionChanged: mouse => {
                            if (!(mouse.buttons & Qt.LeftButton))
                                return
                            viewport.orbitYaw -= mouse.x - previousX
                            viewport.orbitPitch = Math.max(
                                -85,
                                Math.min(85,
                                         viewport.orbitPitch
                                         - (mouse.y - previousY)))
                            previousX = mouse.x
                            previousY = mouse.y
                        }
                        onWheel: wheel => {
                            const factor = Math.exp(
                                -wheel.angleDelta.y / 1200)
                            viewport.orbitDistance = Math.max(
                                3,
                                Math.min(1000,
                                         viewport.orbitDistance * factor))
                        }
                    }

                    Rectangle {
                        id: raceViewerHeader
                        objectName: "raceViewerHeader"
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 52
                        color: "#cc111412"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 12

                            Label {
                                text: qsTr("Race Viewer")
                                color: "#eef2ee"
                                font.pixelSize: 17
                                font.weight: Font.DemiBold
                            }

                            Label {
                                Layout.fillWidth: true
                                text: window.viewer.loaded
                                      ? qsTr("%1 triangles · %2 runs · %3 ellipsoids/run")
                                            .arg(window.viewer.triangleCount)
                                            .arg(window.viewer.runCount)
                                            .arg(window.viewer.ellipsoidCount)
                                      : window.viewer.statusText
                                color: "#aeb8b0"
                                elide: Text.ElideRight
                            }

                            Switch {
                                id: wireframeSwitch
                                objectName: "wireframeSwitch"
                                text: qsTr("Wireframe")
                                checked: window.wireframeMode
                                enabled: window.viewer.loaded
                                onToggled:
                                    window.wireframeMode = checked

                                contentItem: Text {
                                    objectName: "wireframeLabel"
                                    leftPadding:
                                        wireframeSwitch.indicator.width
                                        + wireframeSwitch.spacing
                                    text: wireframeSwitch.text
                                    font: wireframeSwitch.font
                                    color: "#ffffff"
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            Button {
                                text: qsTr("Reset view")
                                enabled: window.viewer.loaded
                                onClicked: {
                                    viewport.orbitYaw = 35
                                    viewport.orbitPitch = -20
                                    viewport.orbitDistance = 38
                                }
                            }
                        }

                        StyledComboBox {
                            id: runSelector

                            objectName: "runSelector"
                            anchors.centerIn: parent
                            width: 180
                            model: window.viewer.runOptions
                            textRole: "name"
                            valueRole: "id"
                            enabled: count > 0

                            function synchronizeSelection() {
                                const selected = indexOfValue(
                                    window.viewer.selectedRunId)
                                if (selected >= 0 && currentIndex !== selected)
                                    currentIndex = selected
                            }

                            Component.onCompleted: synchronizeSelection()
                            onModelChanged: Qt.callLater(synchronizeSelection)
                            onActivated: selectedIndex =>
                                window.viewer.selectedRunId =
                                    valueAt(selectedIndex)

                            Connections {
                                target: window.viewer

                                function onRunsChanged() {
                                    Qt.callLater(runSelector.synchronizeSelection)
                                }
                                function onSelectedRunChanged() {
                                    runSelector.synchronizeSelection()
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: playbackDock
                        objectName: "playbackDock"
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 20
                        width: 190
                        height: 58
                        radius: 16
                        color: "#e6111513"
                        border.width: 1
                        border.color: "#465049"

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 8

                            ToolButton {
                                objectName: "jumpStartButton"
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 42
                                implicitWidth: 42
                                implicitHeight: 42
                                text: ""
                                enabled: window.viewer.loaded
                                palette.buttonText: "#e6ebe7"
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Go to start")
                                onClicked: window.viewer.jumpToStart()

                                contentItem: Item {
                                    Item {
                                        objectName: "jumpStartTransportIcon"
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18

                                        Rectangle {
                                            x: 2
                                            y: 2
                                            width: 3
                                            height: 14
                                            radius: 1
                                            color: "#e6ebe7"
                                        }

                                        Shape {
                                            anchors.fill: parent

                                            ShapePath {
                                                strokeWidth: -1
                                                fillColor: "#e6ebe7"
                                                startX: 15
                                                startY: 2
                                                PathLine {
                                                    x: 6
                                                    y: 9
                                                }
                                                PathLine {
                                                    x: 15
                                                    y: 16
                                                }
                                                PathLine {
                                                    x: 15
                                                    y: 2
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            ToolButton {
                                objectName: "playPauseButton"
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 42
                                implicitWidth: 42
                                implicitHeight: 42
                                text: ""
                                enabled: window.viewer.loaded
                                palette.buttonText: "#ffffff"
                                ToolTip.visible: hovered
                                ToolTip.text: window.viewer.playing
                                              ? qsTr("Pause")
                                              : qsTr("Play")
                                onClicked: window.viewer.togglePlayback()

                                contentItem: Item {
                                    Shape {
                                        objectName: "playTransportIcon"
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18
                                        visible: !window.viewer.playing

                                        ShapePath {
                                            strokeWidth: -1
                                            fillColor: "#ffffff"
                                            startX: 4
                                            startY: 2
                                            PathLine {
                                                x: 16
                                                y: 9
                                            }
                                            PathLine {
                                                x: 4
                                                y: 16
                                            }
                                            PathLine {
                                                x: 4
                                                y: 2
                                            }
                                        }
                                    }

                                    Item {
                                        objectName: "pauseTransportIcon"
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18
                                        visible: window.viewer.playing

                                        Rectangle {
                                            x: 3
                                            y: 2
                                            width: 4
                                            height: 14
                                            radius: 1
                                            color: "#ffffff"
                                        }

                                        Rectangle {
                                            x: 11
                                            y: 2
                                            width: 4
                                            height: 14
                                            radius: 1
                                            color: "#ffffff"
                                        }
                                    }
                                }
                            }

                            ToolButton {
                                objectName: "jumpEndButton"
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 42
                                implicitWidth: 42
                                implicitHeight: 42
                                text: ""
                                enabled: window.viewer.loaded
                                palette.buttonText: "#e6ebe7"
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Go to end")
                                onClicked: window.viewer.jumpToEnd()

                                contentItem: Item {
                                    Item {
                                        objectName: "jumpEndTransportIcon"
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18

                                        Shape {
                                            anchors.fill: parent

                                            ShapePath {
                                                strokeWidth: -1
                                                fillColor: "#e6ebe7"
                                                startX: 3
                                                startY: 2
                                                PathLine {
                                                    x: 12
                                                    y: 9
                                                }
                                                PathLine {
                                                    x: 3
                                                    y: 16
                                                }
                                                PathLine {
                                                    x: 3
                                                    y: 2
                                                }
                                            }
                                        }

                                        Rectangle {
                                            x: 13
                                            y: 2
                                            width: 3
                                            height: 14
                                            radius: 1
                                            color: "#e6ebe7"
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Column {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 60, 430)
                        spacing: 12
                        visible: !window.viewer.loaded

                        BusyIndicator {
                            anchors.horizontalCenter: parent.horizontalCenter
                            running: window.viewer.loading
                            visible: running
                        }

                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: window.viewer.loading
                                  ? window.viewer.statusText
                                  : qsTr("Select a replay and load it from the settings panel.")
                            color: "#d9ded9"
                            wrapMode: Text.WordWrap
                            font.pixelSize: 16
                        }

                        Label {
                            width: parent.width
                            visible: !window.viewer.loading
                                     && window.viewer.statusText
                                        !== qsTr("No replay loaded")
                            horizontalAlignment: Text.AlignHCenter
                            text: window.viewer.statusText
                            color: "#e19b9b"
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                        }

                    }
                }
            }
        }

        Rectangle {
            SplitView.preferredWidth: 390
            SplitView.minimumWidth: 340
            SplitView.maximumWidth: 480
            color: "#f4f5f2"

            ScrollView {
                id: settingsScroll

                objectName: "settingsScroll"
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                PanelWheelRedirector {
                    id: settingsWheelRedirector
                    objectName: "settingsWheelRedirector"
                    parent: settingsScroll.parent
                    anchors.fill: parent
                    flickable: settingsScroll.contentItem
                }

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

                        Rectangle {
                            objectName: "autoPacksSuggestion"
                            Layout.fillWidth: true
                            implicitHeight: autoPacksSuggestionLayout.implicitHeight
                                            + 16
                            radius: 8
                            color: "#e7f2eb"
                            border.width: 1
                            border.color: "#8eb49d"
                            visible:
                                window.controller.autoDetectedPacksDirectory.length
                                > 0

                            RowLayout {
                                id: autoPacksSuggestionLayout
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        objectName: "autoPacksSuggestionText"
                                        Layout.fillWidth: true
                                        text: qsTr("This location was found automatically and should work. Apply?")
                                        color: "#284d35"
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 11
                                        font.weight: Font.Medium
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: window.controller.autoDetectedPacksDirectory
                                        color: "#42654c"
                                        elide: Text.ElideMiddle
                                        font.family: "monospace"
                                        font.pixelSize: 9
                                    }
                                }

                                Button {
                                    objectName: "applyAutoPacksButton"
                                    text: qsTr("Apply")
                                    enabled: !window.controller.running
                                    onClicked:
                                        window.controller.applyAutoDetectedPacksDirectory()
                                }
                            }
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

                        Button {
                            Layout.fillWidth: true
                            text: window.viewer.loading
                                  ? qsTr("Loading viewer...")
                                  : qsTr("Load Race Viewer")
                            enabled: !window.viewer.loading
                                     && !window.controller.running
                                     && window.controller.packsDirectory.length > 0
                                     && window.controller.replayPath.length > 0
                            highlighted: true
                            onClicked: window.viewer.loadReplay(
                                window.controller.packsDirectory,
                                window.controller.replayPath)
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

                    ConfigurationSection {
                        objectName: "evaluationSection"
                        Layout.fillWidth: true
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        title: qsTr("Evaluation")

                        AlgorithmSelector {
                            objectName: "evaluationTargetSelector"
                            Layout.fillWidth: true
                            title: qsTr("Target")
                            comboObjectName: "evaluationTargetCombo"
                            options: window.controller.evaluationTargetOptions
                            selectedId: window.controller.evaluationTargetId
                            controller: window.controller
                            onSelectionRequested: id =>
                                window.controller.evaluationTargetId = id
                        }
                    }

                    ConfigurationSection {
                        objectName: "modifierSection"
                        Layout.fillWidth: true
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        title: qsTr("Input modifiers")

                        ModifierComposition {
                            Layout.fillWidth: true
                            controller: window.controller
                            options: window.controller.modifierOptions
                            passes: window.controller.modifierPasses
                        }
                    }

                    ConfigurationSection {
                        objectName: "searchSection"
                        Layout.fillWidth: true
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        title: qsTr("Search")

                        AlgorithmSelector {
                            objectName: "searchAlgorithmSelector"
                            Layout.fillWidth: true
                            title: qsTr("Algorithm")
                            comboObjectName: "searchAlgorithmCombo"
                            options: window.controller.searchAlgorithmOptions
                            selectedId: window.controller.searchAlgorithmId
                            controller: window.controller
                            onSelectionRequested: id =>
                                window.controller.searchAlgorithmId = id
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
                            objectName: "startSearchButton"
                            Layout.fillWidth: true
                            text: qsTr("Start")
                            highlighted: true
                            enabled: window.controller.canStart
                            onClicked: window.controller.startSearch()
                        }

                        Button {
                            objectName: "stopSearchButton"
                            Layout.fillWidth: true
                            text: window.controller.stopping
                                  ? qsTr("Stopping...")
                                  : qsTr("Stop")
                            enabled: window.controller.running
                                     && !window.controller.stopping
                            onClicked: window.controller.stopSearch()
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

                        RowLayout {
                            id: searchMetricsRow

                            objectName: "searchMetricsRow"
                            Layout.fillWidth: true
                            visible: window.controller.liveMetricsVisible
                            spacing: 6

                            Rectangle {
                                objectName: "iterationsMetricCard"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 58
                                radius: 7
                                color: "#eef2ed"
                                border.width: 1
                                border.color: "#d2d9cf"
                                clip: true

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 2

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Iterations")
                                        color: "#667064"
                                        font.pixelSize: 10
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    Label {
                                        objectName: "iterationsMetricValue"
                                        Layout.fillWidth: true
                                        text: window.controller.iterationCountText
                                        color: "#20251f"
                                        font.family: "monospace"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            Rectangle {
                                objectName: "throughputMetricCard"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 58
                                radius: 7
                                color: "#eef2ed"
                                border.width: 1
                                border.color: "#d2d9cf"
                                clip: true

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 2

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Throughput")
                                        color: "#667064"
                                        font.pixelSize: 10
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    Label {
                                        objectName: "throughputMetricValue"
                                        Layout.fillWidth: true
                                        text: window.controller.throughputText.length > 0
                                              ? window.controller.throughputText
                                                    + qsTr(" /s")
                                              : ""
                                        color: "#20251f"
                                        font.family: "monospace"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            Rectangle {
                                objectName: "elapsedMetricCard"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 58
                                radius: 7
                                color: "#eef2ed"
                                border.width: 1
                                border.color: "#d2d9cf"
                                clip: true

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 2

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Elapsed")
                                        color: "#667064"
                                        font.pixelSize: 10
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    Label {
                                        objectName: "elapsedMetricValue"
                                        Layout.fillWidth: true
                                        text: window.controller.elapsedText
                                        color: "#20251f"
                                        font.family: "monospace"
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideRight
                                    }
                                }
                            }
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

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: window.controller.bestInputsText.length > 0
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Best input script")
                                    font.weight: Font.Medium
                                }

                                Button {
                                    objectName: "copyBestInputsButton"
                                    text: qsTr("Copy all")
                                    onClicked: {
                                        bestInputsArea.selectAll()
                                        bestInputsArea.copy()
                                        bestInputsArea.select(0, 0)
                                    }
                                }
                            }

                            ScrollView {
                                id: bestInputsScroll

                                objectName: "bestInputsScrollView"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 260
                                clip: true
                                ScrollBar.horizontal.policy:
                                    ScrollBar.AsNeeded
                                ScrollBar.vertical.policy:
                                    ScrollBar.AsNeeded

                                TextArea {
                                    id: bestInputsArea

                                    objectName: "bestInputsTextArea"
                                    width: Math.max(
                                        bestInputsScroll.availableWidth,
                                        contentWidth + leftPadding + rightPadding)
                                    text: window.controller.bestInputsText
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.NoWrap
                                    textFormat: TextEdit.PlainText
                                    font.family: "monospace"
                                    font.pixelSize: 12
                                    color: "#202421"
                                    background: Rectangle {
                                        color: "#ffffff"
                                        border.width: 1
                                        border.color: "#c5ccc1"
                                        radius: 6
                                    }
                                }
                            }
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
