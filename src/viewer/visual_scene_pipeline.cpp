#include "viewer/visual_scene_pipeline.h"

#include <QVector2D>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace forevertas::viewer {
namespace {

using forevervalidator::experimental::PhysicsSandboxRenderInstance;
using forevervalidator::experimental::PhysicsSandboxRenderMesh;
using forevervalidator::experimental::PhysicsSandboxRenderScene;
using forevervalidator::experimental::PhysicsSandboxScenePurpose;
using forevervalidator::experimental::PhysicsSandboxTransform;

struct VisualVertex {
    float position[3];
    float normal[3];
    float tangent[3];
    float uv0[2];
    float uv1[2];
    float color[4];
};

static_assert(sizeof(VisualVertex) == StaticVisualVertexStride);

QVector3D ToQt(const forevervalidator::Vector3 &value) {
    return {value.x, value.y, value.z};
}

void ExpandBounds(const QVector3D &point, QVector3D &minimum,
                  QVector3D &maximum) {
    minimum.setX(std::min(minimum.x(), point.x()));
    minimum.setY(std::min(minimum.y(), point.y()));
    minimum.setZ(std::min(minimum.z(), point.z()));
    maximum.setX(std::max(maximum.x(), point.x()));
    maximum.setY(std::max(maximum.y(), point.y()));
    maximum.setZ(std::max(maximum.z(), point.z()));
}

QVector3D TransformPoint(const PhysicsSandboxTransform &transform,
                         const QVector3D &point) {
    return ToQt(transform.translation) + ToQt(transform.basisX) * point.x() +
           ToQt(transform.basisY) * point.y() +
           ToQt(transform.basisZ) * point.z();
}

QVector3D TransformDirection(const PhysicsSandboxTransform &transform,
                             const QVector3D &direction) {
    return ToQt(transform.basisX) * direction.x() +
           ToQt(transform.basisY) * direction.y() +
           ToQt(transform.basisZ) * direction.z();
}

QVector3D TransformNormal(const PhysicsSandboxTransform &transform,
                          const QVector3D &normal) {
    const QVector3D x = ToQt(transform.basisX);
    const QVector3D y = ToQt(transform.basisY);
    const QVector3D z = ToQt(transform.basisZ);
    const float determinant =
            QVector3D::dotProduct(x, QVector3D::crossProduct(y, z));
    QVector3D result;
    if (std::fabs(determinant) > 1.0e-12f) {
        result = (QVector3D::crossProduct(y, z) * normal.x() +
                  QVector3D::crossProduct(z, x) * normal.y() +
                  QVector3D::crossProduct(x, y) * normal.z()) /
                 determinant;
    } else {
        result = TransformDirection(transform, normal);
    }
    return result.lengthSquared() > 1.0e-12f ? result.normalized()
                                             : QVector3D(0.0f, 1.0f, 0.0f);
}

QVector3D OrthogonalTangent(const QVector3D &normal) {
    QVector3D tangent = QVector3D::crossProduct(
            std::fabs(normal.y()) < 0.9f ? QVector3D(0.0f, 1.0f, 0.0f)
                                         : QVector3D(1.0f, 0.0f, 0.0f),
            normal);
    return tangent.lengthSquared() > 1.0e-12f ? tangent.normalized()
                                              : QVector3D(1.0f, 0.0f, 0.0f);
}

void GenerateMissingAttributes(std::vector<VisualVertex> &vertices,
                               const std::vector<std::uint32_t> &indices,
                               bool generateNormals, bool generateTangents,
                               bool generateUv0) {
    if (generateUv0) {
        for (VisualVertex &vertex : vertices) {
            vertex.uv0[0] = vertex.position[0] * 0.1f;
            vertex.uv0[1] = vertex.position[2] * 0.1f;
        }
    }
    if (generateNormals) {
        for (VisualVertex &vertex : vertices) {
            vertex.normal[0] = 0.0f;
            vertex.normal[1] = 0.0f;
            vertex.normal[2] = 0.0f;
        }
        for (std::size_t index = 0u; index + 2u < indices.size(); index += 3u) {
            const std::uint32_t ia = indices[index];
            const std::uint32_t ib = indices[index + 1u];
            const std::uint32_t ic = indices[index + 2u];
            if (ia >= vertices.size() || ib >= vertices.size() ||
                ic >= vertices.size()) {
                continue;
            }
            const QVector3D a(vertices[ia].position[0],
                              vertices[ia].position[1],
                              vertices[ia].position[2]);
            const QVector3D b(vertices[ib].position[0],
                              vertices[ib].position[1],
                              vertices[ib].position[2]);
            const QVector3D c(vertices[ic].position[0],
                              vertices[ic].position[1],
                              vertices[ic].position[2]);
            const QVector3D normal = QVector3D::crossProduct(b - a, c - a);
            for (std::uint32_t vertexIndex : {ia, ib, ic}) {
                vertices[vertexIndex].normal[0] += normal.x();
                vertices[vertexIndex].normal[1] += normal.y();
                vertices[vertexIndex].normal[2] += normal.z();
            }
        }
        for (VisualVertex &vertex : vertices) {
            QVector3D normal(vertex.normal[0], vertex.normal[1],
                             vertex.normal[2]);
            normal = normal.lengthSquared() > 1.0e-12f
                             ? normal.normalized()
                             : QVector3D(0.0f, 1.0f, 0.0f);
            vertex.normal[0] = normal.x();
            vertex.normal[1] = normal.y();
            vertex.normal[2] = normal.z();
        }
    }
    if (generateTangents) {
        for (VisualVertex &vertex : vertices) {
            const QVector3D tangent = OrthogonalTangent(QVector3D(
                    vertex.normal[0], vertex.normal[1], vertex.normal[2]));
            vertex.tangent[0] = tangent.x();
            vertex.tangent[1] = tangent.y();
            vertex.tangent[2] = tangent.z();
        }
    }
}

std::vector<VisualVertex> PrepareMesh(const PhysicsSandboxRenderMesh &mesh) {
    std::vector<VisualVertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const auto &vertex : mesh.vertices) {
        vertices.push_back(
                {{vertex.position.x, vertex.position.y, vertex.position.z},
                 {vertex.normal.x, vertex.normal.y, vertex.normal.z},
                 {vertex.tangent.x, vertex.tangent.y, vertex.tangent.z},
                 {vertex.uv0.x, vertex.uv0.y},
                 {vertex.uv1.x, vertex.uv1.y},
                 {vertex.color.x, vertex.color.y, vertex.color.z,
                  vertex.color.w}});
    }
    GenerateMissingAttributes(vertices, mesh.indices, !mesh.hasNormals,
                              !mesh.hasTangents, !mesh.hasUv0);
    return vertices;
}

struct BatchKey {
    ReplacementMaterialClass materialClass = ReplacementMaterialClass::Unknown;
    PhysicsSandboxScenePurpose purpose =
            PhysicsSandboxScenePurpose::Environment;
    bool vertexColors = false;
    bool defaultVisible = false;
    int transparentCellX = 0;
    int transparentCellZ = 0;

    auto asTuple() const {
        return std::tie(materialClass, purpose, vertexColors, defaultVisible,
                        transparentCellX, transparentCellZ);
    }
};

bool operator<(const BatchKey &left, const BatchKey &right) {
    return left.asTuple() < right.asTuple();
}

struct BatchAccumulator {
    std::vector<VisualVertex> vertices;
    std::vector<std::uint32_t> indices;
    QVector3D boundsMin{};
    QVector3D boundsMax{};
    bool hasBounds = false;
    std::uint64_t sourceInstanceCount = 0u;
};

bool IsTransparent(ReplacementMaterialClass materialClass) {
    return materialClass == ReplacementMaterialClass::Glass ||
           materialClass == ReplacementMaterialClass::Water;
}

BatchKey MakeBatchKey(ReplacementMaterialClass materialClass,
                      PhysicsSandboxScenePurpose purpose, bool vertexColors,
                      bool defaultVisible,
                      const PhysicsSandboxTransform &transform) {
    BatchKey key{materialClass, purpose, vertexColors, defaultVisible, 0, 0};
    if (IsTransparent(materialClass)) {
        constexpr float CellSize = 64.0f;
        key.transparentCellX = static_cast<int>(
                std::floor(transform.translation.x / CellSize));
        key.transparentCellZ = static_cast<int>(
                std::floor(transform.translation.z / CellSize));
    }
    return key;
}

struct DuplicateInstanceKey {
    std::uint32_t meshIndex = 0u;
    std::uint32_t materialIndex = 0u;
    PhysicsSandboxScenePurpose purpose =
            PhysicsSandboxScenePurpose::Environment;
    std::array<float, 12> transform{};

    auto asTuple() const {
        return std::tie(meshIndex, materialIndex, purpose, transform);
    }
};

bool operator<(const DuplicateInstanceKey &left,
               const DuplicateInstanceKey &right) {
    return left.asTuple() < right.asTuple();
}

DuplicateInstanceKey DuplicateKey(
        const PhysicsSandboxRenderInstance &instance) {
    return {
            instance.meshIndex,
            instance.materialIndex,
            instance.purpose,
            {instance.worldTransform.translation.x,
             instance.worldTransform.translation.y,
             instance.worldTransform.translation.z,
             instance.worldTransform.basisX.x,
             instance.worldTransform.basisX.y,
             instance.worldTransform.basisX.z,
             instance.worldTransform.basisY.x,
             instance.worldTransform.basisY.y,
             instance.worldTransform.basisY.z,
             instance.worldTransform.basisZ.x,
             instance.worldTransform.basisZ.y,
             instance.worldTransform.basisZ.z}};
}

bool IsGrassGroundCover(const PhysicsSandboxRenderMesh &mesh,
                        const PhysicsSandboxRenderInstance &instance) {
    if (!mesh.hasNormals || !mesh.hasUv0 || mesh.vertices.empty() ||
        instance.provenance.blockName.empty() ||
        (instance.purpose != PhysicsSandboxScenePurpose::PlacedBlock &&
         instance.purpose != PhysicsSandboxScenePurpose::Clip)) {
        return false;
    }
    const float width = mesh.boundsMax.x - mesh.boundsMin.x;
    const float height = mesh.boundsMax.y - mesh.boundsMin.y;
    const float depth = mesh.boundsMax.z - mesh.boundsMin.z;
    if (height > 1.0f || width < 16.0f || depth < 16.0f) {
        return false;
    }
    const std::size_t horizontalNormals = std::count_if(
            mesh.vertices.cbegin(), mesh.vertices.cend(),
            [](const auto &vertex) {
                return std::fabs(vertex.normal.y) >= 0.9f;
            });
    return horizontalNormals * 100u >= mesh.vertices.size() * 95u;
}

bool IsGrassBladeGeometry(const PhysicsSandboxRenderMesh &mesh,
                          const PhysicsSandboxRenderInstance &instance,
                          std::uint8_t surfaceMaterialId) {
    if (!mesh.hasNormals || mesh.vertices.empty() ||
        instance.provenance.blockName.empty() ||
        (instance.purpose != PhysicsSandboxScenePurpose::PlacedBlock &&
         instance.purpose != PhysicsSandboxScenePurpose::Clip) ||
        (surfaceMaterialId != 0u && surfaceMaterialId != 2u &&
         surfaceMaterialId != 20u && surfaceMaterialId != 25u) ||
        mesh.vertices.size() < 64u || mesh.indices.size() % 3u != 0u) {
        return false;
    }
    const float width = mesh.boundsMax.x - mesh.boundsMin.x;
    const float height = mesh.boundsMax.y - mesh.boundsMin.y;
    const float depth = mesh.boundsMax.z - mesh.boundsMin.z;
    if (height > 1.0f || std::max(width, depth) < 16.0f) {
        return false;
    }
    const std::size_t horizontalNormals = std::count_if(
            mesh.vertices.cbegin(), mesh.vertices.cend(),
            [](const auto &vertex) {
                return std::fabs(vertex.normal.y) >= 0.9f;
            });
    if (horizontalNormals * 100u < mesh.vertices.size() * 95u) {
        return false;
    }
    return (mesh.indices.size() / 3u) * 2u == mesh.vertices.size();
}

enum class ProjectionAxis {
    X,
    Y,
    Z,
};

ProjectionAxis DominantProjectionAxis(const QVector3D &normal) {
    const QVector3D absoluteNormal(std::fabs(normal.x()),
                                   std::fabs(normal.y()),
                                   std::fabs(normal.z()));
    if (absoluteNormal.y() >= absoluteNormal.x() &&
        absoluteNormal.y() >= absoluteNormal.z()) {
        return ProjectionAxis::Y;
    }
    return absoluteNormal.x() >= absoluteNormal.z() ? ProjectionAxis::X
                                                    : ProjectionAxis::Z;
}

QVector2D ProjectedUv(const QVector3D &position, ProjectionAxis axis,
                      float scale) {
    switch (axis) {
    case ProjectionAxis::X:
        return {position.z() * scale, position.y() * scale};
    case ProjectionAxis::Y:
        return {position.x() * scale, position.z() * scale};
    case ProjectionAxis::Z:
        return {position.x() * scale, position.y() * scale};
    }
    return {};
}

std::pair<QVector3D, QVector3D> ProjectionTangents(ProjectionAxis axis) {
    switch (axis) {
    case ProjectionAxis::X:
        return {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}};
    case ProjectionAxis::Y:
        return {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    case ProjectionAxis::Z:
        return {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    }
    return {};
}

void ApplyUvRotation(VisualVertex &vertex, float u, float v,
                     ProjectionAxis axis, unsigned quarterTurns,
                     QVector3D &tangent) {
    const auto [tangentU, tangentV] = ProjectionTangents(axis);
    switch (quarterTurns % 4u) {
    case 1u:
        vertex.uv0[0] = 1.0f - v;
        vertex.uv0[1] = u;
        tangent = -tangentV;
        break;
    case 2u:
        vertex.uv0[0] = 1.0f - u;
        vertex.uv0[1] = 1.0f - v;
        tangent = -tangentU;
        break;
    case 3u:
        vertex.uv0[0] = v;
        vertex.uv0[1] = 1.0f - u;
        tangent = tangentV;
        break;
    case 0u:
    default:
        vertex.uv0[0] = u;
        vertex.uv0[1] = v;
        tangent = tangentU;
        break;
    }
}

void ApplyWorldUvProjection(VisualVertex &vertex,
                            const QVector3D &position,
                            const QVector3D &normal,
                            float scale,
                            QVector3D &tangent) {
    const ProjectionAxis axis = DominantProjectionAxis(normal);
    const QVector2D uv = ProjectedUv(position, axis, scale);
    ApplyUvRotation(vertex, uv.x(), uv.y(), axis, 0u, tangent);
}

unsigned GrassTileQuarterTurns(std::int64_t tileU, std::int64_t tileV,
                               ProjectionAxis axis) {
    const auto coordinateHash = [](std::int64_t coordinate) {
        const std::uint64_t bits = static_cast<std::uint64_t>(coordinate);
        return static_cast<std::uint32_t>(bits ^ (bits >> 32u));
    };
    std::uint32_t hash = 0x9e3779b9u;
    const auto mix = [&hash](std::uint32_t value) {
        hash ^= value + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        hash ^= hash >> 16u;
        hash *= 0x7feb352du;
        hash ^= hash >> 15u;
    };
    mix(coordinateHash(tileU));
    mix(coordinateHash(tileV));
    mix(static_cast<std::uint32_t>(axis));
    return hash & 3u;
}

VisualVertex LerpVertex(const VisualVertex &left, const VisualVertex &right,
                        float amount) {
    VisualVertex result;
    const auto interpolate = [amount](const float *a, const float *b,
                                      float *output, std::size_t count) {
        for (std::size_t index = 0u; index < count; ++index) {
            output[index] = a[index] + (b[index] - a[index]) * amount;
        }
    };
    interpolate(left.position, right.position, result.position, 3u);
    interpolate(left.normal, right.normal, result.normal, 3u);
    interpolate(left.tangent, right.tangent, result.tangent, 3u);
    interpolate(left.uv0, right.uv0, result.uv0, 2u);
    interpolate(left.uv1, right.uv1, result.uv1, 2u);
    interpolate(left.color, right.color, result.color, 4u);
    return result;
}

std::vector<VisualVertex> ClipProjectedPolygon(
        const std::vector<VisualVertex> &input, ProjectionAxis axis,
        float scale, bool clipU, float boundary, bool keepGreater) {
    std::vector<VisualVertex> output;
    if (input.empty()) {
        return output;
    }
    output.reserve(input.size() + 1u);
    const auto coordinate = [axis, scale, clipU](const VisualVertex &vertex) {
        const QVector2D uv = ProjectedUv(
                {vertex.position[0], vertex.position[1], vertex.position[2]},
                axis, scale);
        return clipU ? uv.x() : uv.y();
    };
    VisualVertex previous = input.back();
    float previousCoordinate = coordinate(previous);
    bool previousInside = keepGreater ? previousCoordinate >= boundary
                                      : previousCoordinate <= boundary;
    for (const VisualVertex &current : input) {
        const float currentCoordinate = coordinate(current);
        const bool currentInside = keepGreater
                                           ? currentCoordinate >= boundary
                                           : currentCoordinate <= boundary;
        if (currentInside != previousInside) {
            const float denominator = currentCoordinate - previousCoordinate;
            const float amount = std::fabs(denominator) > 1.0e-12f
                                         ? std::clamp(
                                                   (boundary -
                                                    previousCoordinate) /
                                                           denominator,
                                                   0.0f, 1.0f)
                                         : 0.0f;
            output.push_back(LerpVertex(previous, current, amount));
        }
        if (currentInside) {
            output.push_back(current);
        }
        previous = current;
        previousCoordinate = currentCoordinate;
        previousInside = currentInside;
    }
    return output;
}

VisualVertex TransformVertex(const VisualVertex &source,
                             const PhysicsSandboxTransform &transform) {
    VisualVertex vertex = source;
    const QVector3D position = TransformPoint(
            transform,
            {source.position[0], source.position[1], source.position[2]});
    const QVector3D normal =
            TransformNormal(transform, {source.normal[0], source.normal[1],
                                        source.normal[2]});
    QVector3D tangent = TransformDirection(
            transform,
            {source.tangent[0], source.tangent[1], source.tangent[2]});
    tangent -= normal * QVector3D::dotProduct(normal, tangent);
    tangent = tangent.lengthSquared() > 1.0e-12f
                      ? tangent.normalized()
                      : OrthogonalTangent(normal);
    vertex.position[0] = position.x();
    vertex.position[1] = position.y();
    vertex.position[2] = position.z();
    vertex.normal[0] = normal.x();
    vertex.normal[1] = normal.y();
    vertex.normal[2] = normal.z();
    vertex.tangent[0] = tangent.x();
    vertex.tangent[1] = tangent.y();
    vertex.tangent[2] = tangent.z();
    return vertex;
}

void AppendVertex(BatchAccumulator &batch, const VisualVertex &vertex) {
    if (batch.vertices.size() ==
        std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("static visual batch exceeds U32 indices");
    }
    batch.vertices.push_back(vertex);
    const QVector3D position(vertex.position[0], vertex.position[1],
                             vertex.position[2]);
    if (!batch.hasBounds) {
        batch.boundsMin = position;
        batch.boundsMax = position;
        batch.hasBounds = true;
    } else {
        ExpandBounds(position, batch.boundsMin, batch.boundsMax);
    }
}

void AppendInstance(BatchAccumulator &batch,
                    const std::vector<VisualVertex> &sourceVertices,
                    const std::vector<std::uint32_t> &sourceIndices,
                    const PhysicsSandboxTransform &transform,
                    float worldUvScale) {
    if (batch.vertices.size() >
        std::numeric_limits<std::uint32_t>::max() - sourceVertices.size()) {
        throw std::runtime_error("static visual batch exceeds U32 indices");
    }
    const std::uint32_t baseVertex =
            static_cast<std::uint32_t>(batch.vertices.size());
    for (const VisualVertex &source : sourceVertices) {
        VisualVertex vertex = TransformVertex(source, transform);
        const QVector3D position(vertex.position[0], vertex.position[1],
                                 vertex.position[2]);
        const QVector3D normal(vertex.normal[0], vertex.normal[1],
                               vertex.normal[2]);
        QVector3D tangent(vertex.tangent[0], vertex.tangent[1],
                          vertex.tangent[2]);
        if (worldUvScale > 0.0f) {
            ApplyWorldUvProjection(vertex, position, normal, worldUvScale,
                                   tangent);
        }
        tangent -= normal * QVector3D::dotProduct(normal, tangent);
        tangent = tangent.lengthSquared() > 1.0e-12f
                          ? tangent.normalized()
                          : OrthogonalTangent(normal);
        vertex.tangent[0] = tangent.x();
        vertex.tangent[1] = tangent.y();
        vertex.tangent[2] = tangent.z();
        AppendVertex(batch, vertex);
    }
    for (std::uint32_t index : sourceIndices) {
        batch.indices.push_back(baseVertex + index);
    }
    ++batch.sourceInstanceCount;
}

void AppendRandomizedGrassInstance(
        BatchAccumulator &batch,
        const std::vector<VisualVertex> &sourceVertices,
        const std::vector<std::uint32_t> &sourceIndices,
        const PhysicsSandboxTransform &transform, float worldUvScale) {
    std::vector<VisualVertex> vertices;
    vertices.reserve(sourceVertices.size());
    for (const VisualVertex &source : sourceVertices) {
        vertices.push_back(TransformVertex(source, transform));
    }

    constexpr float BoundaryEpsilon = 1.0e-5f;
    for (std::size_t index = 0u; index + 2u < sourceIndices.size();
         index += 3u) {
        const std::uint32_t ia = sourceIndices[index];
        const std::uint32_t ib = sourceIndices[index + 1u];
        const std::uint32_t ic = sourceIndices[index + 2u];
        if (ia >= vertices.size() || ib >= vertices.size() ||
            ic >= vertices.size()) {
            continue;
        }
        const QVector3D averageNormal(
                vertices[ia].normal[0] + vertices[ib].normal[0] +
                        vertices[ic].normal[0],
                vertices[ia].normal[1] + vertices[ib].normal[1] +
                        vertices[ic].normal[1],
                vertices[ia].normal[2] + vertices[ib].normal[2] +
                        vertices[ic].normal[2]);
        const ProjectionAxis axis = DominantProjectionAxis(averageNormal);
        const std::array<QVector2D, 3> projected{{
                ProjectedUv({vertices[ia].position[0],
                             vertices[ia].position[1],
                             vertices[ia].position[2]},
                            axis, worldUvScale),
                ProjectedUv({vertices[ib].position[0],
                             vertices[ib].position[1],
                             vertices[ib].position[2]},
                            axis, worldUvScale),
                ProjectedUv({vertices[ic].position[0],
                             vertices[ic].position[1],
                             vertices[ic].position[2]},
                            axis, worldUvScale),
        }};
        const auto [minimumU, maximumU] = std::minmax(
                {projected[0].x(), projected[1].x(), projected[2].x()});
        const auto [minimumV, maximumV] = std::minmax(
                {projected[0].y(), projected[1].y(), projected[2].y()});
        std::int64_t firstTileU = static_cast<std::int64_t>(
                std::floor(minimumU + BoundaryEpsilon));
        std::int64_t lastTileU = static_cast<std::int64_t>(
                std::floor(maximumU - BoundaryEpsilon));
        std::int64_t firstTileV = static_cast<std::int64_t>(
                std::floor(minimumV + BoundaryEpsilon));
        std::int64_t lastTileV = static_cast<std::int64_t>(
                std::floor(maximumV - BoundaryEpsilon));
        if (lastTileU < firstTileU) {
            firstTileU = lastTileU = static_cast<std::int64_t>(
                    std::floor((minimumU + maximumU) * 0.5f));
        }
        if (lastTileV < firstTileV) {
            firstTileV = lastTileV = static_cast<std::int64_t>(
                    std::floor((minimumV + maximumV) * 0.5f));
        }

        for (std::int64_t tileU = firstTileU; tileU <= lastTileU; ++tileU) {
            for (std::int64_t tileV = firstTileV; tileV <= lastTileV;
                 ++tileV) {
                std::vector<VisualVertex> polygon{
                        vertices[ia], vertices[ib], vertices[ic]};
                polygon = ClipProjectedPolygon(
                        polygon, axis, worldUvScale, true,
                        static_cast<float>(tileU), true);
                polygon = ClipProjectedPolygon(
                        polygon, axis, worldUvScale, true,
                        static_cast<float>(tileU + 1), false);
                polygon = ClipProjectedPolygon(
                        polygon, axis, worldUvScale, false,
                        static_cast<float>(tileV), true);
                polygon = ClipProjectedPolygon(
                        polygon, axis, worldUvScale, false,
                        static_cast<float>(tileV + 1), false);
                if (polygon.size() < 3u) {
                    continue;
                }

                const unsigned quarterTurns =
                        GrassTileQuarterTurns(tileU, tileV, axis);
                const std::uint32_t baseVertex =
                        static_cast<std::uint32_t>(batch.vertices.size());
                for (VisualVertex &vertex : polygon) {
                    const QVector3D position(
                            vertex.position[0], vertex.position[1],
                            vertex.position[2]);
                    QVector3D normal(vertex.normal[0], vertex.normal[1],
                                     vertex.normal[2]);
                    normal = normal.lengthSquared() > 1.0e-12f
                                     ? normal.normalized()
                                     : QVector3D(0.0f, 1.0f, 0.0f);
                    const QVector2D projectedUv =
                            ProjectedUv(position, axis, worldUvScale);
                    QVector3D tangent;
                    ApplyUvRotation(
                            vertex,
                            projectedUv.x() - static_cast<float>(tileU),
                            projectedUv.y() - static_cast<float>(tileV), axis,
                            quarterTurns, tangent);
                    tangent -= normal * QVector3D::dotProduct(normal, tangent);
                    tangent = tangent.lengthSquared() > 1.0e-12f
                                      ? tangent.normalized()
                                      : OrthogonalTangent(normal);
                    vertex.normal[0] = normal.x();
                    vertex.normal[1] = normal.y();
                    vertex.normal[2] = normal.z();
                    vertex.tangent[0] = tangent.x();
                    vertex.tangent[1] = tangent.y();
                    vertex.tangent[2] = tangent.z();
                    AppendVertex(batch, vertex);
                }
                for (std::uint32_t corner = 1u;
                     corner + 1u < polygon.size(); ++corner) {
                    batch.indices.insert(
                            batch.indices.end(),
                            {baseVertex, baseVertex + corner,
                             baseVertex + corner + 1u});
                }
            }
        }
    }
    ++batch.sourceInstanceCount;
}

}  // namespace

CameraClipPlanes CalculateCameraClipPlanes(const QVector3D &cameraPosition,
                                           float cameraDistance,
                                           const QVector3D &boundsMin,
                                           const QVector3D &boundsMax) {
    float farthest = 0.0f;
    for (int corner = 0; corner < 8; ++corner) {
        const QVector3D point((corner & 1) != 0 ? boundsMax.x() : boundsMin.x(),
                              (corner & 2) != 0 ? boundsMax.y() : boundsMin.y(),
                              (corner & 4) != 0 ? boundsMax.z()
                                                : boundsMin.z());
        farthest = std::max(farthest, cameraPosition.distanceToPoint(point));
    }

    const QVector3D nearestPoint(
            std::clamp(cameraPosition.x(), boundsMin.x(), boundsMax.x()),
            std::clamp(cameraPosition.y(), boundsMin.y(), boundsMax.y()),
            std::clamp(cameraPosition.z(), boundsMin.z(), boundsMax.z()));
    const float nearest = cameraPosition.distanceToPoint(nearestPoint);
    const float distance = std::max(0.0f, cameraDistance);
    const float margin = std::max(2.0f, distance * 0.05f);
    const float farPlane = std::max(25.0f, farthest + margin);
    float nearPlane = std::max({0.1f, distance * 0.01f, farPlane / 50000.0f});
    if (nearest > 0.0f) {
        nearPlane = std::min(nearPlane, nearest * 0.5f);
        nearPlane = std::max(0.05f, nearPlane);
    }
    nearPlane = std::min(nearPlane, farPlane * 0.25f);
    return {nearPlane, farPlane};
}

bool IsDefaultVisualPurpose(PhysicsSandboxScenePurpose purpose) {
    switch (purpose) {
    case PhysicsSandboxScenePurpose::Environment:
    case PhysicsSandboxScenePurpose::PlacedBlock:
    case PhysicsSandboxScenePurpose::SubMobil:
    case PhysicsSandboxScenePurpose::Pylon:
    case PhysicsSandboxScenePurpose::Decoration:
    case PhysicsSandboxScenePurpose::Terrain:
    case PhysicsSandboxScenePurpose::Generated:
        return true;
    case PhysicsSandboxScenePurpose::Clip:
    case PhysicsSandboxScenePurpose::Helper:
    case PhysicsSandboxScenePurpose::CheckpointTrigger:
    case PhysicsSandboxScenePurpose::DedicatedInitialCollision:
        return false;
    }
    return false;
}

bool IsDefaultVisualInstance(PhysicsSandboxScenePurpose purpose,
                             std::string_view blockName) {
    if (IsDefaultVisualPurpose(purpose)) {
        return true;
    }
    return purpose == PhysicsSandboxScenePurpose::Clip &&
           blockName == "StadiumGrassClip";
}

StaticVisualBatchResult
BuildStaticVisualBatches(const PhysicsSandboxRenderScene &scene) {
    StaticVisualBatchResult result;
    result.sourceMeshCount = scene.meshes.size();
    std::vector<std::vector<VisualVertex>> preparedMeshes;
    preparedMeshes.reserve(scene.meshes.size());
    for (const PhysicsSandboxRenderMesh &mesh : scene.meshes) {
        preparedMeshes.push_back(PrepareMesh(mesh));
    }

    std::map<BatchKey, BatchAccumulator> accumulators;
    std::set<DuplicateInstanceKey> seenInstances;
    bool hasDefaultBounds = false;
    for (const PhysicsSandboxRenderInstance &instance : scene.instances) {
        if (!instance.visible || instance.lodLevel != 0u) {
            continue;
        }
        if (instance.meshIndex >= scene.meshes.size() ||
            instance.materialIndex >= scene.materials.size()) {
            ++result.invalidInstanceCount;
            continue;
        }
        ++result.visibleSourceInstanceCount;
        if (!seenInstances.insert(DuplicateKey(instance)).second) {
            ++result.duplicateInstanceCount;
            continue;
        }

        const PhysicsSandboxRenderMesh &mesh = scene.meshes[instance.meshIndex];
        const std::uint8_t surfaceMaterialId =
                scene.materials[instance.materialIndex].surfaceMaterialId;
        if (IsGrassBladeGeometry(mesh, instance, surfaceMaterialId)) {
            ++result.skippedGrassBladeInstanceCount;
            result.skippedGrassBladeTriangleCount +=
                    mesh.indices.size() / 3u;
            continue;
        }
        const MaterialSemanticContext context{
                instance.provenance.blockName,
                instance.provenance.descriptorPath,
                instance.provenance.sceneObjectId,
                instance.provenance.componentIndex, instance.purpose,
                IsGrassGroundCover(mesh, instance)};
        const ReplacementMaterialClass materialClass = ClassifyMaterial(
                scene.materials[instance.materialIndex], context);
        const ReplacementMaterial replacement = ReplacementFor(materialClass);
        const bool defaultVisible = IsDefaultVisualInstance(
                instance.purpose, instance.provenance.blockName);
        const BatchKey key =
                MakeBatchKey(materialClass, instance.purpose,
                             mesh.hasVertexColors, defaultVisible,
                             instance.worldTransform);
        if (materialClass == ReplacementMaterialClass::Grass) {
            AppendRandomizedGrassInstance(
                    accumulators[key], preparedMeshes[instance.meshIndex],
                    mesh.indices, instance.worldTransform,
                    replacement.worldUvScale);
        } else {
            AppendInstance(accumulators[key],
                           preparedMeshes[instance.meshIndex], mesh.indices,
                           instance.worldTransform, replacement.worldUvScale);
        }

        if (defaultVisible) {
            ++result.defaultVisibleInstanceCount;
            result.defaultTriangleCount += mesh.indices.size() / 3u;
            const QVector3D minimum = ToQt(mesh.boundsMin);
            const QVector3D maximum = ToQt(mesh.boundsMax);
            for (int corner = 0; corner < 8; ++corner) {
                const QVector3D local(
                        (corner & 1) != 0 ? maximum.x() : minimum.x(),
                        (corner & 2) != 0 ? maximum.y() : minimum.y(),
                        (corner & 4) != 0 ? maximum.z() : minimum.z());
                const QVector3D world =
                        TransformPoint(instance.worldTransform, local);
                if (!hasDefaultBounds) {
                    result.defaultBoundsMin = world;
                    result.defaultBoundsMax = world;
                    hasDefaultBounds = true;
                } else {
                    ExpandBounds(world, result.defaultBoundsMin,
                                 result.defaultBoundsMax);
                }
            }
        }
    }

    result.batches.reserve(accumulators.size());
    for (auto &[key, accumulator] : accumulators) {
        if (accumulator.indices.empty()) {
            continue;
        }
        StaticVisualBatch batch;
        batch.vertices = QByteArray(
                reinterpret_cast<const char *>(accumulator.vertices.data()),
                static_cast<qsizetype>(accumulator.vertices.size() *
                                       sizeof(VisualVertex)));
        batch.indices = QByteArray(
                reinterpret_cast<const char *>(accumulator.indices.data()),
                static_cast<qsizetype>(accumulator.indices.size() *
                                       sizeof(std::uint32_t)));
        batch.boundsMin = accumulator.boundsMin;
        batch.boundsMax = accumulator.boundsMax;
        batch.materialClass = key.materialClass;
        batch.purpose = key.purpose;
        batch.hasVertexColors = key.vertexColors;
        batch.defaultVisible = key.defaultVisible;
        batch.sourceInstanceCount = accumulator.sourceInstanceCount;
        batch.triangleCount = accumulator.indices.size() / 3u;
        result.batches.push_back(std::move(batch));
    }
    return result;
}

}  // namespace forevertas::viewer
