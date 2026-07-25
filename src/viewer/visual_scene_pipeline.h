#ifndef FOREVERTAS_VIEWER_VISUAL_SCENE_PIPELINE_H
#define FOREVERTAS_VIEWER_VISUAL_SCENE_PIPELINE_H

#include "viewer/material_classifier.h"

#include <forevervalidator/experimental/physics_sandbox.h>

#include <QByteArray>
#include <QString>
#include <QVector3D>

#include <cstdint>
#include <string_view>
#include <vector>

namespace forevertas::viewer {

constexpr int StaticVisualVertexStride = 17 * sizeof(float);

struct CameraClipPlanes {
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

CameraClipPlanes CalculateCameraClipPlanes(const QVector3D &cameraPosition,
                                           float cameraDistance,
                                           const QVector3D &boundsMin,
                                           const QVector3D &boundsMax);

bool IsDefaultVisualPurpose(
        forevervalidator::experimental::PhysicsSandboxScenePurpose purpose);
bool IsDefaultVisualInstance(
        forevervalidator::experimental::PhysicsSandboxScenePurpose purpose,
        std::string_view blockName);

struct StaticVisualBatch {
    QByteArray vertices;
    QByteArray indices;
    QVector3D boundsMin{};
    QVector3D boundsMax{};
    ReplacementMaterialClass materialClass = ReplacementMaterialClass::Unknown;
    forevervalidator::experimental::PhysicsSandboxScenePurpose purpose =
            forevervalidator::experimental::PhysicsSandboxScenePurpose::
                    Environment;
    bool hasVertexColors = false;
    bool defaultVisible = false;
    std::uint64_t sourceInstanceCount = 0u;
    std::uint64_t triangleCount = 0u;
};

struct StaticVisualBatchResult {
    std::vector<StaticVisualBatch> batches;
    QVector3D defaultBoundsMin{};
    QVector3D defaultBoundsMax{};
    std::uint64_t sourceMeshCount = 0u;
    std::uint64_t visibleSourceInstanceCount = 0u;
    std::uint64_t defaultVisibleInstanceCount = 0u;
    std::uint64_t defaultTriangleCount = 0u;
    std::uint64_t duplicateInstanceCount = 0u;
    std::uint64_t invalidInstanceCount = 0u;
    std::uint64_t skippedGrassBladeInstanceCount = 0u;
    std::uint64_t skippedGrassBladeTriangleCount = 0u;
};

StaticVisualBatchResult BuildStaticVisualBatches(
        const forevervalidator::experimental::PhysicsSandboxRenderScene &scene);

}  // namespace forevertas::viewer

#endif
