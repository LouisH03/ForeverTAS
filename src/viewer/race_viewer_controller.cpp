#include "viewer/race_viewer_controller.h"

#include "mutations/input_event_formatter.h"
#include "replay_file_io.h"
#include "time_format.h"
#include "viewer/material_classifier.h"

#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

#include <QFileInfo>
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

class ManualDriveRuntime {
public:
    ManualDriveRuntime(
            forevervalidator::experimental::PhysicsSandbox sandboxValue,
            forevervalidator::experimental::PhysicsSandboxState initialValue,
            std::vector<
                    forevervalidator::experimental::PhysicsSandboxInputEvent>
                    fixedValue,
            forevervalidator::experimental::PhysicsSandboxStateView stateValue)
        : sandbox(std::move(sandboxValue)),
          initialState(std::move(initialValue)),
          fixedInputs(std::move(fixedValue)),
          state(stateValue) {}

    forevervalidator::experimental::PhysicsSandbox sandbox;
    forevervalidator::experimental::PhysicsSandboxState initialState;
    std::vector<
            forevervalidator::experimental::PhysicsSandboxInputEvent>
            fixedInputs;
    std::vector<
            forevervalidator::experimental::PhysicsSandboxInputEvent>
            driverInputs;
    forevervalidator::experimental::PhysicsSandboxStateView state{};
};

namespace {

using forevervalidator::DiscriminatedResult;
using forevervalidator::experimental::PhysicsSandboxCollisionTriangle;
using forevervalidator::experimental::PhysicsSandboxInputAction;
using forevervalidator::experimental::PhysicsSandboxInputEvent;
using forevervalidator::experimental::PhysicsSandboxInputValueKind;
using forevervalidator::experimental::PhysicsSandboxState;
using forevervalidator::experimental::PhysicsSandboxStateView;
using forevervalidator::experimental::PhysicsSandboxSwitchState;

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

bool IsDriverInput(PhysicsSandboxInputAction action) {
    return action == PhysicsSandboxInputAction::Accelerate ||
            action == PhysicsSandboxInputAction::Gas ||
            action == PhysicsSandboxInputAction::Brake ||
            action == PhysicsSandboxInputAction::Steer ||
            action == PhysicsSandboxInputAction::SteerLeft ||
            action == PhysicsSandboxInputAction::SteerRight;
}

PhysicsSandboxInputEvent ManualSwitchEvent(
        std::int32_t timeMs,
        PhysicsSandboxInputAction action,
        bool active) {
    PhysicsSandboxInputEvent event;
    event.timeMs = timeMs;
    event.action = action;
    event.value.kind = PhysicsSandboxInputValueKind::Switch;
    event.value.switchState = active
            ? PhysicsSandboxSwitchState::Pressed
            : PhysicsSandboxSwitchState::Released;
    return event;
}

QString SandboxErrorText(
        const forevervalidator::experimental::PhysicsSandboxError &error) {
    return error.diagnostic.empty()
            ? QStringLiteral("physics simulation failed")
            : QString::fromUtf8(error.diagnostic);
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

QVariantMap MaterialMap(ReplacementMaterialClass materialClass) {
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
    map.insert(QStringLiteral("textureScale"),
               replacement.worldUvScale > 0.0f
                       ? 1.0f
                       : replacement.textureScale);
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

RaceViewerLoadResult LoadMapData(const QString &packsDirectory,
                                 const QString &replayPath,
                                 PhysicsBackend backend) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    RaceViewerLoadResult result;
    result.packsDirectory = packsDirectory;
    result.replayPath = replayPath;
    try {
        const std::string replayPathUtf8 =
                replayPath.toUtf8().toStdString();
        const ReplayIdentity identity{replayPathUtf8};
        AssetSource source = Require(
                OpenInstalledPackDirectory(
                        packsDirectory.toUtf8().toStdString()),
                "opening Packs directory failed");
        AssetBytes bytes = Require(
                ReadReplayFileUtf8(replayPathUtf8, identity),
                "reading replay failed");
        PhysicsSandboxOptions options;
        options.backend = ToForeverValidatorBackend(backend);
        options.tickDurationMs = kViewerTickDurationMs;
        PhysicsSandbox sandbox = Require(
                CreatePhysicsSandbox(std::move(source), options),
                "creating replay sandbox failed");
        const PhysicsSandboxStateView initialState = Require(
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

        StaticVisualBatchResult batches =
                BuildStaticVisualBatches(*renderScene);
        result.rayTracingScene = BuildRayTracingScene(batches.batches);
        result.materialCount =
                static_cast<qint64>(renderScene->materials.size());
        result.diagnosticCount +=
                static_cast<qint64>(renderScene->diagnostics.size()) +
                static_cast<qint64>(batches.invalidInstanceCount) +
                static_cast<qint64>(batches.duplicateInstanceCount);
        result.sourceVisualObjectCount =
                static_cast<qint64>(batches.defaultVisibleInstanceCount);
        result.sourceVisualMeshCount =
                static_cast<qint64>(batches.sourceMeshCount);
        result.duplicateVisualObjectCount =
                static_cast<qint64>(batches.duplicateInstanceCount);
        result.visualTriangleCount =
                static_cast<qint64>(batches.defaultTriangleCount);
        result.visualBoundsMin = batches.defaultBoundsMin;
        result.visualBoundsMax = batches.defaultBoundsMax;

        struct MaterialBindingKey {
            ReplacementMaterialClass materialClass =
                    ReplacementMaterialClass::Unknown;
            bool vertexColors = false;
        };
        std::vector<MaterialBindingKey> materialBindings;
        result.visualBatches = std::move(batches.batches);
        result.visualBatchItems.reserve(result.visualBatches.size());
        for (std::size_t batchIndex = 0u;
             batchIndex < result.visualBatches.size(); ++batchIndex) {
            const StaticVisualBatch &batch = result.visualBatches[batchIndex];
            const bool applyVertexColors =
                    batch.hasVertexColors &&
                    ReplacementFor(batch.materialClass).applyVertexColors;
            std::size_t materialBindingIndex = 0u;
            for (; materialBindingIndex < materialBindings.size();
                 ++materialBindingIndex) {
                const MaterialBindingKey &binding =
                        materialBindings[materialBindingIndex];
                if (binding.materialClass == batch.materialClass &&
                    binding.vertexColors == applyVertexColors) {
                    break;
                }
            }
            if (materialBindingIndex == materialBindings.size()) {
                materialBindings.push_back(
                        {batch.materialClass, applyVertexColors});
                QVariantMap binding = MaterialMap(batch.materialClass);
                binding.insert(QStringLiteral("vertexColors"),
                               applyVertexColors);
                if (batch.materialClass == ReplacementMaterialClass::Unknown) {
                    ++result.diagnosticCount;
                }
                result.visualMaterials.push_back(std::move(binding));
            }

            QVariantMap item;
            item.insert(QStringLiteral("batchIndex"),
                        static_cast<qint64>(batchIndex));
            item.insert(QStringLiteral("materialBindingIndex"),
                        static_cast<qint64>(materialBindingIndex));
            item.insert(QStringLiteral("materialClass"),
                        MaterialClassName(batch.materialClass));
            item.insert(QStringLiteral("defaultVisible"),
                        batch.defaultVisible);
            item.insert(QStringLiteral("sourceInstanceCount"),
                        static_cast<qint64>(batch.sourceInstanceCount));
            item.insert(QStringLiteral("triangleCount"),
                        static_cast<qint64>(batch.triangleCount));
            result.visualBatchItems.push_back(std::move(item));
        }
        if (result.sourceVisualObjectCount == 0) {
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

        const std::vector<PhysicsSandboxInputEvent> replayInputs =
                Require(sandbox.ReadInputs(),
                        "reading replay inputs for manual driving failed");
        std::vector<PhysicsSandboxInputEvent> fixedInputs;
        fixedInputs.reserve(replayInputs.size());
        std::copy_if(
                replayInputs.begin(),
                replayInputs.end(),
                std::back_inserter(fixedInputs),
                [](const PhysicsSandboxInputEvent &event) {
                    return !IsDriverInput(event.action);
                });
        PhysicsSandboxState manualStart = Require(
                sandbox.CaptureState(),
                "capturing manual-driving start state failed");
        result.manualRuntime = std::make_shared<ManualDriveRuntime>(
                std::move(sandbox),
                std::move(manualStart),
                std::move(fixedInputs),
                initialState);
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

RaceViewerFrame ToViewerFrame(const PhysicsSandboxStateView &state) {
    return {
            static_cast<std::int64_t>(state.timeMs),
            ToQt(state.car.position),
            QQuaternion(state.car.rotationW,
                        state.car.rotationX,
                        state.car.rotationY,
                        state.car.rotationZ).normalized(),
            state.accelerate,
            state.brake,
            state.steering};
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
    manualDriveTimer_.setInterval(5);
    manualDriveTimer_.setTimerType(Qt::PreciseTimer);
    connect(&manualDriveTimer_,
            &QTimer::timeout,
            this,
            &RaceViewerController::advanceManualDrive);

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
    manualDriveTimer_.stop();
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
    return visualBatches_;
}

QVariantList RaceViewerController::visualBatches() const {
    return visualBatches_;
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

bool RaceViewerController::manualDriving() const {
    return manualDriving_;
}

bool RaceViewerController::manualLeft() const {
    return manualLeft_;
}

bool RaceViewerController::manualRight() const {
    return manualRight_;
}

bool RaceViewerController::manualAccelerate() const {
    return manualAccelerate_;
}

bool RaceViewerController::manualBrake() const {
    return manualBrake_;
}

bool RaceViewerController::canCopyCurrentInputs() const {
    const RaceViewerRun *const run = selectedRun();
    if (run == nullptr) {
        return false;
    }
    return run->id == QStringLiteral("manual")
            ? manualRuntime_ != nullptr
            : !run->inputs.empty();
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
    return sourceVisualMeshCount_;
}

qint64 RaceViewerController::visualBatchCount() const {
    return static_cast<qint64>(visualGeometries_.size());
}

qint64 RaceViewerController::sourceVisualObjectCount() const {
    return sourceVisualObjectCount_;
}

qint64 RaceViewerController::shadowCount() const { return 0; }

qint64 RaceViewerController::materialCount() const {
    return materialCount_;
}

qint64 RaceViewerController::diagnosticCount() const {
    return diagnosticCount_;
}

qint64 RaceViewerController::ellipsoidCount() const {
    return carEllipsoids_.size();
}

double RaceViewerController::sceneRadius() const { return sceneRadius_; }

QVector3D RaceViewerController::sceneBoundsMin() const {
    return sceneBoundsMin_;
}

QVector3D RaceViewerController::sceneBoundsMax() const {
    return sceneBoundsMax_;
}

std::shared_ptr<const RayTracingSceneData>
RaceViewerController::rayTracingScene() const {
    return rayTracingScene_;
}

QVector2D
RaceViewerController::cameraClipPlanes(const QVector3D &cameraPosition,
                                       double cameraDistance) const {
    const CameraClipPlanes planes = CalculateCameraClipPlanes(
            cameraPosition, static_cast<float>(cameraDistance), sceneBoundsMin_,
            sceneBoundsMax_);
    return {planes.nearPlane, planes.farPlane};
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
    addSearchRun(packsDirectory,
                 replayPath,
                 frames,
                 std::vector<SandboxInputEvent>{},
                 QStringLiteral("optimized-cpu"));
}

void RaceViewerController::addSearchRun(
        const QString &packsDirectory,
        const QString &replayPath,
        const std::vector<SearchTimelineFrame> &frames,
        const QString &backendId) {
    addSearchRun(packsDirectory,
                 replayPath,
                 frames,
                 std::vector<SandboxInputEvent>{},
                 backendId);
}

void RaceViewerController::addSearchRun(
        const QString &packsDirectory,
        const QString &replayPath,
        const std::vector<SearchTimelineFrame> &frames,
        const std::vector<SandboxInputEvent> &inputs) {
    addSearchRun(packsDirectory,
                 replayPath,
                 frames,
                 inputs,
                 QStringLiteral("optimized-cpu"));
}

void RaceViewerController::addSearchRun(
        const QString &packsDirectory,
        const QString &replayPath,
        const std::vector<SearchTimelineFrame> &frames,
        const std::vector<SandboxInputEvent> &inputs,
        const QString &backendId) {
    stopManualDrive();
    if (frames.empty()) {
        setStatusText(QStringLiteral("Best run produced no viewable frames."));
        return;
    }
    const std::optional<PhysicsBackend> backend =
            ParsePhysicsBackend(backendId.toStdString());
    if (!backend) {
        setStatusText(QStringLiteral("Select a valid physics backend."));
        return;
    }
    PendingRun pending{
            packsDirectory,
            replayPath,
            *backend,
            ToViewerFrames(frames),
            inputs};
    if (loaded_ && loadedPacksDirectory_ == packsDirectory &&
        loadedReplayPath_ == replayPath) {
        upsertRun(QStringLiteral("best"),
                  QStringLiteral("Best"),
                  std::move(pending.frames),
                  std::move(pending.inputs),
                  true);
        setStatusText(QStringLiteral("Best run added"));
        return;
    }
    pendingRun_ = std::move(pending);
    if (workerThread_ == nullptr) {
        beginMapLoad(packsDirectory, replayPath, *backend);
    }
}

void RaceViewerController::setTimeMs(qint64 value) {
    if (manualDriving_) {
        return;
    }
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
    if (manualDriving_) {
        return;
    }
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
    if (!loaded_ || manualDriving_ || run == nullptr ||
        run->frames.empty() || playing_) {
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

void RaceViewerController::startManualDrive() {
    if (manualDriving_) {
        return;
    }
    if (!loaded_ || loading_ || manualRuntime_ == nullptr) {
        setStatusText(QStringLiteral(
                "Load a replay map before starting manual drive."));
        return;
    }

    pause();
    auto restored =
            manualRuntime_->sandbox.RestoreState(
                    manualRuntime_->initialState);
    if (!restored) {
        setStatusText(
                QStringLiteral("Manual drive failed: %1")
                        .arg(SandboxErrorText(restored.Error())));
        return;
    }
    manualRuntime_->state = restored.Value();
    manualRuntime_->driverInputs.clear();
    resetManualInputState();
    if (!replaceManualInputs()) {
        return;
    }

    upsertRun(
            QStringLiteral("manual"),
            QStringLiteral("Manual"),
            {ToViewerFrame(manualRuntime_->state)},
            {},
            true);
    manualDriving_ = true;
    setStatusText(QStringLiteral("Manual drive"));
    manualDriveClock_.restart();
    manualDriveTimer_.start();
    emit manualDrivingChanged();
}

void RaceViewerController::stopManualDrive() {
    finishManualDrive(QStringLiteral("Manual drive stopped"), true);
}

void RaceViewerController::setManualInput(const QString &input,
                                         bool active) {
    if (!manualDriving_ || manualRuntime_ == nullptr) {
        return;
    }

    bool *state = nullptr;
    PhysicsSandboxInputAction action =
            PhysicsSandboxInputAction::Unmapped;
    if (input == QStringLiteral("left")) {
        state = &manualLeft_;
        action = PhysicsSandboxInputAction::SteerLeft;
    } else if (input == QStringLiteral("right")) {
        state = &manualRight_;
        action = PhysicsSandboxInputAction::SteerRight;
    } else if (input == QStringLiteral("accelerate")) {
        state = &manualAccelerate_;
        action = PhysicsSandboxInputAction::Accelerate;
    } else if (input == QStringLiteral("brake")) {
        state = &manualBrake_;
        action = PhysicsSandboxInputAction::Brake;
    } else {
        return;
    }
    if (*state == active) {
        return;
    }

    *state = active;
    const std::uint64_t time = manualRuntime_->state.timeMs;
    const std::int32_t eventTime = static_cast<std::int32_t>(
            std::min<std::uint64_t>(
                    time,
                    static_cast<std::uint64_t>(
                            std::numeric_limits<std::int32_t>::max())));
    manualRuntime_->driverInputs.push_back(
            ManualSwitchEvent(eventTime, action, active));
    if (!replaceManualInputs()) {
        manualRuntime_->driverInputs.pop_back();
        *state = !active;
        finishManualDrive(statusText_, false);
        return;
    }
    emit manualInputChanged();
}

void RaceViewerController::releaseManualInputs() {
    if (!manualDriving_) {
        resetManualInputState();
        return;
    }
    setManualInput(QStringLiteral("left"), false);
    setManualInput(QStringLiteral("right"), false);
    setManualInput(QStringLiteral("accelerate"), false);
    setManualInput(QStringLiteral("brake"), false);
}

QString RaceViewerController::currentInputScript() const {
    const RaceViewerRun *const run = selectedRun();
    if (run == nullptr) {
        return {};
    }

    std::vector<SandboxInputEvent> inputs;
    if (run->id == QStringLiteral("manual")) {
        if (manualRuntime_ == nullptr) {
            return {};
        }
        inputs = manualRuntime_->fixedInputs;
        inputs.insert(
                inputs.end(),
                manualRuntime_->driverInputs.begin(),
                manualRuntime_->driverInputs.end());
    } else {
        inputs = run->inputs;
    }

    std::int64_t raceStartTimeMs = 0;
    bool foundRaceStart = false;
    for (const SandboxInputEvent &event : inputs) {
        if (event.action != PhysicsSandboxInputAction::RaceRunning) {
            continue;
        }
        if (!foundRaceStart || event.timeMs < raceStartTimeMs) {
            raceStartTimeMs = event.timeMs;
            foundRaceStart = true;
        }
    }
    const std::int64_t cutoffTimeMs = std::clamp<std::int64_t>(
            raceStartTimeMs + timeMs_,
            std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::max());
    inputs.erase(
            std::remove_if(
                    inputs.begin(),
                    inputs.end(),
                    [cutoffTimeMs](const SandboxInputEvent &event) {
                        return event.timeMs > cutoffTimeMs;
                    }),
            inputs.end());
    return QString::fromStdString(FormatInputScript(inputs));
}

void RaceViewerController::loadMap(const QString &packsDirectory,
                                   const QString &replayPath) {
    loadMap(packsDirectory,
            replayPath,
            QStringLiteral("optimized-cpu"));
}

void RaceViewerController::loadMap(const QString &packsDirectory,
                                   const QString &replayPath,
                                   const QString &backendId) {
    stopManualDrive();
    const std::optional<PhysicsBackend> backend =
            ParsePhysicsBackend(backendId.toStdString());
    if (!backend) {
        setStatusText(QStringLiteral("Select a valid physics backend."));
        return;
    }
    pendingRun_.reset();
    if (workerThread_ != nullptr) {
        queuedMapLoad_ =
                MapLoadRequest{packsDirectory, replayPath, *backend};
        setLoading(true);
        setStatusText(QStringLiteral("Waiting to load selected map..."));
        return;
    }
    beginMapLoad(packsDirectory, replayPath, *backend);
}

void RaceViewerController::beginMapLoad(const QString &packsDirectory,
                                        const QString &replayPath,
                                        PhysicsBackend backend) {
    if (workerThread_ != nullptr) {
        queuedMapLoad_ =
                MapLoadRequest{packsDirectory, replayPath, backend};
        setLoading(true);
        setStatusText(QStringLiteral("Waiting to load selected map..."));
        return;
    }
    queuedMapLoad_.reset();
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
            "Loading map geometry and materials..."));

    const std::uint64_t loadSerial = ++loadSerial_;
    QThread *const thread = QThread::create(
            [this, packsDirectory, replayPath, backend, loadSerial]() {
                RaceViewerLoadResult result =
                        LoadMapData(packsDirectory, replayPath, backend);
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
        if (queuedMapLoad_) {
            const MapLoadRequest request = *queuedMapLoad_;
            queuedMapLoad_.reset();
            beginMapLoad(request.packsDirectory,
                         request.replayPath,
                         request.backend);
            return;
        }
        if (pendingRun_ &&
            (!loaded_ ||
             loadedPacksDirectory_ != pendingRun_->packsDirectory ||
             loadedReplayPath_ != pendingRun_->replayPath)) {
            beginMapLoad(pendingRun_->packsDirectory,
                         pendingRun_->replayPath,
                         pendingRun_->backend);
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
        if (!queuedMapLoad_) setLoading(false);
        return;
    }
    stopManualDrive();

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
    visualGeometries.reserve(result.visualBatches.size());
    for (StaticVisualBatch &batch : result.visualBatches) {
        auto geometry = std::make_unique<RaceGeometry>();
        const int indexCount =
                static_cast<int>(batch.indices.size() /
                                 static_cast<qsizetype>(sizeof(std::uint32_t)));
        geometry->setIndexedMesh(
                std::move(batch.vertices), std::move(batch.indices),
                StaticVisualVertexStride, true, true, true, true,
                batch.hasVertexColors, batch.boundsMin, batch.boundsMax,
                {{0, indexCount}});
        visualGeometries.push_back(std::move(geometry));
    }
    QVariantList visualBatches;
    visualBatches.reserve(result.visualBatchItems.size());
    for (QVariant &entry : result.visualBatchItems) {
        QVariantMap item = entry.toMap();
        const qint64 batchIndex =
                item.value(QStringLiteral("batchIndex")).toLongLong();
        if (batchIndex < 0 ||
            batchIndex >= static_cast<qint64>(visualGeometries.size())) {
            continue;
        }
        item.insert(
                QStringLiteral("geometry"),
                QVariant::fromValue(static_cast<QObject *>(
                        visualGeometries[static_cast<std::size_t>(batchIndex)]
                                .get())));
        visualBatches.push_back(std::move(item));
    }
    visualGeometries_ = std::move(visualGeometries);
    rayTracingScene_ = std::move(result.rayTracingScene);
    visualMaterials_ = std::move(result.visualMaterials);
    visualBatches_ = std::move(visualBatches);
    carEllipsoids_ = std::move(result.carEllipsoids);
    triangleCount_ = result.triangleCount;
    visualTriangleCount_ = result.visualTriangleCount;
    sourceVisualObjectCount_ = result.sourceVisualObjectCount;
    sourceVisualMeshCount_ = result.sourceVisualMeshCount;
    duplicateVisualObjectCount_ = result.duplicateVisualObjectCount;
    materialCount_ = result.materialCount;
    diagnosticCount_ = result.diagnosticCount;
    sceneBoundsMin_ = result.visualBoundsMin;
    sceneBoundsMax_ = result.visualBoundsMax;
    sceneRadius_ = std::max(
            1.0, 0.5 * static_cast<double>(
                    (result.visualBoundsMax -
                     result.visualBoundsMin).length()));
    timeMs_ = 0;
    loadedPacksDirectory_ = result.packsDirectory;
    loadedReplayPath_ = result.replayPath;
    manualRuntime_ = std::move(result.manualRuntime);
    loaded_ = true;
    runs_.clear();
    selectedRunId_.clear();
    durationMs_ = 0;
    updatePose();
    emit runsChanged();
    emit selectedRunChanged();
    emit timelineChanged();
    emit timeChanged();
    const bool addingPendingRun =
            pendingRun_ &&
            pendingRun_->packsDirectory == loadedPacksDirectory_ &&
            pendingRun_->replayPath == loadedReplayPath_;
    applyPendingRunIfReady();
    setStatusText(addingPendingRun
                          ? QStringLiteral("Best run added")
                          : QStringLiteral("Map loaded"));
    if (!queuedMapLoad_) setLoading(false);
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
    std::vector<SandboxInputEvent> inputs =
            std::move(pendingRun_->inputs);
    pendingRun_.reset();
    upsertRun(QStringLiteral("best"),
              QStringLiteral("Best"),
              std::move(frames),
              std::move(inputs),
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
                                     std::vector<SandboxInputEvent> inputs,
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
                         std::move(inputs),
                         {},
                         {}});
    } else {
        existing->name = std::move(name);
        existing->frames = std::move(frames);
        existing->inputs = std::move(inputs);
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

void RaceViewerController::advanceManualDrive() {
    if (!manualDriving_ || manualRuntime_ == nullptr) {
        return;
    }
    RaceViewerRun *const run = selectedRun();
    if (run == nullptr || run->id != QStringLiteral("manual") ||
        run->frames.empty()) {
        finishManualDrive(
                QStringLiteral("Manual drive stopped: run is unavailable."),
                false);
        return;
    }

    const qint64 targetTick = manualDriveClock_.elapsed() /
            static_cast<qint64>(kViewerTickDurationMs);
    const qint64 currentTick =
            static_cast<qint64>(run->frames.size()) - 1;
    const qint64 steps = std::min<qint64>(
            std::max<qint64>(0, targetTick - currentTick), 32);
    bool changed = false;
    for (qint64 step = 0; step < steps; ++step) {
        if (manualRuntime_->state.timeMs >=
            manualRuntime_->state.durationMs) {
            finishManualDrive(
                    QStringLiteral("Manual drive complete"), true);
            break;
        }
        auto advanced = manualRuntime_->sandbox.AdvanceTicks(1u);
        if (!advanced) {
            finishManualDrive(
                    QStringLiteral("Manual drive failed: %1")
                            .arg(SandboxErrorText(advanced.Error())),
                    false);
            break;
        }
        manualRuntime_->state = advanced.Value();
        run->frames.push_back(ToViewerFrame(manualRuntime_->state));
        durationMs_ = static_cast<qint64>(
                manualRuntime_->state.timeMs);
        timeMs_ = durationMs_;
        changed = true;
        if (manualRuntime_->state.raceCompleted) {
            finishManualDrive(
                    QStringLiteral("Manual drive complete"), true);
            break;
        }
    }
    if (changed) {
        updatePose();
        emit timelineChanged();
        emit timeChanged();
    }
}

bool RaceViewerController::replaceManualInputs() {
    if (manualRuntime_ == nullptr) {
        return false;
    }
    std::vector<PhysicsSandboxInputEvent> inputs =
            manualRuntime_->fixedInputs;
    inputs.insert(
            inputs.end(),
            manualRuntime_->driverInputs.begin(),
            manualRuntime_->driverInputs.end());
    std::stable_sort(
            inputs.begin(),
            inputs.end(),
            [](const PhysicsSandboxInputEvent &left,
               const PhysicsSandboxInputEvent &right) {
                return left.timeMs < right.timeMs;
            });
    auto replaced =
            manualRuntime_->sandbox.ReplaceInputs(std::move(inputs));
    if (!replaced) {
        setStatusText(
                QStringLiteral("Manual drive failed: %1")
                        .arg(SandboxErrorText(replaced.Error())));
        return false;
    }
    return true;
}

void RaceViewerController::finishManualDrive(
        const QString &status,
        bool releaseInputs) {
    if (!manualDriving_) {
        return;
    }
    if (releaseInputs) {
        releaseManualInputs();
        if (!manualDriving_) {
            return;
        }
    }
    manualDriveTimer_.stop();
    manualDriving_ = false;
    resetManualInputState();
    setStatusText(status);
    emit manualDrivingChanged();
}

void RaceViewerController::resetManualInputState() {
    const bool changed = manualLeft_ || manualRight_ ||
            manualAccelerate_ || manualBrake_;
    manualLeft_ = false;
    manualRight_ = false;
    manualAccelerate_ = false;
    manualBrake_ = false;
    if (changed) {
        emit manualInputChanged();
    }
}

void RaceViewerController::setPlaying(bool value) {
    if (playing_ == value) {
        return;
    }
    playing_ = value;
    emit playbackChanged();
}

}  // namespace forevertas::viewer
