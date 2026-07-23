#ifndef FOREVERTAS_APP_SEARCH_CONTROLLER_H
#define FOREVERTAS_APP_SEARCH_CONTROLLER_H

#include "searches/search_runner.h"

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
    Q_PROPERTY(QVariantList searchAlgorithmOptions READ searchAlgorithmOptions
                       CONSTANT)
    Q_PROPERTY(QVariantList mutationAlgorithmOptions READ
                       mutationAlgorithmOptions CONSTANT)
    Q_PROPERTY(QVariantList evaluationTargetOptions READ evaluationTargetOptions
                       CONSTANT)
    Q_PROPERTY(QString searchAlgorithmId READ searchAlgorithmId WRITE
                       setSearchAlgorithmId NOTIFY searchAlgorithmIdChanged)
    Q_PROPERTY(QString mutationAlgorithmId READ mutationAlgorithmId WRITE
                       setMutationAlgorithmId NOTIFY mutationAlgorithmIdChanged)
    Q_PROPERTY(QString evaluationTargetId READ evaluationTargetId WRITE
                       setEvaluationTargetId NOTIFY evaluationTargetIdChanged)
    Q_PROPERTY(QVariantMap searchAlgorithmSettings READ searchAlgorithmSettings
                       NOTIFY searchAlgorithmSettingsChanged)
    Q_PROPERTY(QVariantMap mutationAlgorithmSettings READ
                       mutationAlgorithmSettings NOTIFY
                       mutationAlgorithmSettingsChanged)
    Q_PROPERTY(QVariantMap evaluationTargetSettings READ
                       evaluationTargetSettings NOTIFY
                       evaluationTargetSettingsChanged)

    Q_PROPERTY(bool canStart READ canStart NOTIFY canStartChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY cancellingChanged)
    Q_PROPERTY(bool progressIndeterminate READ progressIndeterminate NOTIFY
                       progressChanged)
    Q_PROPERTY(double progressValue READ progressValue NOTIFY progressChanged)
    Q_PROPERTY(QString validationMessage READ validationMessage NOTIFY
                       validationChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)

public:
    explicit SearchController(QObject *parent = nullptr);
    explicit SearchController(const QStringList &packsSearchPatterns,
                              QObject *parent = nullptr);
    ~SearchController() override;

    QString packsDirectory() const;
    QString autoDetectedPacksDirectory() const;
    QString replayPath() const;
    QVariantList searchAlgorithmOptions() const;
    QVariantList mutationAlgorithmOptions() const;
    QVariantList evaluationTargetOptions() const;
    QString searchAlgorithmId() const;
    QString mutationAlgorithmId() const;
    QString evaluationTargetId() const;
    QVariantMap searchAlgorithmSettings() const;
    QVariantMap mutationAlgorithmSettings() const;
    QVariantMap evaluationTargetSettings() const;

    bool canStart() const;
    bool running() const;
    bool cancelling() const;
    bool progressIndeterminate() const;
    double progressValue() const;
    QString validationMessage() const;
    QString statusText() const;
    QString resultText() const;

public slots:
    void setPacksDirectory(const QString &value);
    void setReplayPath(const QString &value);
    void setSearchAlgorithmId(const QString &value);
    void setMutationAlgorithmId(const QString &value);
    void setEvaluationTargetId(const QString &value);

    Q_INVOKABLE void browseForPacksDirectory();
    Q_INVOKABLE void applyAutoDetectedPacksDirectory();
    Q_INVOKABLE void browseForReplay();
    Q_INVOKABLE void setSearchAlgorithmSetting(const QString &key,
                                               const QString &value);
    Q_INVOKABLE void setMutationAlgorithmSetting(const QString &key,
                                                 const QString &value);
    Q_INVOKABLE void setEvaluationTargetSetting(const QString &key,
                                                const QString &value);
    Q_INVOKABLE void startSearch();
    Q_INVOKABLE void cancelSearch();

signals:
    void packsDirectoryChanged();
    void autoDetectedPacksDirectoryChanged();
    void replayPathChanged();
    void searchAlgorithmIdChanged();
    void mutationAlgorithmIdChanged();
    void evaluationTargetIdChanged();
    void searchAlgorithmSettingsChanged();
    void mutationAlgorithmSettingsChanged();
    void evaluationTargetSettingsChanged();
    void canStartChanged();
    void runningChanged();
    void cancellingChanged();
    void progressChanged();
    void validationChanged();
    void statusChanged();
    void resultChanged();

private:
    struct ValidationResult {
        std::optional<SearchRequest> request;
        QString error;
    };

    ValidationResult validate() const;
    void refreshValidation();
    void setRunning(bool value);
    void setCancelling(bool value);
    void setStatusText(const QString &value);
    void setResultText(const QString &value);
    void setProgress(bool indeterminate, double value);
    void initialize(const QStringList *packsSearchPatterns);
    void scheduleAutoDetectPacksDirectory(
            const QStringList *packsSearchPatterns);
    void publishAutoDetectedPacksDirectory(const QString &detected);
    void clearAutoDetectedPacksDirectory();
    void loadSearchAlgorithmSettings();
    void loadMutationAlgorithmSettings();
    void loadEvaluationTargetSettings();
    void persistOptionSetting(const QString &category,
                              const QString &optionId,
                              const QString &key,
                              const QString &value);
    void persist(const char *key, const QString &value);
    void waitForWorker();

    QString packsDirectory_;
    QString autoDetectedPacksDirectory_;
    QString replayPath_;
    QString searchAlgorithmId_;
    QString mutationAlgorithmId_;
    QString evaluationTargetId_;
    QVariantMap searchAlgorithmSettings_;
    QVariantMap mutationAlgorithmSettings_;
    QVariantMap evaluationTargetSettings_;
    QString validationMessage_;
    QString statusText_ = QStringLiteral("Ready");
    QString resultText_;
    bool valid_ = false;
    bool running_ = false;
    bool cancelling_ = false;
    bool progressIndeterminate_ = false;
    bool autoDetectionAttempted_ = false;
    double progressValue_ = 0.0;
    QThread *autoDetectionThread_ = nullptr;
    QThread *workerThread_ = nullptr;
    std::shared_ptr<std::atomic_bool> cancellationRequested_;
};

}  // namespace forevertas::app

#endif
