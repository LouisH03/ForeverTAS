#ifndef FOREVERTAS_APP_SEARCH_CONTROLLER_H
#define FOREVERTAS_APP_SEARCH_CONTROLLER_H

#include "searches/serial_search_runner.h"

#include <QObject>
#include <QString>
#include <QStringList>

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
    Q_PROPERTY(QString minMutateMs READ minMutateMs WRITE setMinMutateMs NOTIFY
                       minMutateMsChanged)
    Q_PROPERTY(QString maxMutateMs READ maxMutateMs WRITE setMaxMutateMs NOTIFY
                       maxMutateMsChanged)
    Q_PROPERTY(QString minEvalTimeMs READ minEvalTimeMs WRITE setMinEvalTimeMs
                       NOTIFY minEvalTimeMsChanged)
    Q_PROPERTY(QString maxEvalTimeMs READ maxEvalTimeMs WRITE setMaxEvalTimeMs
                       NOTIFY maxEvalTimeMsChanged)
    Q_PROPERTY(QString attemptCount READ attemptCount WRITE setAttemptCount
                       NOTIFY attemptCountChanged)
    Q_PROPERTY(QString mutationSeed READ mutationSeed WRITE setMutationSeed
                       NOTIFY mutationSeedChanged)

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
    QString minMutateMs() const;
    QString maxMutateMs() const;
    QString minEvalTimeMs() const;
    QString maxEvalTimeMs() const;
    QString attemptCount() const;
    QString mutationSeed() const;

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
    void setMinMutateMs(const QString &value);
    void setMaxMutateMs(const QString &value);
    void setMinEvalTimeMs(const QString &value);
    void setMaxEvalTimeMs(const QString &value);
    void setAttemptCount(const QString &value);
    void setMutationSeed(const QString &value);

    Q_INVOKABLE void browseForPacksDirectory();
    Q_INVOKABLE void applyAutoDetectedPacksDirectory();
    Q_INVOKABLE void browseForReplay();
    Q_INVOKABLE void startSearch();
    Q_INVOKABLE void cancelSearch();

signals:
    void packsDirectoryChanged();
    void autoDetectedPacksDirectoryChanged();
    void replayPathChanged();
    void minMutateMsChanged();
    void maxMutateMsChanged();
    void minEvalTimeMsChanged();
    void maxEvalTimeMsChanged();
    void attemptCountChanged();
    void mutationSeedChanged();
    void canStartChanged();
    void runningChanged();
    void cancellingChanged();
    void progressChanged();
    void validationChanged();
    void statusChanged();
    void resultChanged();

private:
    struct ValidationResult {
        std::optional<SerialSearchRequest> request;
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
    void persist(const char *key, const QString &value);
    void waitForWorker();

    QString packsDirectory_;
    QString autoDetectedPacksDirectory_;
    QString replayPath_;
    QString minMutateMs_;
    QString maxMutateMs_;
    QString minEvalTimeMs_;
    QString maxEvalTimeMs_;
    QString attemptCount_;
    QString mutationSeed_;
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
