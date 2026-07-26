#ifndef FOREVERTAS_APP_SEARCH_CONTROLLER_H
#define FOREVERTAS_APP_SEARCH_CONTROLLER_H

#include "app/search_configuration_model.h"
#include "app/search_completion.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <optional>

class QThread;

namespace forevertas::app {

class SearchController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString packsDirectory READ packsDirectory WRITE
                       setPacksDirectory NOTIFY packsDirectoryChanged)
    Q_PROPERTY(QString autoDetectedPacksDirectory READ
                       autoDetectedPacksDirectory NOTIFY
                       autoDetectedPacksDirectoryChanged)
    Q_PROPERTY(QString replayPath READ replayPath WRITE setReplayPath NOTIFY
                       replayPathChanged)
    Q_PROPERTY(QVariantList simulationBackendOptions READ
                       simulationBackendOptions CONSTANT)
    Q_PROPERTY(QString simulationBackendId READ simulationBackendId WRITE
                       setSimulationBackendId NOTIFY simulationBackendIdChanged)
    Q_PROPERTY(QString cudaParallelSampleCount READ cudaParallelSampleCount WRITE
                       setCudaParallelSampleCount NOTIFY
                       cudaParallelSampleCountChanged)
    Q_PROPERTY(bool cudaCalibrationEnabled READ cudaCalibrationEnabled WRITE
                       setCudaCalibrationEnabled NOTIFY
                       cudaCalibrationEnabledChanged)
    Q_PROPERTY(QVariantList searchAlgorithmOptions READ searchAlgorithmOptions
                       CONSTANT)
    Q_PROPERTY(QVariantList modifierOptions READ modifierOptions CONSTANT)
    Q_PROPERTY(QVariantList evaluationTargetOptions READ evaluationTargetOptions
                       CONSTANT)
    Q_PROPERTY(QString searchAlgorithmId READ searchAlgorithmId WRITE
                       setSearchAlgorithmId NOTIFY searchAlgorithmIdChanged)
    Q_PROPERTY(QString evaluationTargetId READ evaluationTargetId WRITE
                       setEvaluationTargetId NOTIFY evaluationTargetIdChanged)
    Q_PROPERTY(QVariantMap searchAlgorithmSettings READ searchAlgorithmSettings
                       NOTIFY searchAlgorithmSettingsChanged)
    Q_PROPERTY(QVariantList modifierPasses READ modifierPasses NOTIFY
                       modifierPassesChanged)
    Q_PROPERTY(QVariantMap evaluationTargetSettings READ
                       evaluationTargetSettings NOTIFY
                       evaluationTargetSettingsChanged)

    Q_PROPERTY(bool canStart READ canStart NOTIFY canStartChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool stopping READ stopping NOTIFY stoppingChanged)
    Q_PROPERTY(bool progressIndeterminate READ progressIndeterminate NOTIFY
                       progressChanged)
    Q_PROPERTY(double progressValue READ progressValue NOTIFY progressChanged)
    Q_PROPERTY(QString validationMessage READ validationMessage NOTIFY
                       validationChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool liveMetricsVisible READ liveMetricsVisible NOTIFY
                       metricsChanged)
    Q_PROPERTY(QString iterationCountText READ iterationCountText NOTIFY
                       metricsChanged)
    Q_PROPERTY(QString throughputText READ throughputText NOTIFY metricsChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY metricsChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)
    Q_PROPERTY(QString bestInputsText READ bestInputsText NOTIFY resultChanged)

public:
    explicit SearchController(QObject *parent = nullptr);
    explicit SearchController(const QStringList &packsSearchPatterns,
                              QObject *parent = nullptr);
    ~SearchController() override;

    QString packsDirectory() const;
    QString autoDetectedPacksDirectory() const;
    QString replayPath() const;
    QVariantList simulationBackendOptions() const;
    QString simulationBackendId() const;
    QString cudaParallelSampleCount() const;
    bool cudaCalibrationEnabled() const;
    QVariantList searchAlgorithmOptions() const;
    QVariantList modifierOptions() const;
    QVariantList evaluationTargetOptions() const;
    QString searchAlgorithmId() const;
    QString evaluationTargetId() const;
    QVariantMap searchAlgorithmSettings() const;
    QVariantList modifierPasses() const;
    QVariantMap evaluationTargetSettings() const;

    bool canStart() const;
    bool running() const;
    bool stopping() const;
    bool progressIndeterminate() const;
    double progressValue() const;
    QString validationMessage() const;
    QString statusText() const;
    bool liveMetricsVisible() const;
    QString iterationCountText() const;
    QString throughputText() const;
    QString elapsedText() const;
    QString resultText() const;
    QString bestInputsText() const;

public slots:
    void setPacksDirectory(const QString &value);
    void setReplayPath(const QString &value);
    void setSimulationBackendId(const QString &value);
    void setCudaParallelSampleCount(const QString &value);
    void setCudaCalibrationEnabled(bool value);
    void setSearchAlgorithmId(const QString &value);
    void setEvaluationTargetId(const QString &value);

    Q_INVOKABLE void browseForPacksDirectory();
    Q_INVOKABLE void applyAutoDetectedPacksDirectory();
    Q_INVOKABLE void browseForReplay();
    Q_INVOKABLE void setSearchAlgorithmSetting(const QString &key,
                                               const QString &value);
    Q_INVOKABLE void addModifierPass(const QString &id);
    Q_INVOKABLE void removeModifierPass(int index);
    Q_INVOKABLE void moveModifierPass(int fromIndex, int toIndex);
    Q_INVOKABLE void setModifierPassId(int index, const QString &id);
    Q_INVOKABLE void setModifierPassSetting(int index,
                                            const QString &key,
                                            const QString &value);
    Q_INVOKABLE void setEvaluationTargetSetting(const QString &key,
                                                const QString &value);
    Q_INVOKABLE void startSearch();
    Q_INVOKABLE void stopSearch();

signals:
    void packsDirectoryChanged();
    void autoDetectedPacksDirectoryChanged();
    void replayPathChanged();
    void simulationBackendIdChanged();
    void cudaParallelSampleCountChanged();
    void cudaCalibrationEnabledChanged();
    void searchAlgorithmIdChanged();
    void evaluationTargetIdChanged();
    void searchAlgorithmSettingsChanged();
    void modifierPassesChanged();
    void evaluationTargetSettingsChanged();
    void canStartChanged();
    void runningChanged();
    void stoppingChanged();
    void progressChanged();
    void validationChanged();
    void statusChanged();
    void metricsChanged();
    void resultChanged();
    void searchCompleted(forevertas::app::SearchCompletionPtr completion);

private:
    struct ValidationResult {
        std::optional<SearchRequest> request;
        QString error;
    };

    ValidationResult validate() const;
    void refreshValidation();
    void setRunning(bool value);
    void setStopping(bool value);
    void setStatusText(const QString &value);
    void setLiveMetrics(const QString &iterationCountText,
                        const QString &throughputText,
                        const QString &elapsedText,
                        bool visible);
    void setResultText(const QString &value);
    void setBestInputsText(const QString &value);
    void setProgress(bool indeterminate, double value);
    void initialize(const QStringList *packsSearchPatterns);
    void scheduleAutoDetectPacksDirectory(
            const QStringList *packsSearchPatterns);
    void publishAutoDetectedPacksDirectory(const QString &detected);
    void clearAutoDetectedPacksDirectory();
    void persist(const char *key, const QString &value);
    void waitForWorker();

    QString packsDirectory_;
    QString autoDetectedPacksDirectory_;
    QString replayPath_;
    PhysicsBackend simulationBackend_ = PhysicsBackend::Reference;
    QString cudaParallelSampleCount_ = QString::number(
            kDefaultCudaParallelSampleCount);
    bool cudaCalibrationEnabled_ = false;
    SearchConfigurationModel configuration_;
    QString validationMessage_;
    QString statusText_ = QStringLiteral("Ready");
    QString iterationCountText_;
    QString throughputText_;
    QString elapsedText_;
    QString resultText_;
    QString bestInputsText_;
    bool valid_ = false;
    bool liveMetricsVisible_ = false;
    bool running_ = false;
    bool stopping_ = false;
    bool progressIndeterminate_ = false;
    bool autoDetectionScheduled_ = false;
    double progressValue_ = 0.0;
    QThread *autoDetectionThread_ = nullptr;
    QThread *workerThread_ = nullptr;
    std::shared_ptr<std::atomic_bool> stopRequested_;
    std::shared_ptr<std::atomic_bool> cancellationRequested_;
    SearchCompletionPtr lastCompletion_;
};

}  // namespace forevertas::app

#endif
