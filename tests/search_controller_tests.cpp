#include "app/packs_directory_finder.h"
#include "app/search_configuration_model.h"
#include "app/search_controller.h"
#include "app/search_worker.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QVariantMap>

#include <clocale>
#include <functional>
#include <iostream>
#include <string>

namespace {

using forevertas::app::SearchController;

bool Check(bool condition, const char *message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}


bool WaitUntil(const std::function<bool()> &condition, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return condition();
}

class NumericLocaleGuard final {
public:
    NumericLocaleGuard() {
        if (const char *const current = std::setlocale(LC_NUMERIC, nullptr)) {
            original_ = current;
        }
    }

    ~NumericLocaleGuard() {
        if (!original_.empty()) {
            std::setlocale(LC_NUMERIC, original_.c_str());
        }
    }

    bool ActivateCommaDecimalLocale() {
        constexpr const char *localeNames[] = {
                "fr_FR.utf8", "fr_FR.UTF-8", "de_DE.utf8", "de_DE.UTF-8"};
        for (const char *const localeName : localeNames) {
            if (std::setlocale(LC_NUMERIC, localeName) == nullptr) continue;
            const lconv *const details = std::localeconv();
            if (details != nullptr && details->decimal_point != nullptr &&
                std::string(details->decimal_point) == ",") {
                return true;
            }
        }
        return false;
    }

private:
    std::string original_;
};

bool HasOption(const QVariantList &options,
               const QString &id,
               const QString &component) {
    for (const QVariant &value : options) {
        const QVariantMap option = value.toMap();
        if (option.value(QStringLiteral("id")).toString() == id &&
            option.value(QStringLiteral("settingsComponent")).toString() ==
                    component) {
            return true;
        }
    }
    return false;
}

bool HasBackendOption(const QVariantList &options,
                      const QString &id,
                      const QString &label,
                      const QString &description) {
    for (const QVariant &value : options) {
        const QVariantMap option = value.toMap();
        if (option.value(QStringLiteral("id")).toString() == id &&
            option.value(QStringLiteral("label")).toString() == label &&
            option.value(QStringLiteral("description")).toString() ==
                    description) {
            return true;
        }
    }
    return false;
}

QVariantMap Pass(const SearchController &controller, int index) {
    return controller.modifierPasses().at(index).toMap();
}

QString PassId(const SearchController &controller, int index) {
    return Pass(controller, index).value(QStringLiteral("id")).toString();
}

QVariantMap PassSettings(const SearchController &controller, int index) {
    return Pass(controller, index)
            .value(QStringLiteral("settings"))
            .toMap();
}

void SetValidPaths(SearchController &controller,
                   const QString &packsDirectory,
                   const QString &replayPath) {
    controller.setPacksDirectory(packsDirectory);
    controller.setReplayPath(replayPath);
}

bool TestUserTimelineConfigurationBoundary() {
    QSettings().clear();
    forevertas::app::SearchConfigurationModel configuration;
    bool okay = Check(
            configuration.setModifierPassId(
                    0, QStringLiteral("smooth-steering")),
            "failed to select a duration-bearing modifier");
    okay &= Check(configuration.setModifierPassSetting(
                              0,
                              QStringLiteral("minTimeMs"),
                              QStringLiteral("0")) &&
                          configuration.setModifierPassSetting(
                                  0,
                                  QStringLiteral("maxTimeMs"),
                                  QStringLiteral("20")) &&
                          configuration.setModifierPassSetting(
                                  0,
                                  QStringLiteral("radiusMs"),
                                  QStringLiteral("210")),
                  "failed to configure user timeline modifier values");
    okay &= Check(configuration.setEvaluationTargetSetting(
                              QStringLiteral("minTimeMs"),
                              QStringLiteral("0")) &&
                          configuration.setEvaluationTargetSetting(
                                  QStringLiteral("maxTimeMs"),
                                  QStringLiteral("20")),
                  "failed to configure user timeline evaluation values");

    const auto validated = configuration.validate(10u);
    okay &= Check(validated.configuration.has_value() &&
                          validated.error.isEmpty(),
                  "zero-based user timeline settings did not validate");
    if (!validated.configuration) return false;

    const QVariantMap userModifier =
            configuration.modifierPasses().front().toMap()
                    .value(QStringLiteral("settings"))
                    .toMap();
    const QVariantMap userEvaluation =
            configuration.evaluationTargetSettings();
    const forevertas::OptionSettings &configuredModifier =
            validated.configuration->modifiers.front().settings;
    const forevertas::OptionSettings &configuredEvaluation =
            validated.configuration->evaluationTarget.settings;
    okay &= Check(userModifier.value(QStringLiteral("minTimeMs")).toString() ==
                                  QStringLiteral("0") &&
                          userModifier.value(QStringLiteral("maxTimeMs"))
                                          .toString() ==
                                  QStringLiteral("20") &&
                          userModifier.value(QStringLiteral("radiusMs"))
                                          .toString() ==
                                  QStringLiteral("210") &&
                          userEvaluation.value(QStringLiteral("minTimeMs"))
                                          .toString() ==
                                  QStringLiteral("0") &&
                          userEvaluation.value(QStringLiteral("maxTimeMs"))
                                          .toString() ==
                                  QStringLiteral("20"),
                  "validation rewrote persisted user timeline values");
    okay &= Check(configuredModifier.at("minTimeMs") == "0" &&
                          configuredModifier.at("maxTimeMs") == "20" &&
                          configuredModifier.at("radiusMs") == "210" &&
                          configuredEvaluation.at("minTimeMs") == "0" &&
                          configuredEvaluation.at("maxTimeMs") == "20",
                  "validated configuration did not preserve user timeline values");

    const auto *const modifierRegistration = forevertas::FindModifier(
            validated.configuration->modifiers.front().id);
    const auto *const evaluationRegistration =
            forevertas::FindEvaluationTarget(
                    validated.configuration->evaluationTarget.id);
    okay &= Check(modifierRegistration != nullptr &&
                          evaluationRegistration != nullptr,
                  "validated configuration referenced an unknown component");
    if (modifierRegistration == nullptr || evaluationRegistration == nullptr) {
        return false;
    }
    const std::unique_ptr<forevertas::InputMutator> modifier =
            modifierRegistration->create(configuredModifier, 10u);
    const std::unique_ptr<forevertas::IterationEvaluator> evaluator =
            evaluationRegistration->create(configuredEvaluation, 10u);
    const forevertas::EvaluationPlan plan = evaluator->Plan(
            1000, modifier->EarliestMutationTimeMs(), 10u);
    okay &= Check(modifier->EarliestMutationTimeMs() == 10 &&
                          plan.startTimeMs == 10 && plan.endTimeMs == 30,
                  "registry did not apply exactly one timeline tick");
    return okay;
}

bool TestRegistryAndValidation(const QString &packsDirectory,
                               const QString &replayPath) {
    QSettings().clear();
    SearchController controller;
    SetValidPaths(controller, packsDirectory, replayPath);

    bool okay = Check(controller.canStart(),
                      "valid defaults and paths did not enable Start");
    okay &= Check(controller.baseInputScript().isEmpty() &&
                          controller.baseInputScriptError().isEmpty(),
                  "empty base input script was not valid by default");
    controller.setBaseInputScript(
            QStringLiteral("0.00 press up\n0.20 steer 32768"));
    okay &= Check(controller.canStart() &&
                          controller.baseInputScriptError().isEmpty(),
                  "valid base input script disabled Start");
    controller.setBaseInputScript(QStringLiteral("0.001 press up"));
    okay &= Check(!controller.canStart() &&
                          controller.baseInputScriptError().contains(
                                  QStringLiteral("Line 1")),
                  "invalid base input script did not disable Start");
    controller.setBaseInputScript({});
    okay &= Check(controller.canStart(),
                  "empty base input script did not restore Start");
#if FOREVERVALIDATOR_HAS_CUDA
    constexpr qsizetype expectedBackendCount = 4;
#else
    constexpr qsizetype expectedBackendCount = 3;
#endif
    okay &= Check(controller.simulationBackendOptions().size() ==
                          expectedBackendCount,
                  "unexpected physics backend count");
    okay &= Check(controller.simulationBackendId() ==
                          QStringLiteral("reference"),
                  "Reference was not the default physics backend");
    okay &= Check(HasBackendOption(
                          controller.simulationBackendOptions(),
                          QStringLiteral("reference"),
                          QStringLiteral("Reference"),
                          QStringLiteral("Broadest compatibility")) &&
                          HasBackendOption(
                                  controller.simulationBackendOptions(),
                                  QStringLiteral("optimized-cpu"),
                                  QStringLiteral("CPU Optimized"),
                                  QStringLiteral(
                                          "Faster runtime optimized for "
                                          "Stadium, may break compatibility "
                                          "in other environments")) &&
                          HasBackendOption(
                                  controller.simulationBackendOptions(),
                                  QStringLiteral("multi-threaded-cpu"),
                                  QStringLiteral("CPU Multi-threaded"),
                                  QStringLiteral(
                                          "Runs independent optimized CPU "
                                          "simulations across multiple "
                                          "worker threads")),
                  "physics backend metadata was not exposed");
    okay &= Check(
            controller.cpuWorkerCount() ==
                    QString::number(forevertas::DefaultCpuWorkerCount()),
            "unexpected default CPU worker count");
#if FOREVERVALIDATOR_HAS_CUDA
    okay &= Check(HasBackendOption(
                          controller.simulationBackendOptions(),
                          QStringLiteral("cuda"),
                          QStringLiteral("CUDA"),
                          QStringLiteral(
                                  "Fastest runtime optimized for Stadium, "
                                  "needs a modern NVIDIA GPU and may break "
                                  "compatibility in other environments")),
                  "CUDA metadata was not exposed");
#endif
    okay &= Check(controller.cudaParallelSampleCount() ==
                          QString::number(
                                  forevertas::kDefaultCudaParallelSampleCount),
                  "unexpected default CUDA parallel sample count");
    okay &= Check(!controller.cudaCalibrationEnabled(),
                  "CUDA calibration was unexpectedly enabled by default");
    okay &= Check(controller.searchAlgorithmOptions().size() == 1,
                  "unexpected search algorithm count");
    okay &= Check(controller.modifierOptions().size() == 5,
                  "required modifier options were not exposed");
    okay &= Check(controller.evaluationTargetOptions().size() == 6,
                  "required evaluation targets were not exposed");
    okay &= Check(
            HasOption(controller.modifierOptions(),
                      QStringLiteral("existing-event-perturbation"),
                      QStringLiteral("ExistingEventPerturbationSettings.qml")),
            "existing-event perturbation metadata was not exposed");
    okay &= Check(
            HasOption(controller.modifierOptions(),
                      QStringLiteral("smooth-steering"),
                      QStringLiteral("SmoothSteeringSettings.qml")),
            "smooth steering metadata was not exposed");
    okay &= Check(
            HasOption(controller.modifierOptions(),
                      QStringLiteral("input-insertion"),
                      QStringLiteral("InputInsertionSettings.qml")),
            "input insertion metadata was not exposed");
    okay &= Check(
            HasOption(controller.modifierOptions(),
                      QStringLiteral("input-deletion"),
                      QStringLiteral("InputDeletionSettings.qml")),
            "input deletion metadata was not exposed");
    okay &= Check(
            HasOption(controller.evaluationTargetOptions(),
                      QStringLiteral("precise-finish-time"),
                      QStringLiteral(
                              "PreciseFinishTimeEvaluationSettings.qml")),
            "precise finish target metadata was not exposed");
    okay &= Check(
            HasOption(controller.evaluationTargetOptions(),
                      QStringLiteral("volume-entry-time"),
                      QStringLiteral("VolumeEntryEvaluationSettings.qml")),
            "volume target metadata was not exposed");
    okay &= Check(
            HasOption(controller.evaluationTargetOptions(),
                      QStringLiteral("stunt-points"),
                      QStringLiteral("StuntPointsEvaluationSettings.qml")),
            "stunt points target metadata was not exposed");
    okay &= Check(controller.modifierPasses().size() == 1 &&
                          PassId(controller, 0) ==
                                  QStringLiteral("random-steering"),
                  "default modifier pass was incorrect");

    controller.setSimulationBackendId(QStringLiteral("optimized-cpu"));
    okay &= Check(controller.simulationBackendId() ==
                          QStringLiteral("optimized-cpu") &&
                          controller.canStart(),
                  "CPU Optimized backend was not selectable");
    controller.setSimulationBackendId(
            QStringLiteral("multi-threaded-cpu"));
    okay &= Check(controller.simulationBackendId() ==
                          QStringLiteral("multi-threaded-cpu") &&
                          controller.canStart(),
                  "CPU Multi-threaded backend was not selectable");
    controller.setCpuWorkerCount(QStringLiteral("0"));
    okay &= Check(!controller.canStart(),
                  "zero CPU workers enabled Start");
    controller.setCpuWorkerCount(QStringLiteral("257"));
    okay &= Check(!controller.canStart(),
                  "excessive CPU workers enabled Start");
    controller.setCpuWorkerCount(QStringLiteral("2"));
    okay &= Check(controller.canStart(),
                  "valid CPU worker count did not enable Start");
    controller.setSimulationBackendId(QStringLiteral("optimized-cpu"));
#if FOREVERVALIDATOR_HAS_CUDA
    controller.setSimulationBackendId(QStringLiteral("cuda"));
    okay &= Check(controller.simulationBackendId() ==
                          QStringLiteral("cuda") &&
                          controller.canStart(),
                  "CUDA backend was not selectable");
    controller.setCudaParallelSampleCount(QStringLiteral("0"));
    okay &= Check(!controller.canStart(),
                  "zero CUDA parallel samples enabled Start");
    controller.setCudaParallelSampleCount(QStringLiteral("8192"));
    okay &= Check(controller.canStart(),
                  "CUDA batch size above 4096 did not enable Start");
    controller.setCudaParallelSampleCount(QStringLiteral("4294967296"));
    okay &= Check(!controller.canStart(),
                  "unrepresentable CUDA parallel sample count enabled Start");
    controller.setCudaCalibrationEnabled(true);
    okay &= Check(controller.cudaCalibrationEnabled() &&
                          controller.canStart(),
                  "CUDA calibration depended on the manual sample count");
    controller.setCudaCalibrationEnabled(false);
    okay &= Check(!controller.canStart(),
                  "manual CUDA mode ignored its invalid sample count");
    controller.setCudaParallelSampleCount(QStringLiteral("512"));
    controller.setSimulationBackendId(QStringLiteral("optimized-cpu"));
#endif
    controller.setSimulationBackendId(QStringLiteral("missing-backend"));
    okay &= Check(controller.simulationBackendId() ==
                          QStringLiteral("optimized-cpu"),
                  "invalid physics backend changed the selection");
    controller.setSimulationBackendId(QStringLiteral("reference"));


    controller.setModifierPassSetting(
            0, QStringLiteral("seed"), QStringLiteral("4294967296"));
    okay &= Check(!controller.canStart(),
                  "modifier seed overflow enabled Start");
    controller.setModifierPassSetting(
            0, QStringLiteral("seed"), QStringLiteral("123"));

    controller.setModifierPassSetting(
            0, QStringLiteral("minTimeMs"), QStringLiteral("1001"));
    okay &= Check(!controller.canStart(),
                  "unaligned modifier time enabled Start");
    controller.setModifierPassSetting(
            0, QStringLiteral("minTimeMs"), QStringLiteral("1000"));

    controller.setEvaluationTargetSetting(
            QStringLiteral("minTimeMs"), QStringLiteral("1001"));
    okay &= Check(!controller.canStart(),
                  "unaligned evaluation time enabled Start");
    controller.setEvaluationTargetSetting(
            QStringLiteral("minTimeMs"), QStringLiteral("1000"));

    controller.setEvaluationTargetId(QStringLiteral("finish-time"));
    okay &= Check(
            controller.evaluationTargetId() ==
                    QStringLiteral("precise-finish-time"),
            "legacy finish target ID did not migrate to precise finish");
    controller.setEvaluationTargetId(
            QStringLiteral("precise-finish-time"));
    okay &= Check(controller.canStart(),
                  "precise finish target defaults did not validate");
    controller.setEvaluationTargetId(QStringLiteral("stunt-points"));
    okay &= Check(
            controller.canStart() &&
                    controller.evaluationTargetSettings()
                                    .value(QStringLiteral("targetTimeMs"))
                                    .toString() == QStringLiteral("6000"),
            "stunt target defaults did not validate");
    controller.setEvaluationTargetSetting(
            QStringLiteral("targetTimeMs"), QStringLiteral("6001"));
    okay &= Check(!controller.canStart(),
                  "unaligned stunt target time enabled Start");
    controller.setEvaluationTargetSetting(
            QStringLiteral("targetTimeMs"), QStringLiteral("4320"));
    okay &= Check(controller.canStart(),
                  "valid stunt target time did not enable Start");
    controller.setEvaluationTargetSetting(
            QStringLiteral("targetTimeMs"), QStringLiteral("500"));
    okay &= Check(
            !controller.canStart() &&
                    controller.validationMessage().contains(
                            QStringLiteral("first modifier time")),
            "stunt target accepted a deadline before any mutation");
    controller.setEvaluationTargetSetting(
            QStringLiteral("targetTimeMs"), QStringLiteral("4320"));
    controller.setEvaluationTargetId(QStringLiteral("missing-target"));
    okay &= Check(!controller.canStart(),
                  "unknown evaluation target enabled Start");
    controller.setEvaluationTargetId(QStringLiteral("velocity"));
    okay &= Check(controller.canStart(),
                  "restored valid target did not enable Start");
    return okay;
}

bool TestCompositionEditing(const QString &packsDirectory,
                            const QString &replayPath) {
    QSettings().clear();
    SearchController controller;
    SetValidPaths(controller, packsDirectory, replayPath);

    controller.addModifierPass(QStringLiteral("input-deletion"));
    bool okay = Check(controller.modifierPasses().size() == 2,
                      "modifier pass was not added");
    controller.setModifierPassSetting(
            1, QStringLiteral("steerMaxCount"), QStringLiteral("4"));
    okay &= Check(PassSettings(controller, 1)
                                  .value(QStringLiteral("steerMaxCount"))
                                  .toString() == QStringLiteral("4"),
                  "pass-owned setting was not changed");

    controller.moveModifierPass(1, 0);
    okay &= Check(PassId(controller, 0) ==
                          QStringLiteral("input-deletion") &&
                          PassId(controller, 1) ==
                                  QStringLiteral("random-steering"),
                  "modifier pass order did not change");

    controller.setModifierPassId(1, QStringLiteral("smooth-steering"));
    okay &= Check(PassId(controller, 1) ==
                          QStringLiteral("smooth-steering") &&
                          PassSettings(controller, 1).contains(
                                  QStringLiteral("radiusMs")),
                  "modifier pass type did not replace its settings");

    controller.removeModifierPass(1);
    controller.removeModifierPass(0);
    okay &= Check(controller.modifierPasses().isEmpty(),
                  "modifier passes were not removed");
    okay &= Check(!controller.canStart(),
                  "empty modifier composition enabled Start");
    controller.addModifierPass(QStringLiteral("random-steering"));
    okay &= Check(controller.canStart(),
                  "restored modifier composition did not enable Start");
    return okay;
}

bool TestPersistence(const QString &packsDirectory,
                     const QString &replayPath) {
    QSettings().clear();
    {
        SearchController controller;
        SetValidPaths(controller, packsDirectory, replayPath);
        controller.setModifierPassSetting(
                0, QStringLiteral("seed"), QStringLiteral("321"));
        controller.addModifierPass(QStringLiteral("input-deletion"));
        controller.setModifierPassSetting(
                1, QStringLiteral("steerMaxCount"), QStringLiteral("4"));
        controller.moveModifierPass(1, 0);
        controller.setSimulationBackendId(QStringLiteral("optimized-cpu"));
        controller.setCpuWorkerCount(QStringLiteral("6"));
        controller.setCudaParallelSampleCount(QStringLiteral("384"));
        controller.setCudaCalibrationEnabled(true);
        controller.setEvaluationTargetId(QStringLiteral("point-target"));
        controller.setEvaluationTargetSetting(
                QStringLiteral("x"), QStringLiteral("12.5"));
        controller.setEvaluationTargetId(QStringLiteral("stunt-points"));
        controller.setEvaluationTargetSetting(
                QStringLiteral("targetTimeMs"), QStringLiteral("4320"));
        controller.setBaseInputScript(
                QStringLiteral("0.00 press up\n0.50 steer -16384"));
        QSettings().sync();
    }

    SearchController restored;
    bool okay = Check(restored.searchAlgorithmSettings().isEmpty(),
                      "parameterless search exposed persisted settings");
    okay &= Check(
            restored.baseInputScript() ==
                    QStringLiteral("0.00 press up\n0.50 steer -16384") &&
                    restored.baseInputScriptError().isEmpty(),
            "base input script was not persisted");
    okay &= Check(restored.simulationBackendId() ==
                          QStringLiteral("optimized-cpu"),
                  "physics backend selection was not persisted");
    okay &= Check(restored.cudaParallelSampleCount() ==
                          QStringLiteral("384"),
                  "CUDA parallel sample count was not persisted");
    okay &= Check(restored.cpuWorkerCount() == QStringLiteral("6"),
                  "CPU worker count was not persisted");
    okay &= Check(restored.cudaCalibrationEnabled(),
                  "CUDA calibration mode was not persisted");
    okay &= Check(restored.modifierPasses().size() == 2,
                  "modifier pass count was not persisted");
    okay &= Check(PassId(restored, 0) == QStringLiteral("input-deletion") &&
                          PassSettings(restored, 0)
                                          .value(QStringLiteral(
                                                  "steerMaxCount"))
                                          .toString() == QStringLiteral("4"),
                  "first modifier pass was not persisted");
    okay &= Check(PassId(restored, 1) == QStringLiteral("random-steering") &&
                          PassSettings(restored, 1)
                                          .value(QStringLiteral("seed"))
                                          .toString() == QStringLiteral("321"),
                  "second modifier pass was not persisted");
    okay &= Check(restored.evaluationTargetId() ==
                          QStringLiteral("stunt-points") &&
                          restored.evaluationTargetSettings()
                                          .value(QStringLiteral("targetTimeMs"))
                                          .toString() == QStringLiteral("4320"),
                  "evaluation target configuration was not persisted");
    okay &= Check(QSettings().contains(
                          QStringLiteral("composition/modifiers")),
                  "modifier composition JSON was not persisted");
    okay &= Check(QSettings().value(
                                  QStringLiteral(
                                          "selection/simulationBackend"))
                                  .toString() ==
                          QStringLiteral("optimized-cpu"),
                  "physics backend setting was not stored canonically");
    okay &= Check(QSettings().value(QStringLiteral(
                                  "backends/cuda/parallelSampleCount"))
                                  .toString() == QStringLiteral("384"),
                  "CUDA parallel sample count was not stored canonically");
    okay &= Check(QSettings().value(QStringLiteral(
                                  "backends/cpu/workerCount"))
                                  .toString() == QStringLiteral("6"),
                  "CPU worker count was not stored canonically");
    okay &= Check(QSettings().value(QStringLiteral(
                                  "backends/cuda/calibrationEnabled"))
                                  .toBool(),
                  "CUDA calibration mode was not stored canonically");
    return okay;
}

bool TestExtractionFailurePreservesDraft(const QString &packsDirectory,
                                         const QString &replayPath) {
    QSettings().clear();
    SearchController controller;
    SetValidPaths(controller, packsDirectory, replayPath);
    const QString draft =
            QStringLiteral("0.00 press up\n0.50 steer -16384");
    controller.setBaseInputScript(draft);
    controller.extractReplayInputs();
    const bool finished = WaitUntil(
            [&controller]() {
                return !controller.extractingReplayInputs();
            },
            5000);
    return Check(
            finished &&
                    controller.baseInputScript() == draft &&
                    controller.replayInputStatusText().startsWith(
                            QStringLiteral("Input extraction failed:")),
            "failed extraction replaced the existing base script");
}

bool TestExtractionWorkerShutdown(const QString &packsDirectory,
                                  const QString &replayPath) {
    QElapsedTimer elapsed;
    elapsed.start();
    {
        SearchController controller;
        SetValidPaths(controller, packsDirectory, replayPath);
        controller.extractReplayInputs();
    }
    return Check(elapsed.elapsed() < 5000,
                 "input extraction worker did not stop during shutdown");
}

bool TestDescriptiveSearchStageStatuses() {
    using forevertas::SearchProgressStage;
    using forevertas::app::SearchStageStatus;

    bool okay = Check(
            SearchStageStatus(
                    SearchProgressStage::OpeningPacksDirectory,
                    "reference") ==
                    QStringLiteral("Opening Packs directory..."),
            "Packs loading stage was not descriptive");
    okay &= Check(
            SearchStageStatus(
                    SearchProgressStage::ReadingReplay,
                    "reference") ==
                    QStringLiteral("Reading replay file..."),
            "replay reading stage was not descriptive");
    okay &= Check(
            SearchStageStatus(
                    SearchProgressStage::CreatingSimulation,
                    "optimized-cpu")
                    .contains(QStringLiteral("optimized CPU")),
            "optimized CPU initialization was not identified");
    okay &= Check(
            SearchStageStatus(
                    SearchProgressStage::PreparingSearch,
                    "multi-threaded-cpu")
                            .contains(QStringLiteral(
                                    "independent optimized CPU workers")) &&
                    SearchStageStatus(
                            SearchProgressStage::Mutations,
                            "multi-threaded-cpu")
                            .contains(QStringLiteral(
                                    "across optimized CPU workers")),
            "multi-threaded CPU stages did not identify worker aggregation");
    const QString cudaInitialization = SearchStageStatus(
            SearchProgressStage::CreatingSimulation,
            "cuda");
    const QString cudaReplayLoad = SearchStageStatus(
            SearchProgressStage::LoadingReplay,
            "cuda");
    const QString cudaBaseline = SearchStageStatus(
            SearchProgressStage::Baseline,
            "cuda");
    const QString cudaCalibration = SearchStageStatus(
            SearchProgressStage::Calibration,
            "cuda");
    const QString cudaMutations = SearchStageStatus(
            SearchProgressStage::Mutations,
            "cuda");
    okay &= Check(
            cudaInitialization.contains(QStringLiteral("CUDA")) &&
                    cudaInitialization.contains(
                            QStringLiteral("GPU availability")) &&
                    cudaReplayLoad.contains(
                            QStringLiteral("GPU availability")) &&
                    cudaBaseline.contains(
                            QStringLiteral("GPU availability")) &&
                    cudaCalibration.contains(
                            QStringLiteral("GPU availability")) &&
                    cudaMutations.contains(
                            QStringLiteral("GPU availability")),
            "CUDA wait-prone stages did not explain GPU availability waits");
    okay &= Check(
            SearchStageStatus(
                    SearchProgressStage::FinalSamplingSetup,
                    "reference")
                    .contains(QStringLiteral("final best-run sampling")),
            "final sampling setup was not identified");
    return okay;
}

bool TestIterationBoundaryArbitration() {
    using forevertas::SearchIterationPhase;
    using forevertas::app::TryBeginSearchIteration;
    using forevertas::app::TryCancelBeforeSearchIteration;

    auto cancelled =
            std::make_shared<std::atomic<SearchIterationPhase>>(
                    SearchIterationPhase::Pending);
    bool okay = Check(
            TryCancelBeforeSearchIteration(cancelled) &&
                    !TryBeginSearchIteration(cancelled) &&
                    cancelled->load(std::memory_order_acquire) ==
                            SearchIterationPhase::Cancelled,
            "a pre-iteration cancellation did not win the boundary");

    auto started =
            std::make_shared<std::atomic<SearchIterationPhase>>(
                    SearchIterationPhase::Pending);
    okay &= Check(
            TryBeginSearchIteration(started) &&
                    !TryCancelBeforeSearchIteration(started) &&
                    TryBeginSearchIteration(started) &&
                    started->load(std::memory_order_acquire) ==
                            SearchIterationPhase::Started,
            "a started iteration did not retain the boundary");
    return okay;
}

bool TestStopAbortsBeforeFirstIteration(const QString &packsDirectory,
                                        const QString &replayPath) {
    QSettings().clear();
    SearchController controller;
    SetValidPaths(controller, packsDirectory, replayPath);
    QSignalSpy completionSpy(
            &controller, &SearchController::searchCompleted);

    QElapsedTimer elapsed;
    elapsed.start();
    controller.startSearch();
    controller.stopSearch();
    bool okay = Check(
            controller.running() && controller.stopping() &&
                    controller.statusText() ==
                            QStringLiteral("Aborting search startup..."),
            "Stop did not request an immediate startup abort");
    okay &= Check(
            WaitUntil([&controller]() { return !controller.running(); }, 5000),
            "startup abort did not terminate promptly");
    okay &= Check(
            elapsed.elapsed() < 5000 &&
                    controller.statusText() ==
                            QStringLiteral("Search aborted") &&
                    completionSpy.isEmpty() &&
                    controller.resultText().isEmpty(),
            "startup abort ran or completed a search iteration");
    return okay;
}

bool TestLocaleIndependentPersistedDecimals(const QString &packsDirectory,
                                             const QString &replayPath) {
    NumericLocaleGuard locale;
    if (!locale.ActivateCommaDecimalLocale()) {
        return Check(false, "no comma-decimal locale is installed for testing");
    }

    QSettings().clear();
    {
        SearchController controller;
        SetValidPaths(controller, packsDirectory, replayPath);
        controller.setEvaluationTargetId(QStringLiteral("point-target"));
        controller.setEvaluationTargetSetting(
                QStringLiteral("x"), QStringLiteral("12.5"));
        controller.setEvaluationTargetSetting(
                QStringLiteral("y"), QStringLiteral("-3.25"));
        controller.setModifierPassId(
                0, QStringLiteral("existing-event-perturbation"));
        controller.setModifierPassSetting(
                0, QStringLiteral("steerDeltaMin"), QStringLiteral("-0.25"));
        controller.setModifierPassSetting(
                0, QStringLiteral("steerDeltaMax"), QStringLiteral("0.25"));

        bool okay = Check(
                controller.canStart(),
                "UI-entered dot decimals failed under comma LC_NUMERIC");
        controller.setEvaluationTargetSetting(
                QStringLiteral("x"), QStringLiteral("12,5"));
        okay &= Check(!controller.canStart(),
                      "UI-entered comma decimal was accepted");
        controller.setEvaluationTargetSetting(
                QStringLiteral("x"), QStringLiteral("12.5"));
        okay &= Check(controller.canStart(),
                      "restoring a dot decimal did not restore validation");
        if (!okay) return false;
        QSettings().sync();
    }

    SearchController restored;
    SetValidPaths(restored, packsDirectory, replayPath);
    bool okay = Check(
            restored.canStart(),
            "persisted dot decimals failed under comma LC_NUMERIC");
    okay &= Check(
            restored.evaluationTargetId() == QStringLiteral("point-target") &&
                    restored.evaluationTargetSettings()
                                    .value(QStringLiteral("x"))
                                    .toString() == QStringLiteral("12.5") &&
                    restored.evaluationTargetSettings()
                                    .value(QStringLiteral("y"))
                                    .toString() == QStringLiteral("-3.25"),
            "persisted evaluation decimals changed representation");
    okay &= Check(
            PassId(restored, 0) ==
                            QStringLiteral("existing-event-perturbation") &&
                    PassSettings(restored, 0)
                                    .value(QStringLiteral("steerDeltaMin"))
                                    .toString() == QStringLiteral("-0.25") &&
                    PassSettings(restored, 0)
                                    .value(QStringLiteral("steerDeltaMax"))
                                    .toString() == QStringLiteral("0.25"),
            "persisted modifier decimals changed representation");
    return okay;
}

bool TestLegacyMigration() {
    QSettings().clear();
    const QString retiredBudgetKey = QString::fromLatin1(
            QByteArray::fromHex("617474656d7074436f756e74"));
    QSettings().setValue(
            QStringLiteral("search/") + retiredBudgetKey,
            QStringLiteral("1000"));
    QSettings().setValue(QStringLiteral("selection/mutationAlgorithm"),
                         QStringLiteral("random-steering"));
    QSettings().setValue(QStringLiteral("search/minMutateMs"),
                         QStringLiteral("1200"));
    QSettings().setValue(QStringLiteral("search/maxMutateMs"),
                         QStringLiteral("2400"));
    QSettings().setValue(QStringLiteral("search/mutationSeed"),
                         QStringLiteral("987"));
    QSettings().setValue(QStringLiteral("selection/evaluationTarget"),
                         QStringLiteral("maximum-speed"));

    SearchController controller;
    bool okay = Check(
            !QSettings().contains(
                    QStringLiteral("search/") + retiredBudgetKey),
            "retired search budget was not removed");
    okay &= Check(controller.modifierPasses().size() == 1 &&
                              PassId(controller, 0) ==
                                      QStringLiteral("random-steering"),
                      "legacy mutation selection was not migrated");
    okay &= Check(PassSettings(controller, 0)
                                  .value(QStringLiteral("minTimeMs"))
                                  .toString() == QStringLiteral("1200") &&
                          PassSettings(controller, 0)
                                  .value(QStringLiteral("maxTimeMs"))
                                  .toString() == QStringLiteral("2400") &&
                          PassSettings(controller, 0)
                                  .value(QStringLiteral("seed"))
                                  .toString() == QStringLiteral("987"),
                  "legacy modifier settings were not migrated");
    okay &= Check(controller.evaluationTargetId() ==
                          QStringLiteral("velocity") &&
                          QSettings()
                                          .value(QStringLiteral(
                                                  "selection/evaluationTarget"))
                                          .toString() ==
                                  QStringLiteral("velocity"),
                  "legacy evaluation target was not canonicalized");
    okay &= Check(QSettings().contains(
                          QStringLiteral("composition/modifiers")),
                  "migrated modifier composition was not persisted");
    return okay;
}


bool TestIndefiniteSearchLifecycle(const QString &packsDirectory,
                                   const QString &replayPath) {
    QSettings().clear();
    SearchController controller;
    SetValidPaths(controller, packsDirectory, replayPath);
    controller.setSimulationBackendId(QStringLiteral("optimized-cpu"));
    controller.setModifierPassSetting(
            0,
            QStringLiteral("minTimeMs"),
            QStringLiteral("0"));
    controller.setModifierPassSetting(
            0,
            QStringLiteral("maxTimeMs"),
            QStringLiteral("20"));
    controller.setEvaluationTargetSetting(
            QStringLiteral("minTimeMs"),
            QStringLiteral("0"));
    controller.setEvaluationTargetSetting(
            QStringLiteral("maxTimeMs"),
            QStringLiteral("20"));
    const bool zeroOriginConfigured =
            PassSettings(controller, 0)
                            .value(QStringLiteral("minTimeMs"))
                            .toString() == QStringLiteral("0") &&
            PassSettings(controller, 0)
                            .value(QStringLiteral("maxTimeMs"))
                            .toString() == QStringLiteral("20") &&
            controller.evaluationTargetSettings()
                            .value(QStringLiteral("minTimeMs"))
                            .toString() == QStringLiteral("0") &&
            controller.evaluationTargetSettings()
                            .value(QStringLiteral("maxTimeMs"))
                            .toString() == QStringLiteral("20");
    if (!Check(zeroOriginConfigured,
               "failed to configure the zero-based first input")) {
        return false;
    }
    if (!Check(controller.canStart(),
               "real replay configuration did not enable Start")) {
        return false;
    }
    controller.extractReplayInputs();
    if (!Check(
                WaitUntil(
                        [&controller]() {
                            return !controller.extractingReplayInputs();
                        },
                        30000) &&
                        controller.replayInputStatusText() ==
                                QStringLiteral("Replay inputs extracted") &&
                        !controller.baseInputScript().isEmpty() &&
                        controller.baseInputScriptError().isEmpty(),
                "real replay inputs were not extracted into the base script")) {
        return false;
    }

    QSignalSpy completionSpy(
            &controller, &SearchController::searchCompleted);
    QSignalSpy improvementSpy(
            &controller, &SearchController::searchImprovement);
    controller.startSearch();
    bool okay = Check(controller.running() && !controller.canStart(),
                      "Start did not enter the running state");
    okay &= Check(
            WaitUntil(
                    [&controller]() {
                        return controller.running() &&
                                controller.statusText() ==
                                        QStringLiteral("Searching...") &&
                                controller.liveMetricsVisible() &&
                                !controller.iterationCountText().isEmpty() &&
                                !controller.throughputText().isEmpty() &&
                                !controller.throughputText().contains(
                                        QLatin1Char('.')) &&
                                controller.elapsedText().startsWith(
                                        QStringLiteral("00:")) &&
                                !controller.elapsedText().contains(
                                        QLatin1Char('.')) &&
                                controller.resultText().contains(
                                        QStringLiteral("Last improvement:")) &&
                                !controller.resultText()
                                         .section(QStringLiteral(
                                                          "Last improvement: "),
                                                  1,
                                                  1)
                                         .section(QLatin1Char('\n'), 0, 0)
                                         .contains(QLatin1Char('.')) &&
                                !controller.resultText().contains(
                                        QStringLiteral("iterations so far")) &&
                                !controller.bestInputsText().isEmpty();
                    },
                    30000),
            "live iteration metrics were not shown while running");
    if (!okay) {
        return false;
    }
    okay &= Check(
            WaitUntil(
                    [&improvementSpy]() {
                        return improvementSpy.count() > 0;
                    },
                    10000),
            "search did not publish a best-run improvement trajectory");
    std::uint64_t searchId = 0u;
    std::uint64_t improvementNumber = 0u;
    for (const QList<QVariant> &arguments : improvementSpy) {
        const auto improvement =
                qvariant_cast<forevertas::app::SearchImprovementPtr>(
                        arguments.at(0));
        const bool complete =
                improvement != nullptr &&
                improvement->searchId != 0u &&
                improvement->improvementNumber > improvementNumber &&
                improvement->packsDirectory == packsDirectory &&
                improvement->replayPath == replayPath &&
                improvement->simulationBackendId ==
                        QStringLiteral("optimized-cpu") &&
                !improvement->timeline.empty() &&
                improvement->timeline.front().timeMs == 0 &&
                improvement->timeline.back().timeMs > 0;
        if (improvement != nullptr) {
            if (searchId == 0u) {
                searchId = improvement->searchId;
            }
            improvementNumber = improvement->improvementNumber;
        }
        okay &= Check(complete && improvement->searchId == searchId,
                      "published improvement trajectory was incomplete");
    }
    if (!okay) {
        controller.stopSearch();
        WaitUntil([&controller]() { return !controller.running(); }, 30000);
        return false;
    }
    const QString firstElapsed = controller.elapsedText();
    okay &= Check(
            WaitUntil(
                    [&controller, &firstElapsed]() {
                        return controller.running() &&
                                controller.elapsedText() != firstElapsed;
                    },
                    5000),
            "live elapsed metric did not refresh without completion");
    if (!okay) {
        return false;
    }

    controller.stopSearch();
    okay &= Check(controller.stopping(),
                  "Stop did not enter the stopping state");
    okay &= Check(
            WaitUntil([&completionSpy]() {
                return completionSpy.count() > 0;
            }, 30000),
            "Stop did not complete final best-run sampling");
    okay &= Check(
            WaitUntil([&controller]() { return !controller.running(); }, 5000),
            "worker did not leave the running state after completion");
    if (completionSpy.isEmpty()) {
        return false;
    }

    const auto completion = qvariant_cast<
            forevertas::app::SearchCompletionPtr>(
            completionSpy.takeFirst().at(0));
    okay &= Check(completion != nullptr &&
                          completion->simulationBackendId ==
                                  QStringLiteral("optimized-cpu") &&
                          !completion->bestInputs.empty() &&
                          !completion->bestTimeline.empty(),
                  "completed search did not retain its backend and best run");
    if (completion && !completion->bestTimeline.empty()) {
        okay &= Check(completion->bestTimeline.front().timeMs == 0 &&
                              completion->bestTimeline.back().timeMs > 0,
                      "final sampling did not cover the replay timeline");
    }
    okay &= Check(!controller.stopping() &&
                          controller.statusText() ==
                                  QStringLiteral("Search complete") &&
                          controller.liveMetricsVisible() &&
                          !controller.iterationCountText().isEmpty() &&
                          !controller.throughputText().isEmpty() &&
                          !controller.elapsedText().isEmpty() &&
                          !controller.resultText().isEmpty() &&
                          !controller.bestInputsText().isEmpty(),
                  "completed search did not preserve the final best display");
    return okay;
}

bool TestAutomaticPacksDetection() {
    QSettings().clear();
    QTemporaryDir root;
    if (!root.isValid()) return Check(false, "failed to create search root");
    const QString packs = root.filePath(QStringLiteral(
            "Games/prefix/drive_c/Program Files (x86)/"
            "TmUnitedForever/Packs"));
    if (!QDir().mkpath(packs)) {
        return Check(false, "failed to create detected Packs directory");
    }
    QFile packList(QDir(packs).filePath(QStringLiteral("packlist.dat")));
    if (!packList.open(QIODevice::WriteOnly)) {
        return Check(false, "failed to create packlist.dat");
    }
    packList.write("test");
    packList.close();
    const QString pattern = root.filePath(QStringLiteral(
            "Games/*/drive_c/Program Files (x86)/TmUnitedForever/Packs"));
    const QString canonical = QFileInfo(packs).canonicalFilePath();

    SearchController controller(QStringList{pattern});
    QSignalSpy spy(&controller,
                   &SearchController::autoDetectedPacksDirectoryChanged);
    QThread *publicationThread = nullptr;
    QObject::connect(
            &controller,
            &SearchController::autoDetectedPacksDirectoryChanged,
            &controller,
            [&]() { publicationThread = QThread::currentThread(); });
    bool okay = Check(controller.autoDetectedPacksDirectory().isEmpty(),
                      "automatic detection blocked construction");
    okay &= Check(spy.wait(2000),
                  "automatic detection did not publish asynchronously");
    okay &= Check(controller.autoDetectedPacksDirectory() == canonical,
                  "automatic detection proposed the wrong path");
    okay &= Check(publicationThread == controller.thread(),
                  "automatic detection published off the controller thread");
    controller.applyAutoDetectedPacksDirectory();
    okay &= Check(controller.packsDirectory() == canonical &&
                          controller.autoDetectedPacksDirectory().isEmpty(),
                  "Apply did not activate and hide the detected path");
    return okay;
}

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForeverTASTests"));
    QCoreApplication::setApplicationName(
            QStringLiteral("SearchControllerTests"));
    QStandardPaths::setTestModeEnabled(true);

    QTemporaryDir packsDirectory;
    if (!packsDirectory.isValid()) {
        std::cerr << "failed to create temporary Packs directory\n";
        return 1;
    }
    const QString replayPath =
            packsDirectory.filePath(QStringLiteral("run.Replay.Gbx"));
    QFile replay(replayPath);
    if (!replay.open(QIODevice::WriteOnly)) {
        std::cerr << "failed to create temporary replay file\n";
        return 1;
    }
    replay.write("test");
    replay.close();

    bool okay = TestAutomaticPacksDetection() &&
            TestDescriptiveSearchStageStatuses() &&
            TestIterationBoundaryArbitration() &&
            TestUserTimelineConfigurationBoundary() &&
            TestRegistryAndValidation(packsDirectory.path(), replayPath) &&
            TestCompositionEditing(packsDirectory.path(), replayPath) &&
            TestPersistence(packsDirectory.path(), replayPath) &&
            TestStopAbortsBeforeFirstIteration(
                    packsDirectory.path(), replayPath) &&
            TestExtractionFailurePreservesDraft(
                    packsDirectory.path(), replayPath) &&
            TestExtractionWorkerShutdown(
                    packsDirectory.path(), replayPath) &&
            TestLocaleIndependentPersistedDecimals(
                    packsDirectory.path(), replayPath) &&
            TestLegacyMigration();
    if (okay && argc == 4 &&
        QString::fromLocal8Bit(argv[1]) == QStringLiteral("--lifecycle")) {
        okay = TestIndefiniteSearchLifecycle(
                QString::fromLocal8Bit(argv[2]),
                QString::fromLocal8Bit(argv[3]));
    }
    QSettings().clear();
    return okay ? 0 : 1;
}
