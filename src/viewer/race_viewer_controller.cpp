#include "viewer/race_viewer_controller.h"

#include "time_format.h"
#include "viewer/material_classifier.h"

#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

#include <QFileInfo>
#include <QMatrix3x3>
#include <QMetaObject>
#include <QThread>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace forevertas::viewer {
namespace {

using forevervalidator::DiscriminatedResult;
using forevervalidator::experimental::PhysicsSandboxCollisionTriangle;

constexpr std::uint32_t kViewerTickDurationMs = 10u;

template<typename T, typename Error>
T Require(DiscriminatedResult<T, Error> result, const char *operation) {
    if (!result) {
        std::string message = operation;
        if (!result.Error().diagnostic.empty()) {
            message += ": ";
            message += result.Error().diagnostic;
        }
        throw std::runtime_error(std::move(message));
    }
    return std::move(result).Value();
}

QVector3D ToQt(const forevervalidator::Vector3 &value) {
    return {value.x, value.y, value.z};
}

struct FilledVertex {
    float x;
    float y;
    float z;
    float r;
    float g;
    float b;
    float a;
};

struct WireVertex {
    float x;
    float y;
    float z;
};

struct VisualVertex {
    float position[3];
    float normal[3];
    float tangent[3];
    float uv0[2];
    float uv1[2];
    float color[4];
};

struct ViewerTriangle {
    QVector3D a;
    QVector3D b;
    QVector3D c;
};

void ExpandBounds(const QVector3D &point,
                  QVector3D &minimum,
                  QVector3D &maximum) {
    minimum.setX(std::min(minimum.x(), point.x()));
    minimum.setY(std::min(minimum.y(), point.y()));
    minimum.setZ(std::min(minimum.z(), point.z()));
    maximum.setX(std::max(maximum.x(), point.x()));
    maximum.setY(std::max(maximum.y(), point.y()));
    maximum.setZ(std::max(maximum.z(), point.z()));
}

constexpr int kCarPaletteCount = 6;

std::array<float, 4u> FaceColor(const ViewerTriangle &triangle,
                                int carPalette) {
    QVector3D normal = QVector3D::crossProduct(
            triangle.b - triangle.a, triangle.c - triangle.a);
    if (normal.lengthSquared() > 0.0f) {
        normal.normalize();
    }
    const float x = std::fabs(normal.x());
    const float y = std::fabs(normal.y());
    const float z = std::fabs(normal.z());
    switch (carPalette) {
    case 0:
        // Preserve the original orange collision-car palette exactly.
        return {0.78f + 0.16f * y,
                0.26f + 0.16f * z,
                0.08f + 0.10f * x,
                1.0f};
    case 1:
        return {0.08f + 0.10f * x,
                0.26f + 0.16f * z,
                0.78f + 0.16f * y,
                1.0f};
    case 2:
        return {0.10f + 0.10f * x,
                0.65f + 0.22f * y,
                0.20f + 0.14f * z,
                1.0f};
    case 3:
        return {0.55f + 0.20f * y,
                0.18f + 0.10f * x,
                0.72f + 0.18f * z,
                1.0f};
    case 4:
        return {0.72f + 0.18f * y,
                0.58f + 0.18f * z,
                0.08f + 0.08f * x,
                1.0f};
    case 5:
        return {0.08f + 0.08f * x,
                0.62f + 0.20f * y,
                0.68f + 0.18f * z,
                1.0f};
    default:
        return {0.26f + 0.20f * x,
                0.36f + 0.26f * y,
                0.43f + 0.20f * z,
                1.0f};
    }
}

RaceViewerMeshBuffers BuildMeshBuffers(
        const std::vector<ViewerTriangle> &triangles,
        int carPalette) {
    RaceViewerMeshBuffers result;
    if (triangles.empty()) {
        return result;
    }
    constexpr qsizetype FilledBytesPerTriangle =
            static_cast<qsizetype>(3u * sizeof(FilledVertex));
    constexpr qsizetype WireBytesPerTriangle =
            static_cast<qsizetype>(6u * sizeof(WireVertex));
    if (triangles.size() > static_cast<std::size_t>(
                std::numeric_limits<qsizetype>::max() /
                std::max(FilledBytesPerTriangle, WireBytesPerTriangle))) {
        throw std::runtime_error("viewer geometry is too large");
    }

    result.filled.resize(static_cast<qsizetype>(triangles.size()) *
                         FilledBytesPerTriangle);
    result.wire.resize(static_cast<qsizetype>(triangles.size()) *
                       WireBytesPerTriangle);
    auto *filled = reinterpret_cast<FilledVertex *>(result.filled.data());
    auto *wire = reinterpret_cast<WireVertex *>(result.wire.data());
    result.boundsMin = triangles.front().a;
    result.boundsMax = triangles.front().a;

    const auto addWire = [&](const QVector3D &point) {
        *wire++ = {point.x(), point.y(), point.z()};
    };
    for (const ViewerTriangle &triangle : triangles) {
        const std::array<float, 4u> color =
                FaceColor(triangle, carPalette);
        const std::array<QVector3D, 3u> points{
                triangle.a, triangle.b, triangle.c};
        for (const QVector3D &point : points) {
            *filled++ = {point.x(), point.y(), point.z(),
                         color[0], color[1], color[2], color[3]};
            ExpandBounds(point, result.boundsMin, result.boundsMax);
        }
        addWire(triangle.a);
        addWire(triangle.b);
        addWire(triangle.b);
        addWire(triangle.c);
        addWire(triangle.c);
        addWire(triangle.a);
    }
    return result;
}

void GenerateMissingAttributes(
        std::vector<VisualVertex> &vertices,
        const std::vector<std::uint32_t> &indices,
        bool generateNormals,
        bool generateTangents,
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
        for (std::size_t index = 0u; index + 2u < indices.size();
             index += 3u) {
            const std::uint32_t ia = indices[index];
            const std::uint32_t ib = indices[index + 1u];
            const std::uint32_t ic = indices[index + 2u];
            if (ia >= vertices.size() || ib >= vertices.size() ||
                ic >= vertices.size()) {
                continue;
            }
            const QVector3D a(
                    vertices[ia].position[0],
                    vertices[ia].position[1],
                    vertices[ia].position[2]);
            const QVector3D b(
                    vertices[ib].position[0],
                    vertices[ib].position[1],
                    vertices[ib].position[2]);
            const QVector3D c(
                    vertices[ic].position[0],
                    vertices[ic].position[1],
                    vertices[ic].position[2]);
            const QVector3D normal = QVector3D::crossProduct(b - a, c - a);
            for (const std::uint32_t vertexIndex : {ia, ib, ic}) {
                vertices[vertexIndex].normal[0] += normal.x();
                vertices[vertexIndex].normal[1] += normal.y();
                vertices[vertexIndex].normal[2] += normal.z();
            }
        }
        for (VisualVertex &vertex : vertices) {
            QVector3D normal(
                    vertex.normal[0], vertex.normal[1], vertex.normal[2]);
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
            const QVector3D normal(
                    vertex.normal[0], vertex.normal[1], vertex.normal[2]);
            QVector3D tangent = QVector3D::crossProduct(
                    std::fabs(normal.y()) < 0.9f
                            ? QVector3D(0.0f, 1.0f, 0.0f)
                            : QVector3D(1.0f, 0.0f, 0.0f),
                    normal);
            tangent = tangent.lengthSquared() > 1.0e-12f
                    ? tangent.normalized()
                    : QVector3D(1.0f, 0.0f, 0.0f);
            vertex.tangent[0] = tangent.x();
            vertex.tangent[1] = tangent.y();
            vertex.tangent[2] = tangent.z();
        }
    }
}

RaceViewerVisualMeshBuffers BuildVisualMesh(
        const forevervalidator::experimental::PhysicsSandboxRenderMesh
                &source) {
    RaceViewerVisualMeshBuffers result;
    std::vector<VisualVertex> vertices;
    vertices.reserve(source.vertices.size());
    for (const auto &vertex : source.vertices) {
        vertices.push_back({
                {vertex.position.x, vertex.position.y, vertex.position.z},
                {vertex.normal.x, vertex.normal.y, vertex.normal.z},
                {vertex.tangent.x, vertex.tangent.y, vertex.tangent.z},
                {vertex.uv0.x, vertex.uv0.y},
                {vertex.uv1.x, vertex.uv1.y},
                {vertex.color.x, vertex.color.y,
                 vertex.color.z, vertex.color.w}});
    }
    GenerateMissingAttributes(
            vertices,
            source.indices,
            !source.hasNormals,
            !source.hasTangents,
            !source.hasUv0);
    result.vertices = QByteArray(
            reinterpret_cast<const char *>(vertices.data()),
            static_cast<qsizetype>(vertices.size() *
                                   sizeof(VisualVertex)));
    result.indices = QByteArray(
            reinterpret_cast<const char *>(source.indices.data()),
            static_cast<qsizetype>(source.indices.size() *
                                   sizeof(std::uint32_t)));
    result.boundsMin = ToQt(source.boundsMin);
    result.boundsMax = ToQt(source.boundsMax);
    result.hasNormals = true;
    result.hasTangents = true;
    result.hasUv0 = true;
    result.hasUv1 = source.hasUv1;
    result.hasVertexColors = source.hasVertexColors;
    for (const auto &subset : source.subsets) {
        result.subsets.emplace_back(
                static_cast<int>(subset.indexOffset),
                static_cast<int>(subset.indexCount));
    }
    return result;
}

struct DecomposedTransform {
    QVector3D position;
    QQuaternion rotation;
    QVector3D scale{1.0f, 1.0f, 1.0f};
};

DecomposedTransform Decompose(
        const forevervalidator::experimental::PhysicsSandboxTransform
                &transform) {
    DecomposedTransform result;
    result.position = ToQt(transform.translation);
    QVector3D x = ToQt(transform.basisX);
    QVector3D y = ToQt(transform.basisY);
    QVector3D z = ToQt(transform.basisZ);
    result.scale = {x.length(), y.length(), z.length()};
    if (result.scale.x() > 1.0e-8f) x /= result.scale.x();
    if (result.scale.y() > 1.0e-8f) y /= result.scale.y();
    if (result.scale.z() > 1.0e-8f) z /= result.scale.z();
    if (QVector3D::dotProduct(QVector3D::crossProduct(x, y), z) < 0.0f) {
        result.scale.setZ(-result.scale.z());
        z = -z;
    }
    QMatrix3x3 matrix;
    matrix(0, 0) = x.x();
    matrix(1, 0) = x.y();
    matrix(2, 0) = x.z();
    matrix(0, 1) = y.x();
    matrix(1, 1) = y.y();
    matrix(2, 1) = y.z();
    matrix(0, 2) = z.x();
    matrix(1, 2) = z.y();
    matrix(2, 2) = z.z();
    result.rotation = QQuaternion::fromRotationMatrix(matrix).normalized();
    return result;
}

QVector3D TransformPoint(
        const forevervalidator::experimental::PhysicsSandboxTransform
                &transform,
        const QVector3D &point) {
    return ToQt(transform.translation) +
            ToQt(transform.basisX) * point.x() +
            ToQt(transform.basisY) * point.y() +
            ToQt(transform.basisZ) * point.z();
}

QVariantMap MaterialMap(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &source) {
    const ReplacementMaterialClass materialClass =
            ClassifyMaterial(source);
    const ReplacementMaterial replacement = ReplacementFor(materialClass);
    QVariantMap map;
    map.insert(QStringLiteral("materialClass"),
               MaterialClassName(materialClass));
    map.insert(QStringLiteral("baseColor"), replacement.baseColor);
    map.insert(QStringLiteral("debugColor"), replacement.debugColor);
    map.insert(QStringLiteral("baseTexture"), replacement.baseTexture);
    map.insert(QStringLiteral("normalTexture"), replacement.normalTexture);
    map.insert(QStringLiteral("roughness"), replacement.roughness);
    map.insert(QStringLiteral("metalness"), replacement.metalness);
    map.insert(QStringLiteral("opacity"), replacement.opacity);
    map.insert(QStringLiteral("textureScale"), replacement.textureScale);
    map.insert(QStringLiteral("emissiveStrength"),
               replacement.emissiveStrength);
    map.insert(QStringLiteral("twoSided"), replacement.twoSided);
    map.insert(QStringLiteral("unknown"),
               materialClass == ReplacementMaterialClass::Unknown);
    return map;
}

std::vector<ViewerTriangle> UnitEllipsoidTriangles() {
    constexpr unsigned Latitudes = 12u;
    constexpr unsigned Longitudes = 20u;
    constexpr float Pi = 3.14159265358979323846f;
    std::vector<ViewerTriangle> triangles;
    triangles.reserve(2u * Longitudes * (Latitudes - 1u));
    const auto point = [](float phi, float theta) {
        const float ring = std::cos(phi);
        return QVector3D(ring * std::cos(theta),
                         std::sin(phi),
                         ring * std::sin(theta));
    };
    const auto appendOutward = [&triangles](QVector3D a,
                                            QVector3D b,
                                            QVector3D c) {
        const QVector3D normal = QVector3D::crossProduct(b - a, c - a);
        if (QVector3D::dotProduct(normal, a + b + c) < 0.0f) {
            std::swap(b, c);
        }
        triangles.push_back({a, b, c});
    };
    for (unsigned latitude = 0u; latitude < Latitudes; ++latitude) {
        const float phi0 = -0.5f * Pi + Pi *
                static_cast<float>(latitude) /
                static_cast<float>(Latitudes);
        const float phi1 = -0.5f * Pi + Pi *
                static_cast<float>(latitude + 1u) /
                static_cast<float>(Latitudes);
        for (unsigned longitude = 0u; longitude < Longitudes; ++longitude) {
            const float theta0 = 2.0f * Pi *
                    static_cast<float>(longitude) /
                    static_cast<float>(Longitudes);
            const float theta1 = 2.0f * Pi *
                    static_cast<float>(longitude + 1u) /
                    static_cast<float>(Longitudes);
            const QVector3D a = point(phi0, theta0);
            const QVector3D b = point(phi0, theta1);
            const QVector3D c = point(phi1, theta1);
            const QVector3D d = point(phi1, theta0);
            if (latitude != 0u) {
                appendOutward(a, b, c);
            }
            if (latitude + 1u != Latitudes) {
                appendOutward(a, c, d);
            }
        }
    }
    return triangles;
}

RaceViewerLoadResult LoadReplayData(const QString &packsDirectory,
                                    const QString &replayPath) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    RaceViewerLoadResult result;
    result.packsDirectory = packsDirectory;
    result.replayPath = replayPath;
    try {
        const ReplayIdentity identity{replayPath.toStdString()};
        AssetSource source = Require(
                OpenInstalledPackDirectory(packsDirectory.toStdString()),
                "opening Packs directory failed");
        AssetBytes bytes = Require(
                ReadNativeReplayFile(replayPath.toStdString(), identity),
                "reading replay failed");
        PhysicsSandboxOptions options;
        options.backend = SimulationBackend::OptimizedCpu;
        options.tickDurationMs = kViewerTickDurationMs;
        PhysicsSandbox sandbox = Require(
                CreatePhysicsSandbox(std::move(source), options),
                "creating replay sandbox failed");
        PhysicsSandboxStateView state = Require(
                sandbox.LoadReplay({bytes.data(), bytes.size()}, identity),
                "loading replay failed");
        PhysicsSandboxSceneView scene = Require(
                sandbox.ReadScene(), "reading replay scene failed");
        PhysicsSandboxRenderSceneHandle renderScene = Require(
                sandbox.ReadRenderScene(),
                "reading visual render scene failed");

        std::vector<ViewerTriangle> triangles;
        triangles.reserve(scene.collisionTriangles.size());
        for (const PhysicsSandboxCollisionTriangle &triangle :
             scene.collisionTriangles) {
            triangles.push_back({
                    ToQt(triangle.a), ToQt(triangle.b), ToQt(triangle.c)});
        }
        result.track = BuildMeshBuffers(triangles, -1);
        result.triangleCount = static_cast<qint64>(triangles.size());

        result.visualMeshes.reserve(renderScene->meshes.size());
        for (const PhysicsSandboxRenderMesh &mesh : renderScene->meshes) {
            result.visualMeshes.push_back(BuildVisualMesh(mesh));
        }
        std::vector<QVariantMap> materials;
        materials.reserve(renderScene->materials.size());
        for (const PhysicsSandboxRenderMaterial &material :
             renderScene->materials) {
            QVariantMap replacement = MaterialMap(material);
            if (replacement.value(QStringLiteral("unknown")).toBool()) {
                ++result.diagnosticCount;
            }
            materials.push_back(std::move(replacement));
        }
        result.materialCount =
                static_cast<qint64>(renderScene->materials.size());
        result.diagnosticCount +=
                static_cast<qint64>(renderScene->diagnostics.size());

        struct MaterialBindingKey {
            std::uint32_t materialIndex = 0u;
            bool vertexColors = false;
        };
        std::vector<MaterialBindingKey> materialBindings;
        bool hasVisualBounds = false;
        for (const PhysicsSandboxRenderInstance &instance :
             renderScene->instances) {
            if (instance.meshIndex >= renderScene->meshes.size() ||
                instance.materialIndex >= materials.size()) {
                ++result.diagnosticCount;
                continue;
            }
            const PhysicsSandboxRenderMesh &mesh =
                    renderScene->meshes[instance.meshIndex];
            const DecomposedTransform transform =
                    Decompose(instance.worldTransform);
            std::size_t materialBindingIndex = 0u;
            for (; materialBindingIndex < materialBindings.size();
                 ++materialBindingIndex) {
                const MaterialBindingKey &binding =
                        materialBindings[materialBindingIndex];
                if (binding.materialIndex == instance.materialIndex &&
                    binding.vertexColors == mesh.hasVertexColors) {
                    break;
                }
            }
            if (materialBindingIndex == materialBindings.size()) {
                materialBindings.push_back(
                        {instance.materialIndex, mesh.hasVertexColors});
                QVariantMap binding = materials[instance.materialIndex];
                binding.insert(QStringLiteral("sourceMaterialIndex"),
                               static_cast<qint64>(instance.materialIndex));
                binding.insert(QStringLiteral("vertexColors"),
                               mesh.hasVertexColors);
                result.visualMaterials.push_back(std::move(binding));
            }

            QVariantMap item;
            item.insert(QStringLiteral("meshIndex"),
                        static_cast<qint64>(instance.meshIndex));
            item.insert(QStringLiteral("materialBindingIndex"),
                        static_cast<qint64>(materialBindingIndex));
            item.insert(QStringLiteral("position"), transform.position);
            item.insert(QStringLiteral("rotation"), transform.rotation);
            item.insert(QStringLiteral("scale"), transform.scale);
            item.insert(QStringLiteral("sourceVisible"), instance.visible);
            item.insert(QStringLiteral("castsShadows"),
                        instance.castsShadows);
            item.insert(QStringLiteral("lodLevel"),
                        static_cast<qint64>(instance.lodLevel));
            item.insert(QStringLiteral("lodFarDistance"),
                        instance.lodFarDistance);
            item.insert(
                    QStringLiteral("blockName"),
                    QString::fromStdString(instance.provenance.blockName));
            item.insert(
                    QStringLiteral("collection"),
                    QString::fromStdString(instance.provenance.collection));
            item.insert(
                    QStringLiteral("descriptorPath"),
                    QString::fromStdString(
                            instance.provenance.descriptorPath));
            result.visualInstances.push_back(std::move(item));

            if (!instance.visible || instance.lodLevel != 0u) {
                continue;
            }
            result.visualTriangleCount +=
                    static_cast<qint64>(mesh.indices.size() / 3u);
            const QVector3D minimum = ToQt(mesh.boundsMin);
            const QVector3D maximum = ToQt(mesh.boundsMax);
            for (int corner = 0; corner < 8; ++corner) {
                const QVector3D local(
                        (corner & 1) != 0 ? maximum.x() : minimum.x(),
                        (corner & 2) != 0 ? maximum.y() : minimum.y(),
                        (corner & 4) != 0 ? maximum.z() : minimum.z());
                const QVector3D world =
                        TransformPoint(instance.worldTransform, local);
                if (!hasVisualBounds) {
                    result.visualBoundsMin = world;
                    result.visualBoundsMax = world;
                    hasVisualBounds = true;
                } else {
                    ExpandBounds(world,
                                 result.visualBoundsMin,
                                 result.visualBoundsMax);
                }
            }
        }
        if (!hasVisualBounds) {
            result.visualBoundsMin = result.track.boundsMin;
            result.visualBoundsMax = result.track.boundsMax;
        }

        for (const PhysicsSandboxEllipsoid &ellipsoid :
             scene.carEllipsoids) {
            QVariantMap item;
            item.insert(QStringLiteral("position"), ToQt(ellipsoid.position));
            item.insert(QStringLiteral("rotation"),
                        QQuaternion(ellipsoid.rotationW,
                                    ellipsoid.rotationX,
                                    ellipsoid.rotationY,
                                    ellipsoid.rotationZ).normalized());
            item.insert(QStringLiteral("radii"), ToQt(ellipsoid.radii));
            result.carEllipsoids.push_back(std::move(item));
        }

        const std::uint64_t frameCount =
                state.durationMs / kViewerTickDurationMs + 1u;
        result.frames.reserve(static_cast<std::size_t>(frameCount));
        const auto appendFrame = [&result](const PhysicsSandboxStateView &view) {
            result.frames.push_back({
                    static_cast<std::int64_t>(view.timeMs),
                    ToQt(view.car.position),
                    QQuaternion(view.car.rotationW,
                                view.car.rotationX,
                                view.car.rotationY,
                                view.car.rotationZ).normalized(),
                    view.accelerate,
                    view.brake,
                    view.steering});
        };
        appendFrame(state);
        for (std::uint64_t index = 1u; index < frameCount; ++index) {
            state = Require(sandbox.AdvanceTicks(1u),
                            "sampling replay timeline failed");
            appendFrame(state);
        }
    } catch (const std::exception &exception) {
        result.error = QString::fromUtf8(exception.what());
    } catch (...) {
        result.error = QStringLiteral("Unexpected replay viewer failure");
    }
    return result;
}

QString FormatTime(qint64 milliseconds) {
    return QString::fromStdString(FormatHumanDurationMilliseconds(
            static_cast<double>(std::max<qint64>(0, milliseconds))));
}

std::vector<RaceViewerFrame> ToViewerFrames(
        const std::vector<SearchTimelineFrame> &frames) {
    std::vector<RaceViewerFrame> result;
    result.reserve(frames.size());
    for (const SearchTimelineFrame &frame : frames) {
        result.push_back({
                frame.timeMs,
                QVector3D(frame.positionX,
                          frame.positionY,
                          frame.positionZ),
                QQuaternion(frame.rotationW,
                            frame.rotationX,
                            frame.rotationY,
                            frame.rotationZ).normalized(),
                frame.accelerate,
                frame.brake,
                frame.steering});
    }
    return result;
}

void UpdateRunPose(RaceViewerRun &run, qint64 timeMs) {
    if (run.frames.empty()) {
        run.position = {};
        run.rotation = {};
        return;
    }
    const auto upper = std::lower_bound(
            run.frames.begin(),
            run.frames.end(),
            timeMs,
            [](const RaceViewerFrame &frame, qint64 time) {
                return frame.timeMs < time;
            });
    if (upper == run.frames.begin()) {
        run.position = upper->position;
        run.rotation = upper->rotation;
        return;
    }
    if (upper == run.frames.end()) {
        run.position = run.frames.back().position;
        run.rotation = run.frames.back().rotation;
        return;
    }
    const RaceViewerFrame &after = *upper;
    const RaceViewerFrame &before = *(upper - 1);
    const qint64 interval = after.timeMs - before.timeMs;
    const float blend = interval > 0
            ? static_cast<float>(timeMs - before.timeMs) /
                    static_cast<float>(interval)
            : 0.0f;
    run.position = before.position * (1.0f - blend) +
            after.position * blend;
    run.rotation = QQuaternion::slerp(
            before.rotation, after.rotation, blend);
}

}  // namespace

RaceViewerController::RaceViewerController(QObject *parent)
    : QObject(parent) {
    playbackTimer_.setInterval(5);
    playbackTimer_.setTimerType(Qt::PreciseTimer);
    connect(&playbackTimer_,
            &QTimer::timeout,
            this,
            &RaceViewerController::advancePlayback);

    const std::vector<ViewerTriangle> ellipsoidTriangles =
            UnitEllipsoidTriangles();
    ellipsoidFilledGeometries_.reserve(kCarPaletteCount);
    for (int palette = 0; palette < kCarPaletteCount; ++palette) {
        const RaceViewerMeshBuffers ellipsoid =
                BuildMeshBuffers(ellipsoidTriangles, palette);
        auto geometry = std::make_unique<RaceGeometry>();
        geometry->setMesh(
                ellipsoid.filled,
                static_cast<int>(sizeof(FilledVertex)),
                QQuick3DGeometry::PrimitiveType::Triangles,
                true,
                ellipsoid.boundsMin,
                ellipsoid.boundsMax);
        if (palette == 0) {
            ellipsoidWireGeometry_.setMesh(
                    ellipsoid.wire,
                    static_cast<int>(sizeof(WireVertex)),
                    QQuick3DGeometry::PrimitiveType::Lines,
                    false,
                    ellipsoid.boundsMin,
                    ellipsoid.boundsMax);
        }
        ellipsoidFilledGeometries_.push_back(std::move(geometry));
    }
}

RaceViewerController::~RaceViewerController() {
    playbackTimer_.stop();
    waitForWorker();
}

QQuick3DGeometry *RaceViewerController::trackFilledGeometry() {
    return &trackFilledGeometry_;
}

QQuick3DGeometry *RaceViewerController::trackWireGeometry() {
    return &trackWireGeometry_;
}

QQuick3DGeometry *RaceViewerController::ellipsoidFilledGeometry() {
    return ellipsoidFilledGeometries_.front().get();
}

QQuick3DGeometry *RaceViewerController::ellipsoidWireGeometry() {
    return &ellipsoidWireGeometry_;
}

QVariantList RaceViewerController::carEllipsoids() const {
    return carEllipsoids_;
}

QVariantList RaceViewerController::visualInstances() const {
    return visualInstances_;
}

QVariantList RaceViewerController::visualMaterials() const {
    return visualMaterials_;
}

QVariantList RaceViewerController::runOptions() const {
    QVariantList options;
    options.reserve(static_cast<qsizetype>(runs_.size()));
    for (const RaceViewerRun &run : runs_) {
        QVariantMap option;
        option.insert(QStringLiteral("id"), run.id);
        option.insert(QStringLiteral("name"), run.name);
        options.push_back(std::move(option));
    }
    return options;
}

QVariantList RaceViewerController::runPoses() const {
    QVariantList poses;
    poses.reserve(static_cast<qsizetype>(runs_.size()));
    for (std::size_t index = 0u; index < runs_.size(); ++index) {
        const RaceViewerRun &run = runs_[index];
        QVariantMap pose;
        pose.insert(QStringLiteral("id"), run.id);
        pose.insert(QStringLiteral("name"), run.name);
        pose.insert(QStringLiteral("index"),
                    static_cast<qint64>(index));
        pose.insert(QStringLiteral("position"), run.position);
        pose.insert(QStringLiteral("rotation"), run.rotation);
        pose.insert(QStringLiteral("selected"),
                    run.id == selectedRunId_);
        const std::size_t paletteIndex = index %
                ellipsoidFilledGeometries_.size();
        pose.insert(
                QStringLiteral("geometry"),
                QVariant::fromValue(static_cast<QObject *>(
                        ellipsoidFilledGeometries_[paletteIndex].get())));
        poses.push_back(std::move(pose));
    }
    return poses;
}

qint64 RaceViewerController::runCount() const {
    return static_cast<qint64>(runs_.size());
}

QString RaceViewerController::selectedRunId() const {
    return selectedRunId_;
}

QVector3D RaceViewerController::carPosition() const {
    return carPosition_;
}

QQuaternion RaceViewerController::carRotation() const {
    return carRotation_;
}

qint64 RaceViewerController::durationMs() const {
    return durationMs_;
}

qint64 RaceViewerController::timeMs() const {
    return timeMs_;
}

qint64 RaceViewerController::currentTick() const {
    const RaceViewerRun *const run = selectedRun();
    if (run == nullptr || run->frames.empty()) {
        return 0;
    }
    return std::clamp<qint64>(
            timeMs_ / static_cast<qint64>(kViewerTickDurationMs),
            0,
            tickCount() - 1);
}

qint64 RaceViewerController::tickCount() const {
    const RaceViewerRun *const run = selectedRun();
    return run == nullptr
            ? 0
            : static_cast<qint64>(run->frames.size());
}

int RaceViewerController::tickDurationMs() const {
    return static_cast<int>(kViewerTickDurationMs);
}

QString RaceViewerController::timeText() const {
    return FormatTime(timeMs_) + QStringLiteral(" / ") +
            FormatTime(durationMs_);
}

bool RaceViewerController::playing() const {
    return playing_;
}

bool RaceViewerController::loaded() const {
    return loaded_;
}

bool RaceViewerController::loading() const {
    return loading_;
}

QString RaceViewerController::statusText() const {
    return statusText_;
}

qint64 RaceViewerController::triangleCount() const {
    return triangleCount_;
}

qint64 RaceViewerController::visualTriangleCount() const {
    return visualTriangleCount_;
}

qint64 RaceViewerController::visualMeshCount() const {
    return static_cast<qint64>(visualGeometries_.size());
}

qint64 RaceViewerController::materialCount() const {
    return materialCount_;
}

qint64 RaceViewerController::diagnosticCount() const {
    return diagnosticCount_;
}

qint64 RaceViewerController::ellipsoidCount() const {
    return carEllipsoids_.size();
}

double RaceViewerController::sceneRadius() const {
    return sceneRadius_;
}

RaceViewerInputSample RaceViewerController::inputSample(qint64 tick) const
        noexcept {
    const RaceViewerRun *const run = selectedRun();
    if (run == nullptr || tick < 0 ||
        tick >= static_cast<qint64>(run->frames.size())) {
        return {};
    }
    const RaceViewerFrame &frame =
            run->frames[static_cast<std::size_t>(tick)];
    return {frame.accelerate, frame.brake, frame.steering};
}

void RaceViewerController::addSearchRun(
        const QString &packsDirectory,
        const QString &replayPath,
        const std::vector<SearchTimelineFrame> &frames) {
    if (frames.empty()) {
        setStatusText(QStringLiteral("Best run produced no viewable frames."));
        return;
    }
    PendingRun pending{
            packsDirectory,
            replayPath,
            ToViewerFrames(frames)};
    if (loaded_ && loadedPacksDirectory_ == packsDirectory &&
        loadedReplayPath_ == replayPath) {
        upsertRun(QStringLiteral("best"),
                  QStringLiteral("Best"),
                  std::move(pending.frames),
                  true);
        setStatusText(QStringLiteral("Best run added"));
        return;
    }
    pendingRun_ = std::move(pending);
    if (workerThread_ == nullptr) {
        beginReplayLoad(packsDirectory, replayPath);
    }
}

void RaceViewerController::setTimeMs(qint64 value) {
    const qint64 clamped = std::clamp<qint64>(value, 0, durationMs_);
    if (timeMs_ == clamped) {
        return;
    }
    timeMs_ = clamped;
    updatePose();
    emit timeChanged();
}

void RaceViewerController::setCurrentTick(qint64 tick) {
    const RaceViewerRun *const run = selectedRun();
    if (run == nullptr || run->frames.empty()) {
        setTimeMs(0);
        return;
    }
    const qint64 clamped = std::clamp<qint64>(tick, 0, tickCount() - 1);
    setTimeMs(std::min<qint64>(
            durationMs_,
            clamped * static_cast<qint64>(kViewerTickDurationMs)));
}

void RaceViewerController::setSelectedRunId(const QString &value) {
    const auto selected = std::find_if(
            runs_.begin(), runs_.end(), [&value](const RaceViewerRun &run) {
                return run.id == value;
            });
    if (selected == runs_.end() || selectedRunId_ == value) {
        return;
    }
    pause();
    selectedRunId_ = value;
    refreshSelectedRun();
    emit selectedRunChanged();
    emit timelineChanged();
    emit timeChanged();
}

void RaceViewerController::play() {
    const RaceViewerRun *const run = selectedRun();
    if (!loaded_ || run == nullptr || run->frames.empty() || playing_) {
        return;
    }
    if (timeMs_ >= durationMs_) {
        setCurrentTick(0);
    } else {
        setCurrentTick(currentTick());
    }
    playbackStartTick_ = currentTick();
    playbackClock_.restart();
    playbackTimer_.start();
    setPlaying(true);
}

void RaceViewerController::pause() {
    playbackTimer_.stop();
    setPlaying(false);
}

void RaceViewerController::togglePlayback() {
    if (playing_) {
        pause();
    } else {
        play();
    }
}

void RaceViewerController::jumpToStart() {
    pause();
    setCurrentTick(0);
}

void RaceViewerController::jumpToEnd() {
    pause();
    setTimeMs(durationMs_);
}

void RaceViewerController::loadReplay(const QString &packsDirectory,
                                      const QString &replayPath) {
    pendingRun_.reset();
    if (workerThread_ != nullptr) {
        queuedReplayLoad_ = ReplayLoadRequest{packsDirectory, replayPath};
        setLoading(true);
        setStatusText(QStringLiteral("Waiting to load selected replay..."));
        return;
    }
    beginReplayLoad(packsDirectory, replayPath);
}

void RaceViewerController::beginReplayLoad(const QString &packsDirectory,
                                           const QString &replayPath) {
    if (workerThread_ != nullptr) {
        queuedReplayLoad_ = ReplayLoadRequest{packsDirectory, replayPath};
        setLoading(true);
        setStatusText(QStringLiteral("Waiting to load selected replay..."));
        return;
    }
    queuedReplayLoad_.reset();
    const QFileInfo packsInfo(packsDirectory);
    const QFileInfo replayInfo(replayPath);
    if (!packsInfo.isDir() || !packsInfo.isReadable()) {
        pendingRun_.reset();
        setStatusText(QStringLiteral(
                "Select a readable installed Packs directory."));
        setLoading(false);
        return;
    }
    if (!replayInfo.isFile() || !replayInfo.isReadable()) {
        pendingRun_.reset();
        setStatusText(QStringLiteral("Select a readable replay file."));
        setLoading(false);
        return;
    }

    // Keep the published 3D scene attached until the replacement is complete.
    // Publishing an empty run/ellipsoid model detaches nested Repeater3D render
    // nodes on some Qt Quick 3D backends.
    setLoading(true);
    setStatusText(QStringLiteral(
            "Loading visual geometry, materials, and replay..."));

    const std::uint64_t loadSerial = ++loadSerial_;
    QThread *const thread = QThread::create(
            [this, packsDirectory, replayPath, loadSerial]() {
                RaceViewerLoadResult result =
                        LoadReplayData(packsDirectory, replayPath);
                QMetaObject::invokeMethod(
                        this,
                        [this, loadSerial,
                         result = std::move(result)]() mutable {
                            applyLoadResult(loadSerial, std::move(result));
                        },
                        Qt::QueuedConnection);
            });
    workerThread_ = thread;
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (workerThread_ == thread) {
            workerThread_ = nullptr;
        }
        if (queuedReplayLoad_) {
            const ReplayLoadRequest request = *queuedReplayLoad_;
            queuedReplayLoad_.reset();
            beginReplayLoad(request.packsDirectory, request.replayPath);
            return;
        }
        if (pendingRun_ &&
            (!loaded_ ||
             loadedPacksDirectory_ != pendingRun_->packsDirectory ||
             loadedReplayPath_ != pendingRun_->replayPath)) {
            beginReplayLoad(pendingRun_->packsDirectory,
                            pendingRun_->replayPath);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void RaceViewerController::applyLoadResult(
        std::uint64_t loadSerial,
        RaceViewerLoadResult result) {
    if (loadSerial != loadSerial_) return;
    pause();
    if (!result.error.isEmpty()) {
        pendingRun_.reset();
        setStatusText(result.error);
        if (!queuedReplayLoad_) setLoading(false);
        return;
    }
    if (result.frames.empty()) {
        pendingRun_.reset();
        setStatusText(QStringLiteral("Replay produced no viewable frames."));
        if (!queuedReplayLoad_) setLoading(false);
        return;
    }

    trackFilledGeometry_.setMesh(
            std::move(result.track.filled),
            static_cast<int>(sizeof(FilledVertex)),
            QQuick3DGeometry::PrimitiveType::Triangles,
            true,
            result.track.boundsMin,
            result.track.boundsMax);
    trackWireGeometry_.setMesh(
            std::move(result.track.wire),
            static_cast<int>(sizeof(WireVertex)),
            QQuick3DGeometry::PrimitiveType::Lines,
            false,
            result.track.boundsMin,
            result.track.boundsMax);
    std::vector<std::unique_ptr<RaceGeometry>> visualGeometries;
    visualGeometries.reserve(result.visualMeshes.size());
    for (RaceViewerVisualMeshBuffers &mesh : result.visualMeshes) {
        auto geometry = std::make_unique<RaceGeometry>();
        geometry->setIndexedMesh(
                std::move(mesh.vertices),
                std::move(mesh.indices),
                static_cast<int>(sizeof(VisualVertex)),
                mesh.hasNormals,
                mesh.hasTangents,
                mesh.hasUv0,
                mesh.hasUv1,
                mesh.hasVertexColors,
                mesh.boundsMin,
                mesh.boundsMax,
                mesh.subsets);
        visualGeometries.push_back(std::move(geometry));
    }
    QVariantList visualInstances;
    visualInstances.reserve(result.visualInstances.size());
    for (QVariant &entry : result.visualInstances) {
        QVariantMap item = entry.toMap();
        const qint64 meshIndex =
                item.value(QStringLiteral("meshIndex")).toLongLong();
        if (meshIndex < 0 ||
            meshIndex >= static_cast<qint64>(visualGeometries.size())) {
            continue;
        }
        item.insert(
                QStringLiteral("geometry"),
                QVariant::fromValue(static_cast<QObject *>(
                        visualGeometries[
                                static_cast<std::size_t>(meshIndex)].get())));
        visualInstances.push_back(std::move(item));
    }
    visualGeometries_ = std::move(visualGeometries);
    visualMaterials_ = std::move(result.visualMaterials);
    visualInstances_ = std::move(visualInstances);
    carEllipsoids_ = std::move(result.carEllipsoids);
    triangleCount_ = result.triangleCount;
    visualTriangleCount_ = result.visualTriangleCount;
    materialCount_ = result.materialCount;
    diagnosticCount_ = result.diagnosticCount;
    sceneRadius_ = std::max(
            1.0, 0.5 * static_cast<double>(
                    (result.visualBoundsMax -
                     result.visualBoundsMin).length()));
    timeMs_ = 0;
    loadedPacksDirectory_ = result.packsDirectory;
    loadedReplayPath_ = result.replayPath;
    loaded_ = true;
    runs_.clear();
    selectedRunId_.clear();
    upsertRun(QStringLiteral("baseline"),
              QStringLiteral("Baseline"),
              std::move(result.frames),
              true);
    applyPendingRunIfReady();
    setStatusText(QStringLiteral("Replay and visual map loaded"));
    if (!queuedReplayLoad_) setLoading(false);
    emit sceneChanged();
    emit stateChanged();
}

void RaceViewerController::applyPendingRunIfReady() {
    if (!pendingRun_ || !loaded_ ||
        pendingRun_->packsDirectory != loadedPacksDirectory_ ||
        pendingRun_->replayPath != loadedReplayPath_) {
        return;
    }
    std::vector<RaceViewerFrame> frames =
            std::move(pendingRun_->frames);
    pendingRun_.reset();
    upsertRun(QStringLiteral("best"),
              QStringLiteral("Best"),
              std::move(frames),
              true);
}

void RaceViewerController::setLoading(bool value) {
    if (loading_ == value) {
        return;
    }
    loading_ = value;
    emit stateChanged();
}

void RaceViewerController::setStatusText(const QString &value) {
    if (statusText_ == value) {
        return;
    }
    statusText_ = value;
    emit stateChanged();
}

const RaceViewerRun *RaceViewerController::selectedRun() const noexcept {
    const auto selected = std::find_if(
            runs_.begin(), runs_.end(), [this](const RaceViewerRun &run) {
                return run.id == selectedRunId_;
            });
    return selected == runs_.end() ? nullptr : &*selected;
}

RaceViewerRun *RaceViewerController::selectedRun() noexcept {
    const auto selected = std::find_if(
            runs_.begin(), runs_.end(), [this](const RaceViewerRun &run) {
                return run.id == selectedRunId_;
            });
    return selected == runs_.end() ? nullptr : &*selected;
}

void RaceViewerController::upsertRun(QString id,
                                     QString name,
                                     std::vector<RaceViewerFrame> frames,
                                     bool select) {
    if (frames.empty()) {
        return;
    }
    auto existing = std::find_if(
            runs_.begin(), runs_.end(), [&id](const RaceViewerRun &run) {
                return run.id == id;
            });
    const QString runId = id;
    if (existing == runs_.end()) {
        runs_.push_back({std::move(id),
                         std::move(name),
                         std::move(frames),
                         {},
                         {}});
    } else {
        existing->name = std::move(name);
        existing->frames = std::move(frames);
    }
    emit runsChanged();

    if (selectedRunId_.isEmpty()) {
        selectedRunId_ = runId;
        emit selectedRunChanged();
    } else if (select && selectedRunId_ != runId) {
        setSelectedRunId(runId);
        return;
    }
    refreshSelectedRun();
    emit timelineChanged();
    emit timeChanged();
}

void RaceViewerController::refreshSelectedRun() {
    const RaceViewerRun *const run = selectedRun();
    durationMs_ = run == nullptr || run->frames.empty()
            ? 0
            : static_cast<qint64>(run->frames.back().timeMs);
    timeMs_ = std::clamp<qint64>(timeMs_, 0, durationMs_);
    updatePose();
}

void RaceViewerController::waitForWorker() {
    if (workerThread_ != nullptr) {
        workerThread_->requestInterruption();
        workerThread_->quit();
        workerThread_->wait();
        workerThread_ = nullptr;
    }
}

void RaceViewerController::updatePose() {
    if (runs_.empty()) {
        carPosition_ = {};
        carRotation_ = {};
        emit poseChanged();
        return;
    }
    for (RaceViewerRun &run : runs_) {
        UpdateRunPose(run, timeMs_);
    }
    const RaceViewerRun *const run = selectedRun();
    if (run != nullptr) {
        carPosition_ = run->position;
        carRotation_ = run->rotation;
    }
    emit poseChanged();
}

void RaceViewerController::advancePlayback() {
    const RaceViewerRun *const run = selectedRun();
    if (!playing_ || run == nullptr || run->frames.empty()) {
        return;
    }
    const qint64 elapsedTicks = playbackClock_.elapsed() /
            static_cast<qint64>(kViewerTickDurationMs);
    const qint64 targetTick = playbackStartTick_ + elapsedTicks;
    if (targetTick >= tickCount() - 1) {
        setTimeMs(durationMs_);
        pause();
        return;
    }
    setCurrentTick(targetTick);
}

void RaceViewerController::setPlaying(bool value) {
    if (playing_ == value) {
        return;
    }
    playing_ = value;
    emit playbackChanged();
}

}  // namespace forevertas::viewer
