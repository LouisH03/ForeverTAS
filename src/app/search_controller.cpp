#include "app/search_controller.h"

#include "app/packs_directory_finder.h"
#include "app/search_worker.h"
#include "mutations/input_event_formatter.h"
#include "mutations/replay_input_script.h"

#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace forevertas::app {
namespace {

constexpr char kPacksDirectoryKey[] = "paths/packsDirectory";
constexpr char kReplayPathKey[] = "paths/replayPath";
constexpr char kBaseInputScriptKey[] = "inputs/baseScript";
constexpr char kSimulationBackendKey[] = "selection/simulationBackend";
constexpr char kCudaParallelSampleCountKey[] =
        "backends/cuda/parallelSampleCount";
constexpr char kCudaCalibrationEnabledKey[] =
        "backends/cuda/calibrationEnabled";
std::atomic_bool gAutomaticPacksSearchScheduled{false};

struct ReplayInputExtractionResult {
    QString script;
    QString error;
};

ReplayInputExtractionResult ExtractReplayInputScript(
        const QString &packsDirectory,
        const QString &replayPath) {
    ReplayInputExtractionResult result;
    try {
        result.script = QString::fromStdString(
                forevertas::ExtractReplayInputScript(
                        packsDirectory.toUtf8().toStdString(),
                        replayPath.toUtf8().toStdString()));
    } catch (const std::exception &exception) {
        result.error = QString::fromUtf8(exception.what());
    } catch (...) {
        result.error =
                QStringLiteral("Unexpected replay input extraction failure");
    }
    return result;
}

QString StoredValue(const char *key, const QString &fallback) {
    return QSettings().value(QLatin1String(key), fallback).toString();
}

QString BackendId(PhysicsBackend backend) {
    const std::string_view id = PhysicsBackendId(backend);
    return QString::fromLatin1(id.data(), static_cast<qsizetype>(id.size()));
}

}  // namespace

SearchController::SearchController(QObject *parent)
    : QObject(parent) {
    initialize(nullptr);
}

SearchController::SearchController(const QStringList &packsSearchPatterns,
                                   QObject *parent)
    : QObject(parent) {
    initialize(&packsSearchPatterns);
}

void SearchController::initialize(const QStringList *packsSearchPatterns) {
    qRegisterMetaType<SearchCompletionPtr>();
    packsDirectory_ = StoredValue(kPacksDirectoryKey, {});
    replayPath_ = StoredValue(kReplayPathKey, {});
    baseInputScript_ = StoredValue(kBaseInputScriptKey, {});
    InputScriptParseResult parsed =
            ParseInputScript(baseInputScript_.toStdString());
    parsedBaseInputCommands_ = std::move(parsed.commands);
    if (parsed.error) {
        baseInputScriptError_ = QString::fromStdString(*parsed.error);
    }
    inputScriptPersistTimer_ = new QTimer(this);
    inputScriptPersistTimer_->setSingleShot(true);
    inputScriptPersistTimer_->setInterval(350);
    connect(inputScriptPersistTimer_, &QTimer::timeout, this, [this]() {
        persist(kBaseInputScriptKey, baseInputScript_);
    });
    cudaParallelSampleCount_ = StoredValue(
            kCudaParallelSampleCountKey,
            QString::number(kDefaultCudaParallelSampleCount));
    cudaCalibrationEnabled_ = QSettings()
            .value(QLatin1String(kCudaCalibrationEnabledKey), false)
            .toBool();
    const QString storedBackend = StoredValue(
            kSimulationBackendKey,
            BackendId(PhysicsBackend::Reference));
    const std::optional<PhysicsBackend> parsedBackend =
            ParsePhysicsBackend(storedBackend.toStdString());
    simulationBackend_ = parsedBackend.value_or(PhysicsBackend::Reference);
    if (!parsedBackend) {
        QSettings().setValue(
                QLatin1String(kSimulationBackendKey),
                BackendId(simulationBackend_));
    }
    scheduleAutoDetectPacksDirectory(packsSearchPatterns);
    refreshValidation();
}

SearchController::~SearchController() {
    if (inputScriptPersistTimer_ != nullptr) {
        inputScriptPersistTimer_->stop();
    }
    persist(kBaseInputScriptKey, baseInputScript_);
    QSettings().sync();
    waitForWorker();
}

QString SearchController::packsDirectory() const {
    return packsDirectory_;
}

QString SearchController::autoDetectedPacksDirectory() const {
    return autoDetectedPacksDirectory_;
}

QString SearchController::replayPath() const {
    return replayPath_;
}

QString SearchController::baseInputScript() const {
    return baseInputScript_;
}

QString SearchController::baseInputScriptError() const {
    return baseInputScriptError_;
}

bool SearchController::extractingReplayInputs() const {
    return extractingReplayInputs_;
}

bool SearchController::canExtractReplayInputs() const {
    const QFileInfo packsInfo(packsDirectory_);
    const QFileInfo replayInfo(replayPath_);
    return !running_ && !extractingReplayInputs_ &&
            packsInfo.isDir() && packsInfo.isReadable() &&
            replayInfo.isFile() && replayInfo.isReadable();
}

QString SearchController::replayInputStatusText() const {
    return replayInputStatusText_;
}

QVariantList SearchController::simulationBackendOptions() const {
    QVariantList options{
            QVariantMap{
                    {QStringLiteral("id"),
                     BackendId(PhysicsBackend::Reference)},
                    {QStringLiteral("label"), QStringLiteral("Reference")},
                    {QStringLiteral("description"),
                     QStringLiteral("Broadest compatibility")}},
            QVariantMap{
                    {QStringLiteral("id"),
                     BackendId(PhysicsBackend::OptimizedCpu)},
                    {QStringLiteral("label"),
                     QStringLiteral("CPU Optimized")},
                    {QStringLiteral("description"),
                     QStringLiteral(
                             "Faster runtime optimized for Stadium, may "
                             "break compatibility in other environments")}},
    };
#if FOREVERVALIDATOR_HAS_CUDA
    options.push_back(QVariantMap{
            {QStringLiteral("id"),
             BackendId(PhysicsBackend::Cuda)},
            {QStringLiteral("label"), QStringLiteral("CUDA")},
            {QStringLiteral("description"),
             QStringLiteral(
                     "Fastest runtime optimized for Stadium, needs a modern "
                     "NVIDIA GPU and may break compatibility in other "
                     "environments")}});
#endif
    return options;
}

QString SearchController::simulationBackendId() const {
    return BackendId(simulationBackend_);
}

QString SearchController::cudaParallelSampleCount() const {
    return cudaParallelSampleCount_;
}

bool SearchController::cudaCalibrationEnabled() const {
    return cudaCalibrationEnabled_;
}

QVariantList SearchController::searchAlgorithmOptions() const {
    return configuration_.searchAlgorithmOptions();
}

QVariantList SearchController::modifierOptions() const {
    return configuration_.modifierOptions();
}

QVariantList SearchController::evaluationTargetOptions() const {
    return configuration_.evaluationTargetOptions();
}

QString SearchController::searchAlgorithmId() const {
    return configuration_.searchAlgorithmId();
}

QString SearchController::evaluationTargetId() const {
    return configuration_.evaluationTargetId();
}

QVariantMap SearchController::searchAlgorithmSettings() const {
    return configuration_.searchAlgorithmSettings();
}

QVariantList SearchController::modifierPasses() const {
    return configuration_.modifierPasses();
}

QVariantMap SearchController::evaluationTargetSettings() const {
    return configuration_.evaluationTargetSettings();
}

bool SearchController::canStart() const {
    return valid_ && !running_ && !extractingReplayInputs_;
}

bool SearchController::running() const {
    return running_;
}

bool SearchController::stopping() const {
    return stopping_;
}

bool SearchController::progressIndeterminate() const {
    return progressIndeterminate_;
}

double SearchController::progressValue() const {
    return progressValue_;
}

QString SearchController::validationMessage() const {
    return validationMessage_;
}

QString SearchController::statusText() const {
    return statusText_;
}

bool SearchController::liveMetricsVisible() const {
    return liveMetricsVisible_;
}

QString SearchController::iterationCountText() const {
    return iterationCountText_;
}

QString SearchController::throughputText() const {
    return throughputText_;
}

QString SearchController::elapsedText() const {
    return elapsedText_;
}

QString SearchController::resultText() const {
    return resultText_;
}

QString SearchController::bestInputsText() const {
    return bestInputsText_;
}

void SearchController::setReplayPath(const QString &value) {
    if (replayPath_ == value) {
        return;
    }
    replayPath_ = value;
    persist(kReplayPathKey, value);
    emit replayPathChanged();
    emit replayInputStateChanged();
    refreshValidation();
}

void SearchController::setBaseInputScript(const QString &value) {
    if (baseInputScript_ == value) {
        return;
    }
    baseInputScript_ = value;
    InputScriptParseResult parsed = ParseInputScript(value.toStdString());
    parsedBaseInputCommands_ = std::move(parsed.commands);
    baseInputScriptError_ = parsed.error
            ? QString::fromStdString(*parsed.error)
            : QString{};
    if (inputScriptPersistTimer_ != nullptr) {
        inputScriptPersistTimer_->start();
    }
    emit baseInputScriptChanged();
    refreshValidation();
}

void SearchController::setSearchAlgorithmId(const QString &value) {
    if (!configuration_.setSearchAlgorithmId(value)) return;
    emit searchAlgorithmIdChanged();
    emit searchAlgorithmSettingsChanged();
    refreshValidation();
}

void SearchController::setSimulationBackendId(const QString &value) {
    const std::optional<PhysicsBackend> parsed =
            ParsePhysicsBackend(value.toStdString());
    if (!parsed || simulationBackend_ == *parsed) {
        return;
    }
    simulationBackend_ = *parsed;
    persist(kSimulationBackendKey, BackendId(simulationBackend_));
    emit simulationBackendIdChanged();
    refreshValidation();
}

void SearchController::setCudaParallelSampleCount(const QString &value) {
    if (cudaParallelSampleCount_ == value) {
        return;
    }
    cudaParallelSampleCount_ = value;
    persist(kCudaParallelSampleCountKey, value);
    emit cudaParallelSampleCountChanged();
    refreshValidation();
}

void SearchController::setCudaCalibrationEnabled(bool value) {
    if (cudaCalibrationEnabled_ == value) {
        return;
    }
    cudaCalibrationEnabled_ = value;
    QSettings().setValue(
            QLatin1String(kCudaCalibrationEnabledKey), value);
    emit cudaCalibrationEnabledChanged();
    refreshValidation();
}

void SearchController::setEvaluationTargetId(const QString &value) {
    if (!configuration_.setEvaluationTargetId(value)) return;
    emit evaluationTargetIdChanged();
    emit evaluationTargetSettingsChanged();
    refreshValidation();
}

void SearchController::setSearchAlgorithmSetting(const QString &key,
                                                 const QString &value) {
    if (!configuration_.setSearchAlgorithmSetting(key, value)) return;
    emit searchAlgorithmSettingsChanged();
    refreshValidation();
}

void SearchController::addModifierPass(const QString &id) {
    if (!configuration_.addModifierPass(id)) return;
    emit modifierPassesChanged();
    refreshValidation();
}

void SearchController::removeModifierPass(int index) {
    if (!configuration_.removeModifierPass(index)) return;
    emit modifierPassesChanged();
    refreshValidation();
}

void SearchController::moveModifierPass(int fromIndex, int toIndex) {
    if (!configuration_.moveModifierPass(fromIndex, toIndex)) return;
    emit modifierPassesChanged();
    refreshValidation();
}

void SearchController::setModifierPassId(int index, const QString &id) {
    if (!configuration_.setModifierPassId(index, id)) return;
    emit modifierPassesChanged();
    refreshValidation();
}

void SearchController::setModifierPassSetting(int index,
                                              const QString &key,
                                              const QString &value) {
    if (!configuration_.setModifierPassSetting(index, key, value)) return;
    emit modifierPassesChanged();
    refreshValidation();
}

void SearchController::setEvaluationTargetSetting(const QString &key,
                                                  const QString &value) {
    if (!configuration_.setEvaluationTargetSetting(key, value)) return;
    emit evaluationTargetSettingsChanged();
    refreshValidation();
}

void SearchController::setPacksDirectory(const QString &value) {
    clearAutoDetectedPacksDirectory();
    if (packsDirectory_ == value) {
        return;
    }
    packsDirectory_ = value;
    persist(kPacksDirectoryKey, value);
    emit packsDirectoryChanged();
    emit replayInputStateChanged();
    refreshValidation();
}

void SearchController::browseForPacksDirectory() {
    const QFileInfo current(packsDirectory_.isEmpty()
                                    ? autoDetectedPacksDirectory_
                                    : packsDirectory_);
    const QString initialDirectory = current.isDir()
            ? current.absoluteFilePath()
            : QDir::homePath();
    const QString selected = QFileDialog::getExistingDirectory(
            nullptr,
            QStringLiteral("Select Packs directory"),
            initialDirectory,
            QFileDialog::ShowDirsOnly);
    if (!selected.isEmpty()) {
        setPacksDirectory(selected);
    }
}

void SearchController::applyAutoDetectedPacksDirectory() {
    if (autoDetectedPacksDirectory_.isEmpty()) {
        return;
    }
    const QString detected = autoDetectedPacksDirectory_;
    setPacksDirectory(detected);
}

void SearchController::browseForReplay() {
    const QFileInfo current(replayPath_);
    const QString initialPath = current.isFile()
            ? current.absoluteFilePath()
            : QDir::homePath();
    const QString selected = QFileDialog::getOpenFileName(
            nullptr,
            QStringLiteral("Select replay"),
            initialPath,
            QStringLiteral(
                    "TrackMania replays (*.Replay.Gbx *.Gbx);;"
                    "All files (*)"));
    if (!selected.isEmpty()) {
        setReplayPath(selected);
    }
}

void SearchController::extractReplayInputs() {
    if (!canExtractReplayInputs() || inputExtractionThread_ != nullptr) {
        return;
    }
    const QString packsDirectory = QFileInfo(packsDirectory_)
            .absoluteFilePath();
    const QString replayPath = QFileInfo(replayPath_).absoluteFilePath();
    setExtractingReplayInputs(true);
    setReplayInputStatusText(QStringLiteral("Extracting replay inputs..."));

    QThread *const thread = QThread::create(
            [this, packsDirectory, replayPath]() {
                ReplayInputExtractionResult result =
                        ExtractReplayInputScript(packsDirectory, replayPath);
                QMetaObject::invokeMethod(
                        this,
                        [this,
                         packsDirectory,
                         replayPath,
                         result = std::move(result)]() mutable {
                            if (packsDirectory !=
                                        QFileInfo(packsDirectory_)
                                                .absoluteFilePath() ||
                                replayPath !=
                                        QFileInfo(replayPath_)
                                                .absoluteFilePath()) {
                                setReplayInputStatusText(QStringLiteral(
                                        "Replay selection changed; extracted "
                                        "inputs were discarded."));
                            } else if (!result.error.isEmpty()) {
                                setReplayInputStatusText(
                                        QStringLiteral(
                                                "Input extraction failed: %1")
                                                .arg(result.error));
                            } else {
                                setBaseInputScript(result.script);
                                setReplayInputStatusText(
                                        QStringLiteral(
                                                "Replay inputs extracted"));
                            }
                            setExtractingReplayInputs(false);
                        },
                        Qt::QueuedConnection);
            });
    inputExtractionThread_ = thread;
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (inputExtractionThread_ == thread) {
            inputExtractionThread_ = nullptr;
        }
        thread->deleteLater();
    });
    thread->start();
}

void SearchController::startSearch() {
    if (running_ || extractingReplayInputs_) {
        return;
    }

    const ValidationResult validation = validate();
    if (!validation.request) {
        refreshValidation();
        return;
    }

    setResultText({});
    setBestInputsText({});
    setLiveMetrics({}, {}, {}, false);
    lastCompletion_.reset();
    setProgress(true, 0.0);
    setStatusText(QStringLiteral("Starting search..."));
    setStopping(false);
    setRunning(true);

    stopRequested_ = std::make_shared<std::atomic_bool>(false);
    cancellationRequested_ = std::make_shared<std::atomic_bool>(false);
    iterationPhase_ = std::make_shared<std::atomic<SearchIterationPhase>>(
            SearchIterationPhase::Pending);
    QThread *const thread = new QThread(this);
    SearchWorker *const worker = new SearchWorker(
            *validation.request,
            stopRequested_,
            cancellationRequested_,
            iterationPhase_);
    worker->moveToThread(thread);
    workerThread_ = thread;

    connect(thread, &QThread::started, worker, &SearchWorker::run);
    connect(worker,
            &SearchWorker::stageChanged,
            this,
            [this](const QString &status, bool indeterminate) {
                setStatusText(status);
                setProgress(indeterminate, progressValue_);
            });
    connect(worker,
            &SearchWorker::progressChanged,
            this,
            [this](double value, const QString &status) {
                setStatusText(status);
                setProgress(false, value);
            });
    connect(worker,
            &SearchWorker::metricsChanged,
            this,
            [this](const QString &iterationCountText,
                   const QString &throughputText,
                   const QString &elapsedText) {
                setLiveMetrics(iterationCountText,
                               throughputText,
                               elapsedText,
                               true);
            });
    connect(worker,
            &SearchWorker::cudaBatchSizeChanged,
            this,
            [this](std::uint32_t batchSize) {
                setCudaParallelSampleCount(
                        QString::number(batchSize));
            });
    connect(worker,
            &SearchWorker::bestChanged,
            this,
            [this](const QString &summary, const QString &inputsText) {
                setResultText(summary);
                setBestInputsText(inputsText);
            });
    connect(worker,
            &SearchWorker::succeeded,
            this,
            [this](SearchCompletionPtr completion) {
                lastCompletion_ = completion;
                setResultText(completion->summary);
                setBestInputsText(completion->inputsText);
                setProgress(false, 1.0);
                setStatusText(QStringLiteral("Search complete"));
                emit searchCompleted(std::move(completion));
            });
    connect(worker, &SearchWorker::cancelled, this, [this]() {
        setStatusText(QStringLiteral("Search aborted"));
        setProgress(false, progressValue_);
    });
    connect(worker,
            &SearchWorker::failed,
            this,
            [this](const QString &message) {
                setResultText(message);
                setStatusText(QStringLiteral("Search failed"));
                setProgress(false, progressValue_);
            });
    connect(worker, &SearchWorker::finished, thread, &QThread::quit);
    connect(worker, &SearchWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (workerThread_ == thread) {
            workerThread_ = nullptr;
            stopRequested_.reset();
            cancellationRequested_.reset();
            iterationPhase_.reset();
            setStopping(false);
            setRunning(false);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
}

void SearchController::stopSearch() {
    if (!running_ || stopping_ || !stopRequested_) {
        return;
    }
    setStopping(true);
    if (iterationPhase_ != nullptr &&
        TryCancelBeforeSearchIteration(iterationPhase_) &&
        cancellationRequested_ != nullptr) {
        cancellationRequested_->store(true, std::memory_order_relaxed);
        setStatusText(QStringLiteral("Aborting search startup..."));
        return;
    }
    stopRequested_->store(true, std::memory_order_relaxed);
    setStatusText(QStringLiteral("Stopping after current iteration..."));
}

SearchController::ValidationResult SearchController::validate() const {
    const QFileInfo packsInfo(packsDirectory_);
    if (packsDirectory_.isEmpty()) {
        return {{}, QStringLiteral("Select a Packs directory.")};
    }
    if (!packsInfo.exists() || !packsInfo.isDir() ||
        !packsInfo.isReadable()) {
        return {{}, QStringLiteral(
                            "The Packs path must be a readable directory.")};
    }

    const QFileInfo replayInfo(replayPath_);
    if (replayPath_.isEmpty()) {
        return {{}, QStringLiteral("Select a replay file.")};
    }
    if (!replayInfo.exists() || !replayInfo.isFile() ||
        !replayInfo.isReadable()) {
        return {{}, QStringLiteral(
                            "The replay path must be a readable file.")};
    }

    const SearchConfigurationValidation configurationValidation =
            configuration_.validate(kSearchTickDurationMs);
    if (!configurationValidation.configuration) {
        return {{}, configurationValidation.error};
    }
    const SearchComponentConfiguration &configuration =
            *configurationValidation.configuration;
    if (!baseInputScriptError_.isEmpty()) {
        return {{}, baseInputScriptError_};
    }

    std::uint32_t parallelSampleCount = 1u;
    bool calibrateCudaParallelSampleCount = false;
#if FOREVERVALIDATOR_HAS_CUDA
    if (simulationBackend_ == PhysicsBackend::Cuda) {
        calibrateCudaParallelSampleCount =
                cudaCalibrationEnabled_;
        if (!calibrateCudaParallelSampleCount) {
            bool parsed = false;
            const QString trimmed =
                    cudaParallelSampleCount_.trimmed();
            const uint value = trimmed.toUInt(&parsed);
            if (!parsed ||
                trimmed != cudaParallelSampleCount_ ||
                value == 0u) {
                return {
                        {},
                        QStringLiteral(
                                "CUDA parallel samples must be a positive "
                                "whole number.")};
            }
            parallelSampleCount = value;
        }
    }
#endif

    return {
            SearchRequest{
                    packsInfo.absoluteFilePath().toUtf8().toStdString(),
                    replayInfo.absoluteFilePath().toUtf8().toStdString(),
                    simulationBackend_,
                    parallelSampleCount,
                    calibrateCudaParallelSampleCount,
                    configuration.searchAlgorithm,
                    configuration.modifiers,
                    configuration.evaluationTarget,
                    parsedBaseInputCommands_},
            {}};
}

void SearchController::refreshValidation() {
    const QString newMessage = validate().error;
    const bool newValid = newMessage.isEmpty();
    const bool oldCanStart = canStart();
    const bool messageChanged = validationMessage_ != newMessage;
    valid_ = newValid;
    validationMessage_ = newMessage;
    if (messageChanged) {
        emit validationChanged();
    }
    if (oldCanStart != canStart()) {
        emit canStartChanged();
    }
}

void SearchController::setRunning(bool value) {
    if (running_ == value) {
        return;
    }
    const bool oldCanStart = canStart();
    running_ = value;
    emit runningChanged();
    emit replayInputStateChanged();
    if (oldCanStart != canStart()) {
        emit canStartChanged();
    }
}

void SearchController::setExtractingReplayInputs(bool value) {
    if (extractingReplayInputs_ == value) {
        return;
    }
    const bool oldCanStart = canStart();
    extractingReplayInputs_ = value;
    emit replayInputStateChanged();
    if (oldCanStart != canStart()) {
        emit canStartChanged();
    }
}

void SearchController::setReplayInputStatusText(const QString &value) {
    if (replayInputStatusText_ == value) {
        return;
    }
    replayInputStatusText_ = value;
    emit replayInputStateChanged();
}

void SearchController::setStopping(bool value) {
    if (stopping_ == value) {
        return;
    }
    stopping_ = value;
    emit stoppingChanged();
}

void SearchController::setStatusText(const QString &value) {
    if (statusText_ == value) {
        return;
    }
    statusText_ = value;
    emit statusChanged();
}

void SearchController::setLiveMetrics(
        const QString &iterationCountText,
        const QString &throughputText,
        const QString &elapsedText,
        bool visible) {
    if (iterationCountText_ == iterationCountText &&
        throughputText_ == throughputText && elapsedText_ == elapsedText &&
        liveMetricsVisible_ == visible) {
        return;
    }
    iterationCountText_ = iterationCountText;
    throughputText_ = throughputText;
    elapsedText_ = elapsedText;
    liveMetricsVisible_ = visible;
    emit metricsChanged();
}

void SearchController::setResultText(const QString &value) {
    if (resultText_ == value) {
        return;
    }
    resultText_ = value;
    emit resultChanged();
}

void SearchController::setBestInputsText(const QString &value) {
    if (bestInputsText_ == value) {
        return;
    }
    bestInputsText_ = value;
    emit resultChanged();
}

void SearchController::setProgress(bool indeterminate, double value) {
    value = std::clamp(value, 0.0, 1.0);
    if (progressIndeterminate_ == indeterminate &&
        progressValue_ == value) {
        return;
    }
    progressIndeterminate_ = indeterminate;
    progressValue_ = value;
    emit progressChanged();
}

void SearchController::scheduleAutoDetectPacksDirectory(
        const QStringList *packsSearchPatterns) {
    if (autoDetectionScheduled_ || !packsDirectory_.trimmed().isEmpty()) {
        return;
    }
    if (packsSearchPatterns == nullptr &&
        gAutomaticPacksSearchScheduled.exchange(
                true, std::memory_order_relaxed)) {
        return;
    }
    autoDetectionScheduled_ = true;
    const std::optional<QStringList> patterns = packsSearchPatterns == nullptr
            ? std::nullopt
            : std::optional<QStringList>(*packsSearchPatterns);

    QTimer::singleShot(0, this, [this, patterns]() {
        if (!packsDirectory_.trimmed().isEmpty() ||
            autoDetectionThread_ != nullptr) {
            return;
        }

        QThread *const thread = QThread::create([this, patterns]() {
            const QString detected = patterns
                    ? FindInstalledPacksDirectory(*patterns)
                    : FindInstalledPacksDirectory();
            QMetaObject::invokeMethod(
                    this,
                    [this, detected]() {
                        publishAutoDetectedPacksDirectory(detected);
                    },
                    Qt::QueuedConnection);
        });
        autoDetectionThread_ = thread;
        connect(
                thread,
                &QThread::finished,
                this,
                [this, thread]() {
                    if (autoDetectionThread_ == thread) {
                        autoDetectionThread_ = nullptr;
                    }
                    thread->deleteLater();
                },
                Qt::QueuedConnection);
        thread->start();
    });
}

void SearchController::publishAutoDetectedPacksDirectory(
        const QString &detected) {
    if (detected.isEmpty() || !packsDirectory_.trimmed().isEmpty()) {
        return;
    }
    autoDetectedPacksDirectory_ = detected;
    emit autoDetectedPacksDirectoryChanged();
}

void SearchController::clearAutoDetectedPacksDirectory() {
    if (autoDetectedPacksDirectory_.isEmpty()) {
        return;
    }
    autoDetectedPacksDirectory_.clear();
    emit autoDetectedPacksDirectoryChanged();
}

void SearchController::persist(const char *key, const QString &value) {
    QSettings().setValue(QLatin1String(key), value);
}

void SearchController::waitForWorker() {
    if (autoDetectionThread_ != nullptr) {
        disconnect(autoDetectionThread_, nullptr, this, nullptr);
        autoDetectionThread_->wait();
        delete autoDetectionThread_;
        autoDetectionThread_ = nullptr;
    }
    if (inputExtractionThread_ != nullptr) {
        disconnect(inputExtractionThread_, nullptr, this, nullptr);
        inputExtractionThread_->wait();
        delete inputExtractionThread_;
        inputExtractionThread_ = nullptr;
    }
    if (stopRequested_) {
        stopRequested_->store(true, std::memory_order_relaxed);
    }
    if (cancellationRequested_) {
        cancellationRequested_->store(true, std::memory_order_relaxed);
    }
    if (workerThread_ != nullptr) {
        workerThread_->quit();
        workerThread_->wait();
        workerThread_ = nullptr;
    }
}

}  // namespace forevertas::app
