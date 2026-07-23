#include "app/search_controller.h"

#include "app/packs_directory_finder.h"
#include "app/search_worker.h"

#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <limits>

namespace forevertas::app {
namespace {

constexpr char kPacksDirectoryKey[] = "paths/packsDirectory";
constexpr char kReplayPathKey[] = "paths/replayPath";
constexpr char kMinMutateMsKey[] = "search/minMutateMs";
constexpr char kMaxMutateMsKey[] = "search/maxMutateMs";
constexpr char kMinEvalTimeMsKey[] = "search/minEvalTimeMs";
constexpr char kMaxEvalTimeMsKey[] = "search/maxEvalTimeMs";
constexpr char kAttemptCountKey[] = "search/attemptCount";
constexpr char kMutationSeedKey[] = "search/mutationSeed";
std::atomic_bool gAutomaticPacksSearchAttempted{false};

bool IsDecimal(const QString &value) {
    if (value.isEmpty()) {
        return false;
    }
    for (const QChar character : value) {
        if (!character.isDigit()) {
            return false;
        }
    }
    return true;
}

std::optional<std::int64_t> ParseSigned(const QString &value) {
    if (!IsDecimal(value)) {
        return std::nullopt;
    }
    bool okay = false;
    const qlonglong parsed = value.toLongLong(&okay, 10);
    if (!okay) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(parsed);
}

std::optional<std::uint64_t> ParseUnsigned64(const QString &value) {
    if (!IsDecimal(value)) {
        return std::nullopt;
    }
    bool okay = false;
    const qulonglong parsed = value.toULongLong(&okay, 10);
    if (!okay) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(parsed);
}

std::optional<std::uint32_t> ParseUnsigned32(const QString &value) {
    const std::optional<std::uint64_t> parsed = ParseUnsigned64(value);
    if (!parsed ||
        *parsed > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*parsed);
}

QString StoredValue(const char *key, const QString &fallback) {
    return QSettings().value(QLatin1String(key), fallback).toString();
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
    const SerialBruteForceSettings defaults =
            DefaultSerialBruteForceSettings();
    packsDirectory_ = StoredValue(kPacksDirectoryKey, {});
    replayPath_ = StoredValue(kReplayPathKey, {});
    minMutateMs_ = StoredValue(
            kMinMutateMsKey, QString::number(defaults.minMutateMs));
    maxMutateMs_ = StoredValue(
            kMaxMutateMsKey, QString::number(defaults.maxMutateMs));
    minEvalTimeMs_ = StoredValue(
            kMinEvalTimeMsKey, QString::number(defaults.minEvalTimeMs));
    maxEvalTimeMs_ = StoredValue(
            kMaxEvalTimeMsKey, QString::number(defaults.maxEvalTimeMs));
    attemptCount_ = StoredValue(
            kAttemptCountKey,
            QString::number(static_cast<qulonglong>(
                    defaults.attemptCount)));
    mutationSeed_ = StoredValue(
            kMutationSeedKey,
            QString::number(static_cast<qulonglong>(
                    defaults.mutationSeed)));
    scheduleAutoDetectPacksDirectory(packsSearchPatterns);
    refreshValidation();
}

SearchController::~SearchController() {
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

QString SearchController::minMutateMs() const {
    return minMutateMs_;
}

QString SearchController::maxMutateMs() const {
    return maxMutateMs_;
}

QString SearchController::minEvalTimeMs() const {
    return minEvalTimeMs_;
}

QString SearchController::maxEvalTimeMs() const {
    return maxEvalTimeMs_;
}

QString SearchController::attemptCount() const {
    return attemptCount_;
}

QString SearchController::mutationSeed() const {
    return mutationSeed_;
}

bool SearchController::canStart() const {
    return valid_ && !running_;
}

bool SearchController::running() const {
    return running_;
}

bool SearchController::cancelling() const {
    return cancelling_;
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

QString SearchController::resultText() const {
    return resultText_;
}

#define FOREVERTAS_DEFINE_STRING_SETTER(Method, Member, Signal, Key)          \
    void SearchController::Method(const QString &value) {                    \
        if (Member == value) {                                               \
            return;                                                          \
        }                                                                    \
        Member = value;                                                      \
        persist(Key, value);                                                 \
        emit Signal();                                                       \
        refreshValidation();                                                 \
    }

FOREVERTAS_DEFINE_STRING_SETTER(
        setReplayPath, replayPath_, replayPathChanged, kReplayPathKey)
FOREVERTAS_DEFINE_STRING_SETTER(
        setMinMutateMs, minMutateMs_, minMutateMsChanged, kMinMutateMsKey)
FOREVERTAS_DEFINE_STRING_SETTER(
        setMaxMutateMs, maxMutateMs_, maxMutateMsChanged, kMaxMutateMsKey)
FOREVERTAS_DEFINE_STRING_SETTER(
        setMinEvalTimeMs,
        minEvalTimeMs_,
        minEvalTimeMsChanged,
        kMinEvalTimeMsKey)
FOREVERTAS_DEFINE_STRING_SETTER(
        setMaxEvalTimeMs,
        maxEvalTimeMs_,
        maxEvalTimeMsChanged,
        kMaxEvalTimeMsKey)
FOREVERTAS_DEFINE_STRING_SETTER(
        setAttemptCount,
        attemptCount_,
        attemptCountChanged,
        kAttemptCountKey)
FOREVERTAS_DEFINE_STRING_SETTER(
        setMutationSeed,
        mutationSeed_,
        mutationSeedChanged,
        kMutationSeedKey)

#undef FOREVERTAS_DEFINE_STRING_SETTER

void SearchController::setPacksDirectory(const QString &value) {
    clearAutoDetectedPacksDirectory();
    if (packsDirectory_ == value) {
        return;
    }
    packsDirectory_ = value;
    persist(kPacksDirectoryKey, value);
    emit packsDirectoryChanged();
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

void SearchController::startSearch() {
    if (running_) {
        return;
    }

    const ValidationResult validation = validate();
    if (!validation.request) {
        refreshValidation();
        return;
    }

    setResultText({});
    setProgress(true, 0.0);
    setStatusText(QStringLiteral("Starting search..."));
    setCancelling(false);
    setRunning(true);

    cancellationRequested_ = std::make_shared<std::atomic_bool>(false);
    QThread *const thread = new QThread(this);
    SearchWorker *const worker = new SearchWorker(
            *validation.request, cancellationRequested_);
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
            &SearchWorker::succeeded,
            this,
            [this](const QString &summary) {
                setResultText(summary);
                setProgress(false, 1.0);
                setStatusText(QStringLiteral("Search complete"));
            });
    connect(worker, &SearchWorker::cancelled, this, [this]() {
        setStatusText(QStringLiteral("Cancelled"));
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
            cancellationRequested_.reset();
            setCancelling(false);
            setRunning(false);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
}

void SearchController::cancelSearch() {
    if (!running_ || cancelling_ || !cancellationRequested_) {
        return;
    }
    cancellationRequested_->store(true, std::memory_order_relaxed);
    setCancelling(true);
    setStatusText(QStringLiteral("Cancelling..."));
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

    const auto minMutateMs = ParseSigned(minMutateMs_);
    if (!minMutateMs) {
        return {{}, QStringLiteral(
                            "Minimum mutation time must be a non-negative "
                            "64-bit decimal integer.")};
    }
    const auto maxMutateMs = ParseSigned(maxMutateMs_);
    if (!maxMutateMs) {
        return {{}, QStringLiteral(
                            "Maximum mutation time must be a non-negative "
                            "64-bit decimal integer.")};
    }
    const auto minEvalTimeMs = ParseSigned(minEvalTimeMs_);
    if (!minEvalTimeMs) {
        return {{}, QStringLiteral(
                            "Minimum evaluation time must be a non-negative "
                            "64-bit decimal integer.")};
    }
    const auto maxEvalTimeMs = ParseSigned(maxEvalTimeMs_);
    if (!maxEvalTimeMs) {
        return {{}, QStringLiteral(
                            "Maximum evaluation time must be a non-negative "
                            "64-bit decimal integer.")};
    }
    const auto attemptCount = ParseUnsigned64(attemptCount_);
    if (!attemptCount) {
        return {{}, QStringLiteral(
                            "Attempt count must be an unsigned 64-bit "
                            "decimal integer.")};
    }
    const auto mutationSeed = ParseUnsigned32(mutationSeed_);
    if (!mutationSeed) {
        return {{}, QStringLiteral(
                            "Mutation seed must be an unsigned 32-bit "
                            "decimal integer.")};
    }

    const SerialBruteForceSettings settings{
            *minMutateMs,
            *maxMutateMs,
            *minEvalTimeMs,
            *maxEvalTimeMs,
            *attemptCount,
            *mutationSeed,
    };
    if (const auto error = ValidateSerialBruteForceSettings(
                settings, kSearchTickDurationMs)) {
        return {{}, QString::fromStdString(*error)};
    }

    return {
            SerialSearchRequest{
                    packsInfo.absoluteFilePath().toStdString(),
                    replayInfo.absoluteFilePath().toStdString(),
                    settings},
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
    if (oldCanStart != canStart()) {
        emit canStartChanged();
    }
}

void SearchController::setCancelling(bool value) {
    if (cancelling_ == value) {
        return;
    }
    cancelling_ = value;
    emit cancellingChanged();
}

void SearchController::setStatusText(const QString &value) {
    if (statusText_ == value) {
        return;
    }
    statusText_ = value;
    emit statusChanged();
}

void SearchController::setResultText(const QString &value) {
    if (resultText_ == value) {
        return;
    }
    resultText_ = value;
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
    if (autoDetectionAttempted_ || !packsDirectory_.trimmed().isEmpty()) {
        return;
    }
    if (packsSearchPatterns == nullptr &&
        gAutomaticPacksSearchAttempted.exchange(
                true, std::memory_order_relaxed)) {
        return;
    }
    autoDetectionAttempted_ = true;
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
