#include "searches/basic_brute_force_search.h"

#include "evaluators/evaluator_utils.h"
#include "searches/cuda_batch_calibrator.h"
#include "searches/option_settings_utils.h"

#include <algorithm>
#include <cmath>
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
           left.respawnCount == right.respawnCount &&
           left.stuntsScore == right.stuntsScore;
}

void CheckCancellation(const SearchRunControl *control) {
    if (control != nullptr && control->cancellationRequested &&
        control->cancellationRequested()) {
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
            lastImprovementElapsed});
}

#if FOREVERVALIDATOR_HAS_CUDA
std::string CudaEvaluationDescription(
        const forevervalidator::experimental::
                PhysicsSandboxCudaEvaluator &evaluator,
        const forevervalidator::experimental::
                PhysicsSandboxCudaSearchBatch &batch) {
    using namespace forevervalidator::experimental;
    return std::visit(
            [&](const auto &configured) {
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
                } else {
                    return TimeMetricDescription(
                            "Finish time", batch.bestTimeMs);
                }
            },
            evaluator);
}

SearchResult RunCudaBasicBruteForce(
        const SearchExecutionContext &context,
        const EvaluationPlan &evaluationPlan,
        std::int64_t earliestMutationTimeMs,
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
    PhysicsSandboxCudaSearchSession session = Require(
            CreatePhysicsSandboxCudaSearchSession(
                    context.sandbox, configuration),
            "creating resident CUDA search session");
    std::optional<CudaBatchCalibrator> calibrator;
    if (context.calibrateCudaBatchSize) {
        calibrator.emplace();
    }
    std::uint32_t sessionCapacity = initialBatchSize;

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
    PhysicsSandboxCudaSearchBatch baseline = Require(
            session.EvaluateBaseline(
                    [control = context.control]() {
                        return control != nullptr &&
                                control->cancellationRequested &&
                                control->cancellationRequested();
                    }),
            "evaluating CUDA baseline");
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
                    session.ReserveBatchCapacity(batchSize);
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
        PhysicsSandboxCudaSearchBatch batch = Require(
                session.RunBatch(
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
        if (batch.cancelled) {
            throw SearchCancelled();
        }
        iterations += batch.candidateCount;
        evaluatorCalls += batch.evaluatorCalls;
        totalMutationCount += batch.totalMutationCount;
        mutationImprovementCount +=
                batch.mutationImprovementCount;
        if (batch.bestSnapshot) {
            adoptBest(batch);
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
    return {};
}

std::optional<std::string> ValidateBasicBruteForceOptionSettings(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs) {
    if (const auto keyError = ValidateOptionSettingKeys(
                settings, DefaultBasicBruteForceOptionSettings())) {
        return keyError;
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
    return std::make_unique<BasicBruteForceSearch>();
}

SearchResult BasicBruteForceSearch::Run(
        const SearchExecutionContext &context) const {
    const auto started = std::chrono::steady_clock::now();
    if (const auto error = ValidateBasicBruteForceOptionSettings(
                {}, context.tickDurationMs)) {
        throw std::invalid_argument(*error);
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
    const EvaluationPlan evaluationPlan = context.evaluator.Plan(
            static_cast<std::int64_t>(current.durationMs),
            earliestMutationTimeMs,
            context.tickDurationMs);
    if (evaluationPlan.startTimeMs < earliestMutationTimeMs ||
        evaluationPlan.endTimeMs < evaluationPlan.startTimeMs ||
        evaluationPlan.endTimeMs >
                static_cast<std::int64_t>(current.durationMs) ||
        evaluationPlan.startTimeMs % context.tickDurationMs != 0 ||
        evaluationPlan.endTimeMs % context.tickDurationMs != 0) {
        throw std::invalid_argument(
                "evaluation target returned an invalid observation plan");
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
        forevervalidator::SimulationBackend::Cuda) {
        return RunCudaBasicBruteForce(
                context,
                evaluationPlan,
                earliestMutationTimeMs,
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
            if (source == SearchWinnerSource::Mutation) {
                ++mutationImprovementCount;
                lastImprovementElapsed =
                        std::chrono::steady_clock::now() - started;
                reportLive(true);
            } else {
                reportLive(false);
            }
        }
    };

    ReportProgress(context.control, SearchProgressStage::Baseline, 0u);
    evaluateTimeline(SearchWinnerSource::Baseline, std::nullopt, 0u);
    reportLive(true);
    ReportProgress(context.control, SearchProgressStage::Mutations, 0u);

    std::uint64_t iterationIndex = 0u;
    while (!StopRequested(context.control) &&
           !IterationLimitReached(context.control, iterations)) {
        CheckCancellation(context.control);
        Require(context.sandbox.RestoreState(branch),
                "restoring branch state");
        MutationResult mutation = context.mutator.Mutate(
                {baselineInputs,
                 iterationIndex,
                 0u,
                 context.tickDurationMs,
                 earliestMutationTimeMs});
        CheckCancellation(context.control);
        ++iterations;
        if (mutation.mutationCount != 0u) {
            totalMutationCount += mutation.mutationCount;
            Require(context.sandbox.ReplaceInputs(std::move(mutation.inputs)),
                    "replacing iteration inputs");
            evaluateTimeline(SearchWinnerSource::Mutation,
                             iterationIndex,
                             mutation.mutationCount);
        }
        reportLive(false);
        if (iterationIndex == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("iteration sequence exhausted");
        }
        ++iterationIndex;
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
