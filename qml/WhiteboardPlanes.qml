import QtQuick
import QtQuick3D
import ForeverTAS.Viewer 1.0

View3D {
    id: root

    required property var model
    required property vector3d cameraTarget
    required property vector3d cameraPosition
    required property bool freeCamera
    required property real orbitYaw
    required property real orbitPitch
    required property real orbitDistance
    property real fieldOfView: 55
    property int forcedBoardIndex: -1
    property int exactBoardIndex: -1
    property real contentTop: 52
    property bool exportMode: false
    readonly property var renderedBoards: {
        const visible = model.visibleBoards.slice()
        if (forcedBoardIndex < 0 || forcedBoardIndex >= model.boardCount)
            return visible
        const forced = model.boards[forcedBoardIndex]
        if (!forced || !forced.isCurrentMap)
            return visible
        for (let index = 0; index < visible.length; ++index) {
            if (visible[index].boardIndex === forcedBoardIndex)
                return visible
        }
        visible.push(forced)
        return visible
    }

    objectName: "whiteboardPlaneView"
    camera: planeCamera
    visible: renderedBoards.length > 0

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

    function projectedPlaneBounds(boardIndex) {
        for (let index = 0; index < planeRepeater.count; ++index) {
            const plane = planeRepeater.objectAt(index)
            if (!plane || plane.boardIndex !== boardIndex)
                continue
            const corners = [
                Qt.vector3d(-50, -50, 0),
                Qt.vector3d(50, -50, 0),
                Qt.vector3d(50, 50, 0),
                Qt.vector3d(-50, 50, 0)
            ]
            let left = Number.POSITIVE_INFINITY
            let top = Number.POSITIVE_INFINITY
            let right = Number.NEGATIVE_INFINITY
            let bottom = Number.NEGATIVE_INFINITY
            for (let cornerIndex = 0;
                 cornerIndex < corners.length; ++cornerIndex) {
                const scenePoint = plane.mapPositionToScene(
                    corners[cornerIndex])
                const viewPoint = root.mapFrom3DScene(scenePoint)
                if (!Number.isFinite(viewPoint.x)
                        || !Number.isFinite(viewPoint.y)
                        || !Number.isFinite(viewPoint.z)
                        || viewPoint.z <= 0) {
                    return { "valid": false }
                }
                left = Math.min(left, viewPoint.x)
                top = Math.min(top, viewPoint.y)
                right = Math.max(right, viewPoint.x)
                bottom = Math.max(bottom, viewPoint.y)
            }
            return {
                "valid": true,
                "left": left,
                "top": top,
                "right": right,
                "bottom": bottom
            }
        }
        return { "valid": false }
    }

    function exactPlaneGeometry(board) {
        const viewportWidth = Math.max(1, root.width)
        const viewportHeight = Math.max(1, root.height)
        const top = Math.max(
            0, Math.min(viewportHeight - 1, root.contentTop))
        const contentHeight = viewportHeight - top
        const yaw = board.yaw * Math.PI / 180
        const pitch = board.pitch * Math.PI / 180
        const pitchCos = Math.cos(pitch)
        const forward = Qt.vector3d(
            -Math.sin(yaw) * pitchCos,
            Math.sin(pitch),
            -Math.cos(yaw) * pitchCos)
        const right = Qt.vector3d(
            Math.cos(yaw), 0, -Math.sin(yaw))
        const up = Qt.vector3d(
            Math.sin(yaw) * Math.sin(pitch),
            Math.cos(pitch),
            Math.cos(yaw) * Math.sin(pitch))
        const camera = Qt.vector3d(
            board.targetX - forward.x * board.distance,
            board.targetY - forward.y * board.distance,
            board.targetZ - forward.z * board.distance)
        const fullHeight = 2 * Math.tan(
            board.fieldOfView * Math.PI / 360)
            * board.planeDistance
        const fullWidth =
            fullHeight * viewportWidth / viewportHeight
        const centerY = (top + contentHeight * 0.5)
                        / viewportHeight
        const rightOffset = 0
        const upOffset = fullHeight * (0.5 - centerY)
        return {
            "position": Qt.vector3d(
                camera.x + forward.x * board.planeDistance
                    + right.x * rightOffset + up.x * upOffset,
                camera.y + forward.y * board.planeDistance
                    + right.y * rightOffset + up.y * upOffset,
                camera.z + forward.z * board.planeDistance
                    + right.z * rightOffset + up.z * upOffset),
            "width": fullWidth,
            "height": fullHeight
                      * contentHeight / viewportHeight
        }
    }

    function planeGeometry(board) {
        if (board.boardIndex === root.exactBoardIndex
                && board.projectionVersion >= 1
                && board.projection === "perspective-vertical") {
            return exactPlaneGeometry(board)
        }
        return {
            "position": Qt.vector3d(
                board.planeX, board.planeY, board.planeZ),
            "width": board.planeWidth,
            "height": board.planeHeight
        }
    }

    environment: SceneEnvironment {
        backgroundMode: SceneEnvironment.Transparent
        antialiasingMode: SceneEnvironment.MSAA
        antialiasingQuality: SceneEnvironment.Medium
    }

    Node {
        position: root.freeCamera
                  ? root.cameraPosition : root.cameraTarget
        eulerRotation.x: root.orbitPitch
        eulerRotation.y: root.orbitYaw

        PerspectiveCamera {
            id: planeCamera
            z: root.freeCamera ? 0 : root.orbitDistance
            clipNear: 0.01
            clipFar: 1000000
            fieldOfView: root.fieldOfView
            fieldOfViewOrientation: PerspectiveCamera.Vertical
        }
    }

    Repeater3D {
        id: planeRepeater
        objectName: "whiteboardPlaneRepeater"
        model: root.renderedBoards

        delegate: Model {
            id: plane
            required property var modelData
            readonly property int boardIndex: modelData.boardIndex
            readonly property bool exactProjectionActive:
                boardIndex === root.exactBoardIndex
                && modelData.projectionVersion >= 1
            readonly property var effectiveGeometry:
                root.planeGeometry(modelData)
            readonly property real effectivePlaneWidth:
                effectiveGeometry.width
            readonly property real effectivePlaneHeight:
                effectiveGeometry.height
            readonly property real sourceCanvasWidth:
                modelData.projectionVersion >= 1
                ? modelData.canvasWidth : 1024
            readonly property real sourceCanvasHeight:
                modelData.projectionVersion >= 1
                ? modelData.canvasHeight : 576

            objectName: "whiteboardPlane_" + modelData.id
            source: "#Rectangle"
            position: effectiveGeometry.position
            eulerRotation.x: modelData.pitch
            eulerRotation.y: modelData.yaw
            scale: Qt.vector3d(
                       effectivePlaneWidth / 100,
                       effectivePlaneHeight / 100,
                       1)
            pickable: true
            castsShadows: false
            receivesShadows: false

            materials: DefaultMaterial {
                lighting: DefaultMaterial.NoLighting
                cullMode: Material.NoCulling
                diffuseMap: Texture {
                    sourceItem: Rectangle {
                        objectName:
                            "whiteboardPlaneSurface_" + plane.modelData.id
                        width: Math.max(
                            1, Math.min(8192, plane.sourceCanvasWidth))
                        height: Math.max(
                            1, Math.min(8192, plane.sourceCanvasHeight))
                        color: "transparent"

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
