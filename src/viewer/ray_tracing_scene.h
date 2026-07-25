#ifndef FOREVERTAS_VIEWER_RAY_TRACING_SCENE_H
#define FOREVERTAS_VIEWER_RAY_TRACING_SCENE_H

#include "viewer/visual_scene_pipeline.h"

#include <QByteArray>

#include <cstdint>
#include <memory>
#include <vector>

namespace forevertas::viewer {

struct RayTracingSceneData {
    QByteArray vertices;
    QByteArray triangles;
    QByteArray bvhNodes;
    QByteArray materials;
    std::uint32_t vertexCount = 0u;
    std::uint32_t triangleCount = 0u;
    std::uint32_t bvhNodeCount = 0u;
    std::uint32_t materialCount = 0u;
};

std::shared_ptr<const RayTracingSceneData> BuildRayTracingScene(
        const std::vector<StaticVisualBatch> &batches);

}  // namespace forevertas::viewer

#endif
