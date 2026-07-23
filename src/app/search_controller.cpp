#include "app/search_controller.h"

#include "app/packs_directory_finder.h"
#include "app/search_worker.h"
#include "searches/algorithm_registry.h"

#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>

namespace forevertas::app {
namespace {

constexpr char kPacksDirectoryKey[] = "paths/packsDirectory";
constexpr char kReplayPathKey[] = "paths/replayPath";
constexpr char kSearchAlgorithmKey[] = "selection/searchAlgorithm";
constexpr char kMutationAlgorithmKey[] = "selection/mutationAlgorithm";
constexpr char kEvaluationTargetKey[] = "selection/evaluationTarget";
std::atomic_bool gAutomaticPacksSearchAttempted{false};

QString StoredValue(const char *key, const QString &fallback) {
    return QSettings().value(QLatin1String(key), fallback).toString();
}

QString OptionSettingPath(const QString &category,
                          const QString &optionId,
                          const QString &key) {
    return QStringLiteral("configuration/%1/%2/%3")
            .arg(category, optionId, key);
}

OptionSettings ToOptionSettings(const QVariantMap &values) {
    OptionSettings settings;
    for (auto iterator = values.constBegin(); iterator != values.constEnd();
         ++iterator) {
        settings.emplace(iterator.key().toStdString(),
                         iterator.value().toString().toStdString());
    }
    return settings;
}

template<typename Registration>
QVariantMap LoadOptionSettings(const QString &category,
                               const Registration &registration) {
    QSettings storage;
    QVariantMap values;
    for (const auto &[key, defaultValue] : registration.defaultSettings) {
        const QString qKey = QString::fromStdString(key);
        const QString path = OptionSettingPath(
                category,
                QString::fromStdString(registration.id),
                qKey);
        QString value;
        if (storage.contains(path)) {
            value = storage.value(path).toString();
        } else {
            const auto legacy = registration.legacyPersistenceKeys.find(key);
            if (legacy != registration.legacyPersistenceKeys.end() &&
                storage.contains(QString::fromStdString(legacy->second))) {
                value = storage.value(QString::fromStdString(legacy->second))
                                .toString();
            } else {
                value = QString::fromStdString(defaultValue);
            }
        }
        values.insert(qKey, value);
    }
    return values;
}

template<typename Registration>
QVariantList OptionList(const std::vector<Registration> &registrations) {
    QVariantList options;
    options.reserve(static_cast<qsizetype>(registrations.size()));
    for (const Registration &registration : registrations) {
        options.push_back(QVariantMap{
                {QStringLiteral("id"),
                 QString::fromStdString(registration.id)},
                {QStringLiteral("label"),
                 QString::fromStdString(registration.displayName)},
                {QStringLiteral("settingsComponent"),
                 QString::fromStdString(registration.settingsComponent)}});
    }
    return options;
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
    const OptionConfiguration defaultSearch =
            DefaultSearchAlgorithmConfiguration();
    const OptionConfiguration defaultMutation =
            DefaultMutationAlgorithmConfiguration();
    const OptionConfiguration defaultEvaluation =
            DefaultEvaluationTargetConfiguration();
    packsDirectory_ = StoredValue(kPacksDirectoryKey, {});
    replayPath_ = StoredValue(kReplayPathKey, {});
    searchAlgorithmId_ = StoredValue(
            kSearchAlgorithmKey,
            QString::fromStdString(defaultSearch.id));
    mutationAlgorithmId_ = StoredValue(
            kMutationAlgorithmKey,
            QString::fromStdString(defaultMutation.id));
    evaluationTargetId_ = StoredValue(
            kEvaluationTargetKey,
            QString::fromStdString(defaultEvaluation.id));
    if (FindSearchAlgorithm(searchAlgorithmId_.toStdString()) == nullptr) {
        searchAlgorithmId_ = QString::fromStdString(defaultSearch.id);
    }
    if (FindMutationAlgorithm(mutationAlgorithmId_.toStdString()) == nullptr) {
        mutationAlgorithmId_ = QString::fromStdString(defaultMutation.id);
    }
    if (FindEvaluationTarget(evaluationTargetId_.toStdString()) == nullptr) {
        evaluationTargetId_ = QString::fromStdString(defaultEvaluation.id);
    }
    loadSearchAlgorithmSettings();
    loadMutationAlgorithmSettings();
    loadEvaluationTargetSettings();
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

QVariantList SearchController::searchAlgorithmOptions() const {
    return OptionList(SearchAlgorithmRegistry());
}

QVariantList SearchController::mutationAlgorithmOptions() const {
    return OptionList(MutationAlgorithmRegistry());
}

QVariantList SearchController::evaluationTargetOptions() const {
    return OptionList(EvaluationTargetRegistry());
}

QString SearchController::searchAlgorithmId() const {
    return searchAlgorithmId_;
}

QString SearchController::mutationAlgorithmId() const {
    return mutationAlgorithmId_;
}

QString SearchController::evaluationTargetId() const {
    return evaluationTargetId_;
}

QVariantMap SearchController::searchAlgorithmSettings() const {
    return searchAlgorithmSettings_;
}

QVariantMap SearchController::mutationAlgorithmSettings() const {
    return mutationAlgorithmSettings_;
}

QVariantMap SearchController::evaluationTargetSettings() const {
    return evaluationTargetSettings_;
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

#undef FOREVERTAS_DEFINE_STRING_SETTER

void SearchController::setSearchAlgorithmId(const QString &value) {
    if (searchAlgorithmId_ == value) {
        return;
    }
    searchAlgorithmId_ = value;
    persist(kSearchAlgorithmKey, value);
    loadSearchAlgorithmSettings();
    emit searchAlgorithmIdChanged();
    emit searchAlgorithmSettingsChanged();
    refreshValidation();
}

void SearchController::setMutationAlgorithmId(const QString &value) {
    if (mutationAlgorithmId_ == value) {
        return;
    }
    mutationAlgorithmId_ = value;
    persist(kMutationAlgorithmKey, value);
    loadMutationAlgorithmSettings();
    emit mutationAlgorithmIdChanged();
    emit mutationAlgorithmSettingsChanged();
    refreshValidation();
}

void SearchController::setEvaluationTargetId(const QString &value) {
    if (evaluationTargetId_ == value) {
        return;
    }
    evaluationTargetId_ = value;
    persist(kEvaluationTargetKey, value);
    loadEvaluationTargetSettings();
    emit evaluationTargetIdChanged();
    emit evaluationTargetSettingsChanged();
    refreshValidation();
}

void SearchController::setSearchAlgorithmSetting(const QString &key,
                                                 const QString &value) {
    const SearchAlgorithmRegistration *const registration =
            FindSearchAlgorithm(searchAlgorithmId_.toStdString());
    if (registration == nullptr ||
        registration->defaultSettings.find(key.toStdString()) ==
                registration->defaultSettings.end() ||
        searchAlgorithmSettings_.value(key).toString() == value) {
        return;
    }
    searchAlgorithmSettings_.insert(key, value);
    persistOptionSetting(
            QStringLiteral("search"), searchAlgorithmId_, key, value);
    emit searchAlgorithmSettingsChanged();
    refreshValidation();
}

void SearchController::setMutationAlgorithmSetting(const QString &key,
                                                   const QString &value) {
    const MutationAlgorithmRegistration *const registration =
            FindMutationAlgorithm(mutationAlgorithmId_.toStdString());
    if (registration == nullptr ||
        registration->defaultSettings.find(key.toStdString()) ==
                registration->defaultSettings.end() ||
        mutationAlgorithmSettings_.value(key).toString() == value) {
        return;
    }
    mutationAlgorithmSettings_.insert(key, value);
    persistOptionSetting(
            QStringLiteral("mutation"), mutationAlgorithmId_, key, value);
    emit mutationAlgorithmSettingsChanged();
    refreshValidation();
}

void SearchController::setEvaluationTargetSetting(const QString &key,
                                                  const QString &value) {
    const EvaluationTargetRegistration *const registration =
            FindEvaluationTarget(evaluationTargetId_.toStdString());
    if (registration == nullptr ||
        registration->defaultSettings.find(key.toStdString()) ==
                registration->defaultSettings.end() ||
        evaluationTargetSettings_.value(key).toString() == value) {
        return;
    }
    evaluationTargetSettings_.insert(key, value);
    persistOptionSetting(
            QStringLiteral("evaluation"), evaluationTargetId_, key, value);
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

    const SearchAlgorithmRegistration *const searchRegistration =
            FindSearchAlgorithm(searchAlgorithmId_.toStdString());
    if (searchRegistration == nullptr) {
        return {{}, QStringLiteral("Select a valid search algorithm.")};
    }
    const MutationAlgorithmRegistration *const mutationRegistration =
            FindMutationAlgorithm(mutationAlgorithmId_.toStdString());
    if (mutationRegistration == nullptr) {
        return {{}, QStringLiteral("Select a valid mutation algorithm.")};
    }
    const EvaluationTargetRegistration *const evaluationRegistration =
            FindEvaluationTarget(evaluationTargetId_.toStdString());
    if (evaluationRegistration == nullptr) {
        return {{}, QStringLiteral("Select a valid evaluation target.")};
    }

    const OptionSettings searchSettings =
            ToOptionSettings(searchAlgorithmSettings_);
    const OptionSettings mutationSettings =
            ToOptionSettings(mutationAlgorithmSettings_);
    const OptionSettings evaluationSettings =
            ToOptionSettings(evaluationTargetSettings_);
    if (const auto error = searchRegistration->validateSettings(
                searchSettings, kSearchTickDurationMs)) {
        return {{}, QString::fromStdString(*error)};
    }
    if (const auto error =
                mutationRegistration->validateSettings(mutationSettings)) {
        return {{}, QString::fromStdString(*error)};
    }
    if (const auto error =
                evaluationRegistration->validateSettings(evaluationSettings)) {
        return {{}, QString::fromStdString(*error)};
    }

    return {
            SearchRequest{
                    packsInfo.absoluteFilePath().toStdString(),
                    replayInfo.absoluteFilePath().toStdString(),
                    {searchAlgorithmId_.toStdString(), searchSettings},
                    {mutationAlgorithmId_.toStdString(), mutationSettings},
                    {evaluationTargetId_.toStdString(), evaluationSettings}},
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

void SearchController::loadSearchAlgorithmSettings() {
    const SearchAlgorithmRegistration *const registration =
            FindSearchAlgorithm(searchAlgorithmId_.toStdString());
    searchAlgorithmSettings_ = registration == nullptr
            ? QVariantMap{}
            : LoadOptionSettings(QStringLiteral("search"), *registration);
}

void SearchController::loadMutationAlgorithmSettings() {
    const MutationAlgorithmRegistration *const registration =
            FindMutationAlgorithm(mutationAlgorithmId_.toStdString());
    mutationAlgorithmSettings_ = registration == nullptr
            ? QVariantMap{}
            : LoadOptionSettings(QStringLiteral("mutation"), *registration);
}

void SearchController::loadEvaluationTargetSettings() {
    const EvaluationTargetRegistration *const registration =
            FindEvaluationTarget(evaluationTargetId_.toStdString());
    evaluationTargetSettings_ = registration == nullptr
            ? QVariantMap{}
            : LoadOptionSettings(QStringLiteral("evaluation"), *registration);
}

void SearchController::persistOptionSetting(const QString &category,
                                            const QString &optionId,
                                            const QString &key,
                                            const QString &value) {
    QSettings().setValue(OptionSettingPath(category, optionId, key), value);
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
