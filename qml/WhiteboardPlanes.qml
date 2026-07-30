import QtQuick
import QtQuick3D
import ForeverTAS.Viewer 1.0

View3D {
    id: root

    required property var model
    required property vector3d cameraTarget
    required property real orbitYaw
    required property real orbitPitch
    required property real orbitDistance
    property real fieldOfView: 55

    objectName: "whiteboardPlaneView"
    camera: planeCamera
    visible: model.visibleBoards.length > 0

    function pointInProjectedQuad(point, corners) {
        let sign = 0
        for (let index = 0; index < corners.length; ++index) {
            const first = corners[index]
            const second = corners[(index + 1) % corners.length]
            const cross = (second.x - first.x) * (point.y - first.y)
                        - (second.y - first.y) * (point.x - first.x)
            if (Math.abs(cross) < 0.001)
                continue
            const edgeSign = cross > 0 ? 1 : -1
            if (sign !== 0 && sign !== edgeSign)
                return false
            sign = edgeSign
        }
        return sign !== 0
    }

    function projectedPlaneContains(plane, x, y) {
        const localCorners = [
            Qt.vector3d(-50, -50, 0),
            Qt.vector3d(50, -50, 0),
            Qt.vector3d(50, 50, 0),
            Qt.vector3d(-50, 50, 0)
        ]
        const projected = []
        for (let index = 0; index < localCorners.length; ++index) {
            const scenePoint = plane.mapPositionToScene(
                                 localCorners[index])
            const viewPoint = root.mapFrom3DScene(scenePoint)
            if (!Number.isFinite(viewPoint.x)
                    || !Number.isFinite(viewPoint.y)
                    || !Number.isFinite(viewPoint.z)
                    || viewPoint.z <= 0)
                return false
            projected.push(viewPoint)
        }
        return pointInProjectedQuad(Qt.point(x, y), projected)
    }

    function pickBoard(x, y) {
        const hit = root.pick(x, y).objectHit
        if (hit && hit.boardIndex !== undefined)
            return hit.boardIndex

        // Source-item textured models are not pickable on every Quick 3D
        // backend. Project their actual plane corners as an exact fallback.
        for (let index = planeRepeater.count - 1; index >= 0; --index) {
            const plane = planeRepeater.objectAt(index)
            if (plane && projectedPlaneContains(plane, x, y))
                return plane.boardIndex
        }
        return -1
    }

    environment: SceneEnvironment {
        backgroundMode: SceneEnvironment.Transparent
        antialiasingMode: SceneEnvironment.MSAA
        antialiasingQuality: SceneEnvironment.Medium
    }

    Node {
        position: root.cameraTarget
        eulerRotation.x: root.orbitPitch
        eulerRotation.y: root.orbitYaw

        PerspectiveCamera {
            id: planeCamera
            z: root.orbitDistance
            clipNear: 0.01
            clipFar: 1000000
            fieldOfView: root.fieldOfView
        }
    }

    Repeater3D {
        id: planeRepeater
        objectName: "whiteboardPlaneRepeater"
        model: root.model.visibleBoards

        delegate: Model {
            id: plane
            required property var modelData
            readonly property int boardIndex: modelData.boardIndex

            objectName: "whiteboardPlane_" + modelData.id
            source: "#Rectangle"
            position: Qt.vector3d(
                          modelData.planeX,
                          modelData.planeY,
                          modelData.planeZ)
            eulerRotation.x: modelData.pitch
            eulerRotation.y: modelData.yaw
            scale: Qt.vector3d(
                       modelData.planeWidth / 100,
                       modelData.planeHeight / 100,
                       1)
            pickable: true
            castsShadows: false
            receivesShadows: false

            materials: DefaultMaterial {
                lighting: DefaultMaterial.NoLighting
                cullMode: Material.NoCulling
                diffuseMap: Texture {
                    sourceItem: Rectangle {
                        width: 1024
                        height: 576
                        color: "#b8111513"
                        border.width: 3
                        border.color: plane.modelData.selected
                                      ? "#dce75c" : "#8aa096"

                        Repeater {
                            model: plane.modelData.items

                            delegate: Item {
                                required property var modelData

                                x: modelData.x * parent.width
                                y: modelData.y * parent.height
                                width: Math.max(
                                           2,
                                           modelData.width * parent.width)
                                height: Math.max(
                                            2,
                                            modelData.height * parent.height)

                                WhiteboardCanvasItem {
                                    anchors.fill: parent
                                    drawing: parent.modelData
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
