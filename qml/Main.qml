import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D

ApplicationWindow {
    id: window

    required property var controller
    required property var viewer

    property bool wireframeMode: false

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

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    id: viewport
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

                        Node {
                            objectName: "carCollisionRoot"
                            position: window.viewer.carPosition
                            rotation: window.viewer.carRotation

                            Repeater3D {
                                model: window.viewer.carEllipsoids

                                delegate: Node {
                                    required property var modelData

                                    position: modelData.position
                                    rotation: modelData.rotation
                                    scale: modelData.radii

                                    Model {
                                        objectName: "carFilledModel"
                                        visible: !window.wireframeMode
                                        geometry:
                                            window.viewer.ellipsoidFilledGeometry
                                        materials: DefaultMaterial {
                                            lighting:
                                                DefaultMaterial.NoLighting
                                            vertexColorsEnabled: true
                                            diffuseColor: "white"
                                            cullMode:
                                                Material.BackFaceCulling
                                        }
                                    }

                                    Model {
                                        objectName: "carWireModel"
                                        visible: window.wireframeMode
                                        geometry:
                                            window.viewer.ellipsoidWireGeometry
                                        materials: DefaultMaterial {
                                            lighting:
                                                DefaultMaterial.NoLighting
                                            diffuseColor: "#ff8a3d"
                                            cullMode: Material.NoCulling
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
                                      ? qsTr("%1 triangles · %2 ellipsoids")
                                            .arg(window.viewer.triangleCount)
                                            .arg(window.viewer.ellipsoidCount)
                                      : window.viewer.statusText
                                color: "#aeb8b0"
                                elide: Text.ElideRight
                            }

                            Switch {
                                text: qsTr("Wireframe")
                                checked: window.wireframeMode
                                enabled: window.viewer.loaded
                                palette.text: "#eef2ee"
                                onToggled:
                                    window.wireframeMode = checked
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

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    color: "#111412"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 14

                        Label {
                            text: window.viewer.timeText
                            color: "#d9ded9"
                            font.family: "monospace"
                        }

                        Slider {
                            id: timeline
                            Layout.fillWidth: true
                            from: 0
                            to: Math.max(1, window.viewer.durationMs)
                            value: window.viewer.timeMs
                            enabled: window.viewer.loaded
                            live: true
                            onMoved:
                                window.viewer.timeMs = Math.round(value)
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
