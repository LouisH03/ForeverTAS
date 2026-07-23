#include "viewer/race_viewer_controller.h"

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

std::array<float, 4u> FaceColor(const ViewerTriangle &triangle,
                                bool car) {
    QVector3D normal = QVector3D::crossProduct(
            triangle.b - triangle.a, triangle.c - triangle.a);
    if (normal.lengthSquared() > 0.0f) {
        normal.normalize();
    }
    const float x = std::fabs(normal.x());
    const float y = std::fabs(normal.y());
    const float z = std::fabs(normal.z());
    if (car) {
        return {0.78f + 0.16f * y,
                0.26f + 0.16f * z,
                0.08f + 0.10f * x,
                1.0f};
    }
    return {0.26f + 0.20f * x,
            0.36f + 0.26f * y,
            0.43f + 0.20f * z,
            1.0f};
}

RaceViewerMeshBuffers BuildMeshBuffers(
        const std::vector<ViewerTriangle> &triangles,
        bool car) {
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
        const std::array<float, 4u> color = FaceColor(triangle, car);
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

        std::vector<ViewerTriangle> triangles;
        triangles.reserve(scene.collisionTriangles.size());
        for (const PhysicsSandboxCollisionTriangle &triangle :
             scene.collisionTriangles) {
            triangles.push_back({
                    ToQt(triangle.a), ToQt(triangle.b), ToQt(triangle.c)});
        }
        result.track = BuildMeshBuffers(triangles, false);
        result.triangleCount = static_cast<qint64>(triangles.size());

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

        result.durationMs = static_cast<qint64>(state.durationMs);
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
    milliseconds = std::max<qint64>(0, milliseconds);
    const qint64 minutes = milliseconds / 60000;
    const qint64 seconds = (milliseconds / 1000) % 60;
    const qint64 millis = milliseconds % 1000;
    return QStringLiteral("%1:%2.%3")
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'))
            .arg(millis, 3, 10, QLatin1Char('0'));
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

    const RaceViewerMeshBuffers ellipsoid =
            BuildMeshBuffers(UnitEllipsoidTriangles(), true);
    ellipsoidFilledGeometry_.setMesh(
            ellipsoid.filled,
            static_cast<int>(sizeof(FilledVertex)),
            QQuick3DGeometry::PrimitiveType::Triangles,
            true,
            ellipsoid.boundsMin,
            ellipsoid.boundsMax);
    ellipsoidWireGeometry_.setMesh(
            ellipsoid.wire,
            static_cast<int>(sizeof(WireVertex)),
            QQuick3DGeometry::PrimitiveType::Lines,
            false,
            ellipsoid.boundsMin,
            ellipsoid.boundsMax);
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
    return &ellipsoidFilledGeometry_;
}

QQuick3DGeometry *RaceViewerController::ellipsoidWireGeometry() {
    return &ellipsoidWireGeometry_;
}

QVariantList RaceViewerController::carEllipsoids() const {
    return carEllipsoids_;
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
    if (frames_.empty()) {
        return 0;
    }
    return std::clamp<qint64>(
            timeMs_ / static_cast<qint64>(kViewerTickDurationMs),
            0,
            tickCount() - 1);
}

qint64 RaceViewerController::tickCount() const {
    return static_cast<qint64>(frames_.size());
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

qint64 RaceViewerController::ellipsoidCount() const {
    return carEllipsoids_.size();
}

double RaceViewerController::sceneRadius() const {
    return sceneRadius_;
}

RaceViewerInputSample RaceViewerController::inputSample(qint64 tick) const
        noexcept {
    if (tick < 0 || tick >= tickCount()) {
        return {};
    }
    const RaceViewerFrame &frame = frames_[static_cast<std::size_t>(tick)];
    return {frame.accelerate, frame.brake, frame.steering};
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
    if (frames_.empty()) {
        setTimeMs(0);
        return;
    }
    const qint64 clamped = std::clamp<qint64>(tick, 0, tickCount() - 1);
    setTimeMs(std::min<qint64>(
            durationMs_,
            clamped * static_cast<qint64>(kViewerTickDurationMs)));
}

void RaceViewerController::play() {
    if (!loaded_ || frames_.empty() || playing_) {
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
    if (workerThread_ != nullptr) {
        return;
    }
    const QFileInfo packsInfo(packsDirectory);
    const QFileInfo replayInfo(replayPath);
    if (!packsInfo.isDir() || !packsInfo.isReadable()) {
        setStatusText(QStringLiteral(
                "Select a readable installed Packs directory."));
        return;
    }
    if (!replayInfo.isFile() || !replayInfo.isReadable()) {
        setStatusText(QStringLiteral("Select a readable replay file."));
        return;
    }

    clearLoadedScene();
    setLoading(true);
    setStatusText(QStringLiteral(
            "Loading collision geometry and sampling replay..."));

    QThread *const thread = QThread::create(
            [this, packsDirectory, replayPath]() {
                RaceViewerLoadResult result =
                        LoadReplayData(packsDirectory, replayPath);
                QMetaObject::invokeMethod(
                        this,
                        [this, result = std::move(result)]() mutable {
                            applyLoadResult(std::move(result));
                        },
                        Qt::QueuedConnection);
            });
    workerThread_ = thread;
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (workerThread_ == thread) {
            workerThread_ = nullptr;
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void RaceViewerController::applyLoadResult(RaceViewerLoadResult result) {
    pause();
    if (!result.error.isEmpty()) {
        setStatusText(result.error);
        setLoading(false);
        return;
    }
    if (result.frames.empty()) {
        setStatusText(QStringLiteral("Replay produced no viewable frames."));
        setLoading(false);
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
    frames_ = std::move(result.frames);
    carEllipsoids_ = std::move(result.carEllipsoids);
    durationMs_ = result.durationMs;
    triangleCount_ = result.triangleCount;
    sceneRadius_ = std::max(
            1.0, 0.5 * static_cast<double>(
                    (result.track.boundsMax - result.track.boundsMin).length()));
    timeMs_ = 0;
    loaded_ = true;
    updatePose();
    setStatusText(QStringLiteral("Replay loaded"));
    setLoading(false);
    emit sceneChanged();
    emit timelineChanged();
    emit timeChanged();
    emit stateChanged();
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

void RaceViewerController::clearLoadedScene() {
    pause();
    loaded_ = false;
    frames_.clear();
    carEllipsoids_.clear();
    trackFilledGeometry_.clearMesh();
    trackWireGeometry_.clearMesh();
    carPosition_ = {};
    carRotation_ = {};
    durationMs_ = 0;
    timeMs_ = 0;
    triangleCount_ = 0;
    sceneRadius_ = 1.0;
    emit sceneChanged();
    emit poseChanged();
    emit timelineChanged();
    emit timeChanged();
    emit stateChanged();
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
    if (frames_.empty()) {
        return;
    }
    const auto upper = std::lower_bound(
            frames_.begin(),
            frames_.end(),
            timeMs_,
            [](const RaceViewerFrame &frame, qint64 time) {
                return frame.timeMs < time;
            });
    if (upper == frames_.begin()) {
        carPosition_ = upper->position;
        carRotation_ = upper->rotation;
    } else if (upper == frames_.end()) {
        carPosition_ = frames_.back().position;
        carRotation_ = frames_.back().rotation;
    } else {
        const RaceViewerFrame &after = *upper;
        const RaceViewerFrame &before = *(upper - 1);
        const qint64 interval = after.timeMs - before.timeMs;
        const float blend = interval > 0
                ? static_cast<float>(timeMs_ - before.timeMs) /
                        static_cast<float>(interval)
                : 0.0f;
        carPosition_ = before.position * (1.0f - blend) +
                after.position * blend;
        carRotation_ = QQuaternion::slerp(
                before.rotation, after.rotation, blend);
    }
    emit poseChanged();
}

void RaceViewerController::advancePlayback() {
    if (!playing_ || frames_.empty()) {
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
