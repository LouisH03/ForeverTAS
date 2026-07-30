#ifndef FOREVERTAS_VIEWER_RACE_VIEWER_CONTROLLER_H
#define FOREVERTAS_VIEWER_RACE_VIEWER_CONTROLLER_H

#include "physics_backend.h"
#include "searches/search_algorithm.h"
#include "viewer/race_geometry.h"
#include "viewer/ray_tracing_scene.h"
#include "viewer/visual_scene_pipeline.h"

#include <QElapsedTimer>
#include <QObject>
#include <QQuaternion>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector2D>
#include <QVector3D>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QThread;

namespace forevertas::viewer {

class ManualDriveRuntime;

struct RaceViewerFrame {
    std::int64_t timeMs = 0;
    QVector3D position{};
    QQuaternion rotation{};
    float accelerate = 0.0f;
    float brake = 0.0f;
    float steering = 0.0f;
};

struct RaceViewerInputSample {
    float accelerate = 0.0f;
    float brake = 0.0f;
    float steering = 0.0f;
};

struct RaceViewerRun {
    QString id;
    QString name;
    std::vector<RaceViewerFrame> frames;
    std::vector<SandboxInputEvent> inputs;
    QVector3D position{};
    QQuaternion rotation{};
};

struct RaceViewerMeshBuffers {
    QByteArray filled;
    QByteArray wire;
    QVector3D boundsMin{};
    QVector3D boundsMax{};
};

struct RaceViewerLoadResult {
    QString error;
    QString packsDirectory;
    QString replayPath;
    RaceViewerMeshBuffers track;
    std::vector<StaticVisualBatch> visualBatches;
    std::shared_ptr<const RayTracingSceneData> rayTracingScene;
    QVariantList visualMaterials;
    QVariantList visualBatchItems;
    QVector3D visualBoundsMin{};
    QVector3D visualBoundsMax{};
    QVariantList carEllipsoids;
    std::int64_t triangleCount = 0;
    std::int64_t visualTriangleCount = 0;
    std::int64_t sourceVisualObjectCount = 0;
    std::int64_t sourceVisualMeshCount = 0;
    std::int64_t duplicateVisualObjectCount = 0;
    std::int64_t materialCount = 0;
    std::int64_t diagnosticCount = 0;
    std::shared_ptr<ManualDriveRuntime> manualRuntime;
};

class RaceViewerController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QQuick3DGeometry *trackFilledGeometry READ
                       trackFilledGeometry CONSTANT)
    Q_PROPERTY(QQuick3DGeometry *trackWireGeometry READ
                       trackWireGeometry CONSTANT)
    Q_PROPERTY(QQuick3DGeometry *ellipsoidFilledGeometry READ
                       ellipsoidFilledGeometry CONSTANT)
    Q_PROPERTY(QQuick3DGeometry *ellipsoidWireGeometry READ
                       ellipsoidWireGeometry CONSTANT)
    Q_PROPERTY(QVariantList carEllipsoids READ carEllipsoids NOTIFY
                       sceneChanged)
    Q_PROPERTY(QVariantList visualInstances READ visualInstances NOTIFY
                       sceneChanged)
    Q_PROPERTY(
            QVariantList visualBatches READ visualBatches NOTIFY sceneChanged)
    Q_PROPERTY(QVariantList visualMaterials READ visualMaterials NOTIFY
                       sceneChanged)
    Q_PROPERTY(QVariantList runOptions READ runOptions NOTIFY runsChanged)
    Q_PROPERTY(QVariantList runPoses READ runPoses NOTIFY poseChanged)
    Q_PROPERTY(qint64 runCount READ runCount NOTIFY runsChanged)
    Q_PROPERTY(QString selectedRunId READ selectedRunId WRITE setSelectedRunId
                       NOTIFY selectedRunChanged)
    Q_PROPERTY(QVector3D carPosition READ carPosition NOTIFY poseChanged)
    Q_PROPERTY(QQuaternion carRotation READ carRotation NOTIFY poseChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY timelineChanged)
    Q_PROPERTY(qint64 timeMs READ timeMs WRITE setTimeMs NOTIFY timeChanged)
    Q_PROPERTY(qint64 currentTick READ currentTick WRITE setCurrentTick NOTIFY
                       timeChanged)
    Q_PROPERTY(qint64 tickCount READ tickCount NOTIFY timelineChanged)
    Q_PROPERTY(int tickDurationMs READ tickDurationMs CONSTANT)
    Q_PROPERTY(QString timeText READ timeText NOTIFY timeChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(bool manualDriving READ manualDriving NOTIFY
                       manualDrivingChanged)
    Q_PROPERTY(bool manualLeft READ manualLeft NOTIFY manualInputChanged)
    Q_PROPERTY(bool manualRight READ manualRight NOTIFY manualInputChanged)
    Q_PROPERTY(bool manualAccelerate READ manualAccelerate NOTIFY
                       manualInputChanged)
    Q_PROPERTY(bool manualBrake READ manualBrake NOTIFY manualInputChanged)
    Q_PROPERTY(bool canCopyCurrentInputs READ canCopyCurrentInputs NOTIFY
                       timelineChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(qint64 triangleCount READ triangleCount NOTIFY sceneChanged)
    Q_PROPERTY(qint64 visualTriangleCount READ visualTriangleCount NOTIFY
                       sceneChanged)
    Q_PROPERTY(qint64 visualMeshCount READ visualMeshCount NOTIFY
                       sceneChanged)
    Q_PROPERTY(
            qint64 visualBatchCount READ visualBatchCount NOTIFY sceneChanged)
    Q_PROPERTY(qint64 sourceVisualObjectCount READ sourceVisualObjectCount
                       NOTIFY sceneChanged)
    Q_PROPERTY(qint64 shadowCount READ shadowCount NOTIFY sceneChanged)
    Q_PROPERTY(qint64 materialCount READ materialCount NOTIFY sceneChanged)
    Q_PROPERTY(qint64 diagnosticCount READ diagnosticCount NOTIFY
                       sceneChanged)
    Q_PROPERTY(qint64 ellipsoidCount READ ellipsoidCount NOTIFY sceneChanged)
    Q_PROPERTY(double sceneRadius READ sceneRadius NOTIFY sceneChanged)
    Q_PROPERTY(QVector3D sceneBoundsMin READ sceneBoundsMin NOTIFY sceneChanged)
    Q_PROPERTY(QVector3D sceneBoundsMax READ sceneBoundsMax NOTIFY sceneChanged)

public:
    explicit RaceViewerController(QObject *parent = nullptr);
    ~RaceViewerController() override;

    QQuick3DGeometry *trackFilledGeometry();
    QQuick3DGeometry *trackWireGeometry();
    QQuick3DGeometry *ellipsoidFilledGeometry();
    QQuick3DGeometry *ellipsoidWireGeometry();
    QVariantList carEllipsoids() const;
    QVariantList visualInstances() const;
    QVariantList visualBatches() const;
    QVariantList visualMaterials() const;
    QVariantList runOptions() const;
    QVariantList runPoses() const;
    qint64 runCount() const;
    QString selectedRunId() const;
    QVector3D carPosition() const;
    QQuaternion carRotation() const;
    qint64 durationMs() const;
    qint64 timeMs() const;
    qint64 currentTick() const;
    qint64 tickCount() const;
    int tickDurationMs() const;
    QString timeText() const;
    bool playing() const;
    bool manualDriving() const;
    bool manualLeft() const;
    bool manualRight() const;
    bool manualAccelerate() const;
    bool manualBrake() const;
    bool canCopyCurrentInputs() const;
    bool loaded() const;
    bool loading() const;
    QString statusText() const;
    qint64 triangleCount() const;
    qint64 visualTriangleCount() const;
    qint64 visualMeshCount() const;
    qint64 visualBatchCount() const;
    qint64 sourceVisualObjectCount() const;
    qint64 shadowCount() const;
    qint64 materialCount() const;
    qint64 diagnosticCount() const;
    qint64 ellipsoidCount() const;
    double sceneRadius() const;
    QVector3D sceneBoundsMin() const;
    QVector3D sceneBoundsMax() const;
    std::shared_ptr<const RayTracingSceneData> rayTracingScene() const;
    RaceViewerInputSample inputSample(qint64 tick) const noexcept;
    void addSearchRun(
            const QString &packsDirectory,
            const QString &replayPath,
            const std::vector<SearchTimelineFrame> &frames);
    void addSearchRun(
            const QString &packsDirectory,
            const QString &replayPath,
            const std::vector<SearchTimelineFrame> &frames,
            const QString &backendId);
    void addSearchRun(
            const QString &packsDirectory,
            const QString &replayPath,
            const std::vector<SearchTimelineFrame> &frames,
            const std::vector<SandboxInputEvent> &inputs);
    void addSearchRun(
            const QString &packsDirectory,
            const QString &replayPath,
            const std::vector<SearchTimelineFrame> &frames,
            const std::vector<SandboxInputEvent> &inputs,
            const QString &backendId);

public slots:
    void setTimeMs(qint64 value);
    void setCurrentTick(qint64 tick);
    void setSelectedRunId(const QString &value);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void jumpToStart();
    Q_INVOKABLE void jumpToEnd();
    Q_INVOKABLE void startManualDrive();
    Q_INVOKABLE void stopManualDrive();
    Q_INVOKABLE void setManualInput(const QString &input, bool active);
    Q_INVOKABLE void releaseManualInputs();
    Q_INVOKABLE QString currentInputScript() const;
    Q_INVOKABLE void loadMap(const QString &packsDirectory,
                            const QString &replayPath);
    Q_INVOKABLE void loadMap(const QString &packsDirectory,
                            const QString &replayPath,
                            const QString &backendId);
    Q_INVOKABLE QVector2D cameraClipPlanes(const QVector3D &cameraPosition,
                                           double cameraDistance) const;

signals:
    void sceneChanged();
    void poseChanged();
    void timelineChanged();
    void timeChanged();
    void playbackChanged();
    void manualDrivingChanged();
    void manualInputChanged();
    void stateChanged();
    void runsChanged();
    void selectedRunChanged();

private:
    void applyLoadResult(std::uint64_t loadSerial,
                         RaceViewerLoadResult result);
    void beginMapLoad(const QString &packsDirectory,
                      const QString &replayPath,
                      PhysicsBackend backend);
    void applyPendingRunIfReady();
    void upsertRun(QString id,
                   QString name,
                   std::vector<RaceViewerFrame> frames,
                   std::vector<SandboxInputEvent> inputs,
                   bool select);
    const RaceViewerRun *selectedRun() const noexcept;
    RaceViewerRun *selectedRun() noexcept;
    void refreshSelectedRun();
    void setLoading(bool value);
    void setStatusText(const QString &value);
    void waitForWorker();
    void updatePose();
    void advancePlayback();
    void advanceManualDrive();
    void setPlaying(bool value);
    void finishManualDrive(const QString &status, bool releaseInputs);
    bool replaceManualInputs();
    void resetManualInputState();

    RaceGeometry trackFilledGeometry_;
    RaceGeometry trackWireGeometry_;
    std::vector<std::unique_ptr<RaceGeometry>> visualGeometries_;
    std::shared_ptr<const RayTracingSceneData> rayTracingScene_;
    std::vector<std::unique_ptr<RaceGeometry>>
            ellipsoidFilledGeometries_;
    RaceGeometry ellipsoidWireGeometry_;
    struct MapLoadRequest {
        QString packsDirectory;
        QString replayPath;
        PhysicsBackend backend = PhysicsBackend::OptimizedCpu;
    };
    struct PendingRun {
        QString packsDirectory;
        QString replayPath;
        PhysicsBackend backend = PhysicsBackend::OptimizedCpu;
        std::vector<RaceViewerFrame> frames;
        std::vector<SandboxInputEvent> inputs;
    };

    std::vector<RaceViewerRun> runs_;
    std::optional<MapLoadRequest> queuedMapLoad_;
    std::optional<PendingRun> pendingRun_;
    QVariantList carEllipsoids_;
    QVariantList visualBatches_;
    QVariantList visualMaterials_;
    QVector3D carPosition_{};
    QQuaternion carRotation_{};
    QString statusText_ = QStringLiteral("No map loaded");
    QString selectedRunId_;
    QString loadedPacksDirectory_;
    QString loadedReplayPath_;
    qint64 durationMs_ = 0;
    qint64 timeMs_ = 0;
    qint64 triangleCount_ = 0;
    qint64 visualTriangleCount_ = 0;
    qint64 sourceVisualObjectCount_ = 0;
    qint64 sourceVisualMeshCount_ = 0;
    qint64 duplicateVisualObjectCount_ = 0;
    qint64 materialCount_ = 0;
    qint64 diagnosticCount_ = 0;
    double sceneRadius_ = 1.0;
    QVector3D sceneBoundsMin_{};
    QVector3D sceneBoundsMax_{};
    qint64 playbackStartTick_ = 0;
    bool loaded_ = false;
    bool loading_ = false;
    bool playing_ = false;
    bool manualDriving_ = false;
    bool manualLeft_ = false;
    bool manualRight_ = false;
    bool manualAccelerate_ = false;
    bool manualBrake_ = false;
    QTimer playbackTimer_;
    QElapsedTimer playbackClock_;
    QTimer manualDriveTimer_;
    QElapsedTimer manualDriveClock_;
    std::shared_ptr<ManualDriveRuntime> manualRuntime_;
    QThread *workerThread_ = nullptr;
    std::uint64_t loadSerial_ = 0u;
};

}  // namespace forevertas::viewer

#endif
