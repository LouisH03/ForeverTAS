#include "searches/basic_brute_force_search.h"

#include "evaluators/evaluator_utils.h"
#include "searches/cuda_batch_calibrator.h"
#include "searches/option_settings_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace forevertas {
namespace {

using forevervalidator::DiscriminatedResult;
using forevervalidator::Vector3;
using forevervalidator::experimental::PhysicsSandbox;
using forevervalidator::experimental::PhysicsSandboxCarState;
using forevervalidator::experimental::PhysicsSandboxInputEvent;
using forevervalidator::experimental::PhysicsSandboxState;
using forevervalidator::experimental::PhysicsSandboxStateView;

template<typename T, typename Error>
T Require(DiscriminatedResult<T, Error> result, const char *operation) {
    if (!result) {
        std::string message = std::string(operation) + " failed";
        if (!result.Error().diagnostic.empty()) {
            message += ": " + result.Error().diagnostic;
        }
        throw std::runtime_error(std::move(message));
    }
    return std::move(result).Value();
}

bool SameVector(const Vector3 &left, const Vector3 &right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool SameCar(const PhysicsSandboxCarState &left,
             const PhysicsSandboxCarState &right) {
    return left.rotationX == right.rotationX &&
           left.rotationY == right.rotationY &&
           left.rotationZ == right.rotationZ &&
           left.rotationW == right.rotationW &&
           SameVector(left.position, right.position) &&
           SameVector(left.linearSpeed, right.linearSpeed) &&
           SameVector(left.angularSpeed, right.angularSpeed) &&
           SameVector(left.force, right.force) &&
           SameVector(left.torque, right.torque);
}

bool SameState(const PhysicsSandboxStateView &left,
               const PhysicsSandboxStateView &right) {
    return left.tick == right.tick && left.timeMs == right.timeMs &&
           left.mapEnvironment == right.mapEnvironment &&
           left.vehicleModel == right.vehicleModel &&
           left.playMode == right.playMode && SameCar(left.car, right.car) &&
           left.accelerate == right.accelerate &&
           left.brake == right.brake && left.steering == right.steering &&
           left.checkpointsCollected == right.checkpointsCollected &&
           left.checkpointsTotal == right.checkpointsTotal &&
           left.completedLaps == right.completedLaps &&
           left.totalLaps == right.totalLaps &&
           left.raceCompleted == right.raceCompleted &&
           left.finishTimeMs == right.finishTimeMs &&
           left.finishTime == right.finishTime &&
           left.respawnCount == right.respawnCount &&
           left.stuntsScore == right.stuntsScore;
}

void CheckCancellation(const SearchRunControl *control) {
    if (control != nullptr && control->cancellationRequested &&
        control->cancellationRequested()) {
        throw SearchCancelled();
    }
}

void BeginIteration(const SearchRunControl *control) {
    CheckCancellation(control);
    if (control != nullptr && control->beginIteration &&
        !control->beginIteration()) {
        throw SearchCancelled();
    }
}

bool StopRequested(const SearchRunControl *control) {
    return control != nullptr && control->stopRequested &&
            control->stopRequested();
}

bool IterationLimitReached(const SearchRunControl *control,
                           std::uint64_t iterations) {
    return control != nullptr && control->iterationLimit &&
            iterations >= *control->iterationLimit;
}

void ReportProgress(const SearchRunControl *control,
                    SearchProgressStage stage,
                    std::uint64_t completedWork,
                    std::uint64_t totalWork = 0u) {
    if (control != nullptr && control->progressChanged) {
        control->progressChanged({stage, completedWork, totalWork});
    }
}

#if FOREVERVALIDATOR_HAS_CUDA
void ReportCudaBatchSize(const SearchRunControl *control,
                         std::uint32_t batchSize) {
    if (control != nullptr && control->cudaBatchSizeChanged) {
        control->cudaBatchSizeChanged(batchSize);
    }
}

bool CudaBatchProfilingEnabled() {
    static const bool enabled = []() {
#if defined(_WIN32)
        char *value = nullptr;
        std::size_t valueSize = 0u;
        if (_dupenv_s(&value, &valueSize,
                      "FOREVERTAS_CUDA_PROFILE") != 0) {
            return false;
        }
        const bool result = value != nullptr && value[0] != '\0' &&
                !(value[0] == '0' && value[1] == '\0');
        std::free(value);
        return result;
#else
        const char *value = std::getenv("FOREVERTAS_CUDA_PROFILE");
        return value != nullptr && value[0] != '\0' &&
                !(value[0] == '0' && value[1] == '\0');
#endif
    }();
    return enabled;
}

void ReportCudaBatchProfile(
        const char *phase,
        const forevervalidator::experimental::
                PhysicsSandboxCudaSearchBatch &batch,
        std::uint64_t timelineTickCount,
        std::chrono::steady_clock::duration wallElapsed) {
    if (!CudaBatchProfilingEnabled()) {
        return;
    }
    const double wallMilliseconds =
            std::chrono::duration<double, std::milli>(
                    wallElapsed)
                    .count();
    const double attemptsPerSecond =
            wallMilliseconds > 0.0
            ? static_cast<double>(batch.candidateCount) *
                      1000.0 / wallMilliseconds
            : 0.0;
    const double simulatedTicks =
            static_cast<double>(batch.evaluatedCandidateCount) *
            static_cast<double>(timelineTickCount);
    const double physicsTicksPerSecond =
            batch.metrics.simulationKernelMilliseconds > 0.0
            ? simulatedTicks * 1000.0 /
                      batch.metrics.simulationKernelMilliseconds
            : 0.0;
    std::clog << "forevertas_cuda_batch"
              << " phase=" << phase
              << " first_candidate=" << batch.firstCandidateId
              << " candidates=" << batch.candidateCount
              << " active=" << batch.evaluatedCandidateCount
              << " timeline_ticks=" << timelineTickCount
              << " attempts_per_second=" << attemptsPerSecond
              << " physics_ticks_per_second="
              << physicsTicksPerSecond
              << " wall_ms=" << wallMilliseconds
              << " kernel_ms=" << batch.metrics.kernelMilliseconds
              << " mutation_ms="
              << batch.metrics.mutationKernelMilliseconds
              << " simulation_ms="
              << batch.metrics.simulationKernelMilliseconds
              << " finish_refinement_ms="
              << batch.metrics.finishRefinementKernelMilliseconds
              << " winner_capture_ms="
              << batch.metrics.winnerStateCaptureKernelMilliseconds
              << " finalization_ms="
              << batch.metrics.finalizationKernelMilliseconds
              << " best_changed=" << batch.bestChanged
              << " resident_mib="
              << static_cast<double>(
                         batch.metrics.residentDeviceBytes) /
                         (1024.0 * 1024.0)
              << '\n'
              << std::flush;
}
#endif

std::uint64_t TickCount(std::uint64_t durationMs,
                        std::uint32_t tickDurationMs) {
    if (durationMs % tickDurationMs != 0u) {
        throw std::runtime_error(
                "sandbox state time is not aligned to the tick duration");
    }
    return durationMs / tickDurationMs;
}

PhysicsSandboxStateView AdvanceTo(PhysicsSandbox &sandbox,
                                  std::uint64_t currentTimeMs,
                                  std::uint64_t targetTimeMs,
                                  std::uint32_t tickDurationMs,
                                  const SearchRunControl *control) {
    if (currentTimeMs > targetTimeMs) {
        throw std::runtime_error("sandbox is already past the requested time");
    }
    if (currentTimeMs == targetTimeMs) {
        return Require(sandbox.ReadState(), "reading sandbox state");
    }

    constexpr std::uint32_t maxTicksPerAdvance = 128u;
    std::uint64_t ticksRemaining =
            TickCount(targetTimeMs - currentTimeMs, tickDurationMs);
    PhysicsSandboxStateView state;
    while (ticksRemaining != 0u) {
        CheckCancellation(control);
        const std::uint32_t ticks =
                static_cast<std::uint32_t>(
                        std::min<std::uint64_t>(ticksRemaining,
                                                maxTicksPerAdvance));
        state = Require(sandbox.AdvanceTicks(ticks), "advancing sandbox");
        ticksRemaining -= ticks;
    }
    CheckCancellation(control);
    return state;
}

struct BestIteration {
    std::optional<EvaluationSample> evaluation;
    SearchWinnerSource source = SearchWinnerSource::Baseline;
    std::optional<std::uint64_t> iterationIndex;
    std::size_t mutationCount = 0u;
    PhysicsSandboxStateView view;
    std::optional<PhysicsSandboxState> snapshot;
    std::vector<PhysicsSandboxInputEvent> inputs;
};

void ReportLive(
        const SearchRunControl *control,
        const BestIteration &best,
        std::uint64_t iterations,
        std::uint64_t evaluatorCalls,
        std::uint64_t mutationImprovementCount,
        std::uint64_t totalMutationCount,
        std::chrono::steady_clock::duration elapsed,
        const std::optional<std::chrono::steady_clock::duration>
                &lastImprovementElapsed) {
    if (control == nullptr || !control->liveChanged || !best.evaluation) {
        return;
    }
    control->liveChanged({
            best.source,
            best.iterationIndex,
            best.mutationCount,
            best.evaluation->score,
            best.evaluation->timeMs,
            best.evaluation->description,
            best.view,
            best.inputs,
            iterations,
            evaluatorCalls,
            mutationImprovementCount,
            totalMutationCount,
            elapsed,
            lastImprovementElapsed,
            {}});
}

#if FOREVERVALIDATOR_HAS_CUDA
std::string CudaEvaluationDescription(
        const forevervalidator::experimental::
                PhysicsSandboxCudaEvaluator &evaluator,
        const forevervalidator::experimental::
                PhysicsSandboxCudaSearchBatch &batch) {
    using namespace forevervalidator::experimental;
    return std::visit(
            [&](const auto &configured) -> std::string {
                using T = std::decay_t<decltype(configured)>;
                if constexpr (std::is_same_v<
                                      T,
                                      PhysicsSandboxCudaVelocityEvaluator>) {
                    return MetricDescription(
                            configured.projected
                                    ? "Projected velocity"
                                    : "Velocity",
                            batch.bestScore,
                            "m/s",
                            batch.bestTimeMs);
                } else if constexpr (std::is_same_v<
                                             T,
                                             PhysicsSandboxCudaPointEvaluator>) {
                    return MetricDescription(
                            "Point distance",
                            batch.bestScore,
                            "m",
                            batch.bestTimeMs);
                } else if constexpr (std::is_same_v<
                                             T,
                                             PhysicsSandboxCudaPoseEvaluator>) {
                    constexpr double radiansToDegrees =
                            180.0 / 3.14159265358979323846;
                    std::ostringstream description;
                    description.precision(8);
                    description
                            << "Pose error: " << batch.bestScore
                            << " (position " << batch.bestDetail0
                            << " m, rotation "
                            << batch.bestDetail1 * radiansToDegrees
                            << " deg) at "
                            << FormatHumanDurationMilliseconds(
                                       batch.bestTimeMs);
                    return description.str();
                } else if constexpr (std::is_same_v<
                                             T,
                                             PhysicsSandboxCudaVolumeEntryEvaluator>) {
                    return TimeMetricDescription(
                            "Volume entry time", batch.bestTimeMs);
                } else if constexpr (std::is_same_v<
                                             T,
                                             PhysicsSandboxCudaStuntPointsEvaluator>) {
                    return "Stunt points: " +
                            std::to_string(
                                    static_cast<std::uint32_t>(
                                            batch.bestScore)) +
                            " at " +
                            FormatHumanDurationMilliseconds(
                                    batch.bestTimeMs);
                } else {
                    return "Precise finish time: " +
                            FormatHumanDurationNanoseconds(
                                    static_cast<std::uint64_t>(
                                            batch.bestScore));
                }
            },
            evaluator);
}

SearchResult RunCudaBasicBruteForce(
        const SearchExecutionContext &context,
        const EvaluationPlan &evaluationPlan,
        std::int64_t earliestMutationTimeMs,
        const PhysicsSandboxState &branch,
        const std::vector<PhysicsSandboxInputEvent>
                &originalBaselineInputs,
        bool autoPromoteBest,
        std::chrono::steady_clock::time_point started) {
    using namespace forevervalidator::experimental;
    if (context.cudaModifiers == nullptr ||
        context.cudaEvaluator == nullptr ||
        (!context.calibrateCudaBatchSize &&
         context.cudaBatchSize == 0u)) {
        throw std::invalid_argument(
                "CUDA search configuration is unavailable");
    }

    constexpr std::uint32_t calibrationInitialBatchSize = 1u;
    const std::uint32_t initialBatchSize =
            context.calibrateCudaBatchSize
            ? calibrationInitialBatchSize
            : context.cudaBatchSize;
    PhysicsSandboxCudaSearchConfiguration configuration;
    configuration.maximumBatchSize = initialBatchSize;
    configuration.earliestMutationTimeMs = earliestMutationTimeMs;
    configuration.evaluationStartTimeMs = evaluationPlan.startTimeMs;
    configuration.evaluationEndTimeMs = evaluationPlan.endTimeMs;
    configuration.modifiers = *context.cudaModifiers;
    configuration.evaluator = *context.cudaEvaluator;
    std::optional<PhysicsSandboxCudaSearchSession> session;
    session.emplace(Require(
            CreatePhysicsSandboxCudaSearchSession(
                    context.sandbox, configuration),
            "creating resident CUDA search session"));
    std::optional<CudaBatchCalibrator> calibrator;
    if (context.calibrateCudaBatchSize) {
        calibrator.emplace();
    }
    std::uint32_t sessionCapacity = initialBatchSize;
    const std::uint64_t timelineTickCount =
            static_cast<std::uint64_t>(
                    (evaluationPlan.endTimeMs -
                     earliestMutationTimeMs) /
                    context.tickDurationMs) +
            1u;

    BestIteration best;
    std::uint64_t iterations = 0u;
    std::uint64_t evaluatorCalls = 0u;
    std::uint64_t mutationImprovementCount = 0u;
    std::uint64_t totalMutationCount = 0u;
    std::optional<std::chrono::steady_clock::duration>
            lastImprovementElapsed;
    auto lastLiveReport = started - std::chrono::milliseconds(100);
    const auto reportLive = [&](bool force) {
        const auto now = std::chrono::steady_clock::now();
        if (!force &&
            now - lastLiveReport < std::chrono::milliseconds(100)) {
            return;
        }
        ReportLive(context.control,
                   best,
                   iterations,
                   evaluatorCalls,
                   mutationImprovementCount,
                   totalMutationCount,
                   now - started,
                   lastImprovementElapsed);
        lastLiveReport = now;
    };
    const auto adoptBest =
            [&](PhysicsSandboxCudaSearchBatch &batch) {
                if (!batch.bestSnapshot) {
                    return;
                }
                best.source = batch.bestIsMutation
                        ? SearchWinnerSource::Mutation
                        : SearchWinnerSource::Baseline;
                best.iterationIndex = batch.bestCandidateId;
                best.mutationCount = batch.bestMutationCount;
                best.evaluation = EvaluationSample{
                        batch.bestScore,
                        batch.bestTimeMs,
                        CudaEvaluationDescription(
                                *context.cudaEvaluator, batch)};
                best.view = batch.bestState;
                best.snapshot = std::move(*batch.bestSnapshot);
                best.inputs = std::move(batch.bestInputs);
            };

    CheckCancellation(context.control);
    ReportProgress(context.control, SearchProgressStage::Baseline, 0u);
    const auto baselineStarted = std::chrono::steady_clock::now();
    PhysicsSandboxCudaSearchBatch baseline = Require(
            session->EvaluateBaseline(
                    [control = context.control]() {
                        return control != nullptr &&
                                control->cancellationRequested &&
                                control->cancellationRequested();
                    }),
            "evaluating CUDA baseline");
    ReportCudaBatchProfile(
            "baseline",
            baseline,
            timelineTickCount,
            std::chrono::steady_clock::now() - baselineStarted);
    if (baseline.cancelled) {
        throw SearchCancelled();
    }
    evaluatorCalls += baseline.evaluatorCalls;
    adoptBest(baseline);
    reportLive(true);
    if (calibrator) {
        ReportCudaBatchSize(
                context.control, calibrator->CurrentBatchSize());
        ReportProgress(
                context.control, SearchProgressStage::Calibration, 0u);
    } else {
        ReportProgress(
                context.control, SearchProgressStage::Mutations, 0u);
    }

    std::uint64_t iterationIndex = 0u;
    while (!StopRequested(context.control) &&
           !IterationLimitReached(context.control, iterations)) {
        CheckCancellation(context.control);
        std::uint32_t batchSize = calibrator
                ? calibrator->CurrentBatchSize()
                : context.cudaBatchSize;
        if (batchSize > sessionCapacity) {
            PhysicsSandboxResult<std::uint32_t> reserved =
                    session->ReserveBatchCapacity(batchSize);
            if (!reserved) {
                if (!calibrator ||
                    reserved.Error().code !=
                            PhysicsSandboxErrorCode::AllocationFailed) {
                    std::string message =
                            "reserving calibrated CUDA batch capacity failed";
                    if (!reserved.Error().diagnostic.empty()) {
                        message += ": " + reserved.Error().diagnostic;
                    }
                    throw std::runtime_error(std::move(message));
                }
                calibrator->CapacityUnavailable();
                ReportCudaBatchSize(
                        context.control,
                        calibrator->CurrentBatchSize());
                if (calibrator->Complete()) {
                    ReportProgress(
                            context.control,
                            SearchProgressStage::Mutations,
                            iterations);
                }
                continue;
            }
            sessionCapacity = reserved.Value();
        }
        if (context.control != nullptr &&
            context.control->iterationLimit) {
            batchSize = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(
                            batchSize,
                            *context.control->iterationLimit -
                                    iterations));
        }
        const bool exhaustsSequence =
                batchSize != 0u &&
                iterationIndex >
                        std::numeric_limits<std::uint64_t>::max() -
                                (batchSize - 1u);
        if (exhaustsSequence) {
            batchSize = static_cast<std::uint32_t>(
                    std::numeric_limits<std::uint64_t>::max() -
                    iterationIndex + 1u);
        }
        const auto batchStarted = std::chrono::steady_clock::now();
        BeginIteration(context.control);
        PhysicsSandboxCudaSearchBatch batch = Require(
                session->RunBatch(
                        iterationIndex,
                        batchSize,
                        [control = context.control]() {
                            return control != nullptr &&
                                    control->cancellationRequested &&
                                    control->cancellationRequested();
                        }),
                "executing CUDA search batch");
        const auto batchElapsed =
                std::chrono::steady_clock::now() - batchStarted;
        ReportCudaBatchProfile(
                "mutations", batch, timelineTickCount, batchElapsed);
        if (batch.cancelled) {
            throw SearchCancelled();
        }
        iterations += batch.candidateCount;
        evaluatorCalls += batch.evaluatorCalls;
        totalMutationCount += batch.totalMutationCount;
        mutationImprovementCount +=
                batch.mutationImprovementCount;
        const bool promote =
                autoPromoteBest &&
                batch.mutationImprovementCount != 0u &&
                batch.bestSnapshot.has_value();
        if (batch.bestChanged && batch.bestSnapshot) {
            adoptBest(batch);
            if (autoPromoteBest) {
                best.mutationCount = EffectiveInputChangeCount(
                        originalBaselineInputs, best.inputs);
            }
        }
        if (promote) {
            session.reset();
            Require(context.sandbox.RestoreState(branch),
                    "restoring CUDA branch for promoted baseline");
            Require(context.sandbox.ReplaceInputs(best.inputs),
                    "promoting CUDA best inputs to baseline");
            configuration.maximumBatchSize = sessionCapacity;
            session.emplace(Require(
                    CreatePhysicsSandboxCudaSearchSession(
                            context.sandbox, configuration),
                    "recreating promoted CUDA search session"));
            PhysicsSandboxCudaSearchBatch promotedBaseline = Require(
                    session->EvaluateBaseline(
                            [control = context.control]() {
                                return control != nullptr &&
                                        control->cancellationRequested &&
                                        control->cancellationRequested();
                            }),
                    "evaluating promoted CUDA baseline");
            if (promotedBaseline.cancelled) {
                throw SearchCancelled();
            }
            evaluatorCalls += promotedBaseline.evaluatorCalls;
            if (!promotedBaseline.bestSnapshot ||
                !best.evaluation ||
                promotedBaseline.bestScore != best.evaluation->score ||
                promotedBaseline.bestTimeMs != best.evaluation->timeMs) {
                throw std::runtime_error(
                        "promoted CUDA baseline does not match the "
                        "global best");
            }
        }
        if (batch.mutationImprovementCount != 0u) {
            lastImprovementElapsed =
                    std::chrono::steady_clock::now() - started;
            reportLive(true);
        } else {
            reportLive(false);
        }
        if (calibrator && !calibrator->Complete()) {
            calibrator->Observe(batch.candidateCount, batchElapsed);
            ReportCudaBatchSize(
                    context.control, calibrator->CurrentBatchSize());
            if (calibrator->Complete()) {
                ReportProgress(
                        context.control,
                        SearchProgressStage::Mutations,
                        iterations);
            }
        }
        if (exhaustsSequence) {
            throw std::overflow_error("iteration sequence exhausted");
        }
        iterationIndex += batchSize;
    }

    CheckCancellation(context.control);
    reportLive(true);
    if (!best.evaluation || !best.snapshot) {
        throw std::runtime_error(
                "no iteration satisfied the selected evaluation target");
    }
    const PhysicsSandboxStateView restored = Require(
            context.sandbox.RestoreState(*best.snapshot),
            "restoring global CUDA best state");
    if (!SameState(restored, best.view)) {
        throw std::runtime_error(
                "restored CUDA global best does not match its captured state");
    }
    const bool mutationWon =
            best.source == SearchWinnerSource::Mutation;
    if (mutationWon != (mutationImprovementCount > 0u)) {
        throw std::runtime_error(
                "CUDA mutation winner and improvement count are inconsistent");
    }
    return SearchResult{
            best.source,
            best.iterationIndex,
            best.mutationCount,
            best.evaluation->score,
            best.evaluation->timeMs,
            best.evaluation->description,
            best.view,
            std::move(best.inputs),
            {},
            iterations,
            evaluatorCalls,
            mutationImprovementCount,
            totalMutationCount,
            std::chrono::steady_clock::now() - started,
            lastImprovementElapsed,
            *best.snapshot};
}
#endif

}  // namespace

OptionSettings DefaultBasicBruteForceOptionSettings() {
    return {{"autoPromoteBest", "false"}};
}

std::optional<std::string> ValidateBasicBruteForceOptionSettings(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs) {
    if (const auto keyError = ValidateOptionSettingKeys(
                settings, DefaultBasicBruteForceOptionSettings())) {
        return keyError;
    }
    if (!ParseBoolean(settings.at("autoPromoteBest"))) {
        return "auto-promote best must be true or false";
    }
    if (tickDurationMs == 0u) {
        return "tick duration must be greater than zero";
    }
    return std::nullopt;
}

std::unique_ptr<SearchAlgorithm> CreateBasicBruteForceSearch(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs) {
    if (const auto error =
                ValidateBasicBruteForceOptionSettings(settings, tickDurationMs)) {
        throw std::invalid_argument(*error);
    }
    return std::make_unique<BasicBruteForceSearch>(
            *ParseBoolean(settings.at("autoPromoteBest")));
}

BasicBruteForceSearch::BasicBruteForceSearch(bool autoPromoteBest)
    : autoPromoteBest_(autoPromoteBest) {}

SearchResult BasicBruteForceSearch::Run(
        const SearchExecutionContext &context) const {
    const auto started = std::chrono::steady_clock::now();
    if (context.tickDurationMs == 0u) {
        throw std::invalid_argument(
                "tick duration must be greater than zero");
    }

    const std::int64_t earliestMutationTimeMs =
            context.mutator.EarliestMutationTimeMs();
    if (earliestMutationTimeMs <
                static_cast<std::int64_t>(context.tickDurationMs) ||
        earliestMutationTimeMs % context.tickDurationMs != 0) {
        throw std::invalid_argument(
                "modifier pipeline must begin on or after the first whole "
                "tick");
    }

    CheckCancellation(context.control);
    PhysicsSandboxStateView current = Require(
            context.sandbox.ReadState(), "reading initial sandbox state");
    EvaluationPlan evaluationPlan = context.evaluator.Plan(
            static_cast<std::int64_t>(current.durationMs),
            earliestMutationTimeMs,
            context.tickDurationMs);
    if (context.control != nullptr &&
        context.control->evaluationEndTimeLimitMs) {
        evaluationPlan.endTimeMs = std::min(
                evaluationPlan.endTimeMs,
                *context.control->evaluationEndTimeLimitMs);
    }
    if (evaluationPlan.startTimeMs < earliestMutationTimeMs ||
        evaluationPlan.endTimeMs < evaluationPlan.startTimeMs ||
        evaluationPlan.endTimeMs >
                static_cast<std::int64_t>(current.durationMs) ||
        evaluationPlan.startTimeMs % context.tickDurationMs != 0 ||
        evaluationPlan.endTimeMs % context.tickDurationMs != 0) {
        throw std::invalid_argument(
                "evaluation target returned an invalid observation plan: "
                "mutation=" +
                std::to_string(earliestMutationTimeMs) +
                " start=" +
                std::to_string(evaluationPlan.startTimeMs) +
                " end=" +
                std::to_string(evaluationPlan.endTimeMs) +
                " duration=" +
                std::to_string(current.durationMs));
    }

    const std::uint64_t branchTimeMs =
            static_cast<std::uint64_t>(earliestMutationTimeMs) -
            context.tickDurationMs;
    current = AdvanceTo(context.sandbox,
                        current.timeMs,
                        branchTimeMs,
                        context.tickDurationMs,
                        context.control);

    const std::vector<PhysicsSandboxInputEvent> baselineInputs = Require(
            context.sandbox.ReadInputs(), "reading baseline inputs");
    const PhysicsSandboxState branch = Require(
            context.sandbox.CaptureState(), "capturing branch state");

#if FOREVERVALIDATOR_HAS_CUDA
    if (context.sandbox.Backend() ==
                forevervalidator::SimulationBackend::Cuda &&
        context.cudaEvaluator != nullptr) {
        return RunCudaBasicBruteForce(
                context,
                evaluationPlan,
                earliestMutationTimeMs,
                branch,
                baselineInputs,
                autoPromoteBest_,
                started);
    }
#endif

    const std::uint64_t preEvaluationTimeMs =
            static_cast<std::uint64_t>(evaluationPlan.startTimeMs) -
            context.tickDurationMs;
    const std::uint64_t evaluationTicks =
            static_cast<std::uint64_t>(
                    (evaluationPlan.endTimeMs - evaluationPlan.startTimeMs) /
                    context.tickDurationMs) +
            1u;

    BestIteration best;
    std::uint64_t iterations = 0u;
    std::uint64_t evaluatorCalls = 0u;
    std::uint64_t mutationImprovementCount = 0u;
    std::uint64_t totalMutationCount = 0u;
    std::optional<std::chrono::steady_clock::duration>
            lastImprovementElapsed;
    auto lastLiveReport = started - std::chrono::milliseconds(100);
    const auto reportLive = [&](bool force) {
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - lastLiveReport < std::chrono::milliseconds(100)) {
            return;
        }
        ReportLive(context.control,
                   best,
                   iterations,
                   evaluatorCalls,
                   mutationImprovementCount,
                   totalMutationCount,
                   now - started,
                   lastImprovementElapsed);
        lastLiveReport = now;
    };

    const auto evaluateTimeline = [&](SearchWinnerSource source,
                                      std::optional<std::uint64_t> iterationIndex,
                                      std::size_t mutationCount) {
        bool improved = false;
        PhysicsSandboxStateView state = AdvanceTo(
                context.sandbox,
                branchTimeMs,
                preEvaluationTimeMs,
                context.tickDurationMs,
                context.control);
        std::optional<PhysicsSandboxStateView> previous = state;
        std::unique_ptr<IterationEvaluationSession> session =
                context.evaluator.CreateSession();
        for (std::uint64_t tick = 0u; tick < evaluationTicks; ++tick) {
            CheckCancellation(context.control);
            state = Require(context.sandbox.AdvanceTicks(1u),
                            "advancing evaluation tick");
            const std::optional<EvaluationSample> sample =
                    session->Observe(previous, state);
            previous = state;
            ++evaluatorCalls;
            if (!sample) {
                continue;
            }
            if (!std::isfinite(sample->score) ||
                !std::isfinite(sample->timeMs)) {
                throw std::runtime_error(
                        "iteration evaluator returned a non-finite result");
            }
            if (best.evaluation &&
                !context.evaluator.IsBetter(*sample, *best.evaluation)) {
                continue;
            }

            best.evaluation = sample;
            best.source = source;
            best.iterationIndex = iterationIndex;
            best.mutationCount = mutationCount;
            best.view = state;
            best.snapshot = Require(context.sandbox.CaptureState(),
                                    "capturing improved state");
            best.inputs = Require(context.sandbox.ReadInputs(),
                                  "reading improved inputs");
            improved = true;
            if (source == SearchWinnerSource::Mutation) {
                ++mutationImprovementCount;
                lastImprovementElapsed =
                        std::chrono::steady_clock::now() - started;
                reportLive(true);
            } else {
                reportLive(false);
            }
        }
        return improved;
    };

    ReportProgress(context.control, SearchProgressStage::Baseline, 0u);
    evaluateTimeline(SearchWinnerSource::Baseline, std::nullopt, 0u);
    reportLive(true);
    ReportProgress(context.control, SearchProgressStage::Mutations, 0u);

    if (context.control != nullptr &&
        context.control->iterationIndexStride == 0u) {
        throw std::invalid_argument(
                "iteration index stride must be greater than zero");
    }
    std::uint64_t iterationIndex = context.control == nullptr
            ? 0u
            : context.control->iterationIndexOffset;
    const std::uint64_t iterationIndexStride = context.control == nullptr
            ? 1u
            : context.control->iterationIndexStride;
    std::vector<PhysicsSandboxInputEvent> mutationBaselineInputs =
            baselineInputs;
    while (!StopRequested(context.control) &&
           !IterationLimitReached(context.control, iterations)) {
        BeginIteration(context.control);
        Require(context.sandbox.RestoreState(branch),
                "restoring branch state");
        MutationResult mutation = context.mutator.Mutate(
                {mutationBaselineInputs,
                 iterationIndex,
                 0u,
                 context.tickDurationMs,
                 earliestMutationTimeMs});
        CheckCancellation(context.control);
        ++iterations;
        bool improved = false;
        if (mutation.mutationCount != 0u) {
            totalMutationCount += mutation.mutationCount;
            const std::size_t overallMutationCount =
                    EffectiveInputChangeCount(
                            baselineInputs, mutation.inputs);
            Require(context.sandbox.ReplaceInputs(std::move(mutation.inputs)),
                    "replacing iteration inputs");
            improved = evaluateTimeline(
                    SearchWinnerSource::Mutation,
                    iterationIndex,
                    overallMutationCount);
        }
        if (autoPromoteBest_) {
            std::optional<std::vector<PhysicsSandboxInputEvent>>
                    sharedBaseline;
            if (context.control != nullptr &&
                context.control->promotedBaselineInputs) {
                sharedBaseline =
                        context.control->promotedBaselineInputs();
            }
            if (sharedBaseline) {
                mutationBaselineInputs =
                        std::move(*sharedBaseline);
            } else if (improved) {
                mutationBaselineInputs = best.inputs;
            }
        }
        reportLive(false);
        if (iterationIndex >
            std::numeric_limits<std::uint64_t>::max() -
                    iterationIndexStride) {
            throw std::overflow_error("iteration sequence exhausted");
        }
        iterationIndex += iterationIndexStride;
    }

    CheckCancellation(context.control);
    reportLive(true);
    if (!best.evaluation || !best.snapshot) {
        throw std::runtime_error(
                "no iteration satisfied the selected evaluation target");
    }
    const PhysicsSandboxStateView restored = Require(
            context.sandbox.RestoreState(*best.snapshot),
            "restoring global best state");
    if (!SameState(restored, best.view)) {
        throw std::runtime_error(
                "restored global best does not match its captured state");
    }
    const bool mutationWon = best.source == SearchWinnerSource::Mutation;
    if (mutationWon != (mutationImprovementCount > 0u)) {
        throw std::runtime_error(
                "mutation winner and improvement count are inconsistent");
    }

    return SearchResult{
            best.source,
            best.iterationIndex,
            best.mutationCount,
            best.evaluation->score,
            best.evaluation->timeMs,
            best.evaluation->description,
            best.view,
            std::move(best.inputs),
            {},
            iterations,
            evaluatorCalls,
            mutationImprovementCount,
            totalMutationCount,
            std::chrono::steady_clock::now() - started,
            lastImprovementElapsed,
            *best.snapshot};
}

}  // namespace forevertas
