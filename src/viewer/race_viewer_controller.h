#ifndef FOREVERTAS_VIEWER_RACE_VIEWER_CONTROLLER_H
#define FOREVERTAS_VIEWER_RACE_VIEWER_CONTROLLER_H

#include "viewer/race_geometry.h"

#include <QElapsedTimer>
#include <QObject>
#include <QQuaternion>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector3D>

#include <cstdint>
#include <vector>

class QThread;

namespace forevertas::viewer {

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

struct RaceViewerMeshBuffers {
    QByteArray filled;
    QByteArray wire;
    QVector3D boundsMin{};
    QVector3D boundsMax{};
};

struct RaceViewerLoadResult {
    QString error;
    RaceViewerMeshBuffers track;
    std::vector<RaceViewerFrame> frames;
    QVariantList carEllipsoids;
    std::int64_t durationMs = 0;
    std::int64_t triangleCount = 0;
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
    Q_PROPERTY(bool loaded READ loaded NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(qint64 triangleCount READ triangleCount NOTIFY sceneChanged)
    Q_PROPERTY(qint64 ellipsoidCount READ ellipsoidCount NOTIFY sceneChanged)
    Q_PROPERTY(double sceneRadius READ sceneRadius NOTIFY sceneChanged)

public:
    explicit RaceViewerController(QObject *parent = nullptr);
    ~RaceViewerController() override;

    QQuick3DGeometry *trackFilledGeometry();
    QQuick3DGeometry *trackWireGeometry();
    QQuick3DGeometry *ellipsoidFilledGeometry();
    QQuick3DGeometry *ellipsoidWireGeometry();
    QVariantList carEllipsoids() const;
    QVector3D carPosition() const;
    QQuaternion carRotation() const;
    qint64 durationMs() const;
    qint64 timeMs() const;
    qint64 currentTick() const;
    qint64 tickCount() const;
    int tickDurationMs() const;
    QString timeText() const;
    bool playing() const;
    bool loaded() const;
    bool loading() const;
    QString statusText() const;
    qint64 triangleCount() const;
    qint64 ellipsoidCount() const;
    double sceneRadius() const;
    RaceViewerInputSample inputSample(qint64 tick) const noexcept;

public slots:
    void setTimeMs(qint64 value);
    void setCurrentTick(qint64 tick);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void jumpToStart();
    Q_INVOKABLE void jumpToEnd();
    Q_INVOKABLE void loadReplay(const QString &packsDirectory,
                                const QString &replayPath);

signals:
    void sceneChanged();
    void poseChanged();
    void timelineChanged();
    void timeChanged();
    void playbackChanged();
    void stateChanged();

private:
    void applyLoadResult(RaceViewerLoadResult result);
    void setLoading(bool value);
    void setStatusText(const QString &value);
    void clearLoadedScene();
    void waitForWorker();
    void updatePose();
    void advancePlayback();
    void setPlaying(bool value);

    RaceGeometry trackFilledGeometry_;
    RaceGeometry trackWireGeometry_;
    RaceGeometry ellipsoidFilledGeometry_;
    RaceGeometry ellipsoidWireGeometry_;
    std::vector<RaceViewerFrame> frames_;
    QVariantList carEllipsoids_;
    QVector3D carPosition_{};
    QQuaternion carRotation_{};
    QString statusText_ = QStringLiteral("No replay loaded");
    qint64 durationMs_ = 0;
    qint64 timeMs_ = 0;
    qint64 triangleCount_ = 0;
    double sceneRadius_ = 1.0;
    qint64 playbackStartTick_ = 0;
    bool loaded_ = false;
    bool loading_ = false;
    bool playing_ = false;
    QTimer playbackTimer_;
    QElapsedTimer playbackClock_;
    QThread *workerThread_ = nullptr;
};

}  // namespace forevertas::viewer

#endif
