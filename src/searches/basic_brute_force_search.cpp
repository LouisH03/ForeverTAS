#include "searches/basic_brute_force_search.h"

#include "searches/option_settings_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
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

void ReportProgress(const SearchRunControl *control,
                    SearchProgressStage stage,
                    std::uint64_t completedAttempts,
                    std::uint64_t requestedAttempts) {
    if (control != nullptr && control->progressChanged) {
        control->progressChanged(
                {stage, completedAttempts, requestedAttempts});
    }
}

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

struct BestCandidate {
    std::optional<EvaluationSample> evaluation;
    SearchWinnerSource source = SearchWinnerSource::Baseline;
    std::optional<std::uint64_t> attempt;
    std::size_t mutationCount = 0u;
    PhysicsSandboxStateView view;
    std::optional<PhysicsSandboxState> snapshot;
    std::vector<PhysicsSandboxInputEvent> inputs;
};

std::optional<BasicBruteForceSettings> ParseBasicBruteForceSettings(
        const OptionSettings &settings,
        std::string *error) {
    const OptionSettings defaults = DefaultBasicBruteForceOptionSettings();
    if (const auto keyError = ValidateOptionSettingKeys(settings, defaults)) {
        *error = *keyError;
        return std::nullopt;
    }

    const auto attemptCount =
            ParseUnsignedDecimal64(settings.at("attemptCount"));
    if (!attemptCount) {
        *error = "attempt count must be an unsigned 64-bit decimal integer";
        return std::nullopt;
    }

    return BasicBruteForceSettings{*attemptCount};
}

}  // namespace

BasicBruteForceSettings DefaultBasicBruteForceSettings() {
    return {10u};
}

OptionSettings DefaultBasicBruteForceOptionSettings() {
    const BasicBruteForceSettings defaults =
            DefaultBasicBruteForceSettings();
    return {{"attemptCount", std::to_string(defaults.attemptCount)}};
}

std::optional<std::string> ValidateBasicBruteForceSettings(
        const BasicBruteForceSettings &settings,
        std::uint32_t tickDurationMs) {
    if (tickDurationMs == 0u) {
        return "tick duration must be greater than zero";
    }
    if (settings.attemptCount == 0u) {
        return "attempt count must be greater than zero";
    }
    return std::nullopt;
}

std::optional<std::string> ValidateBasicBruteForceOptionSettings(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs) {
    std::string parseError;
    const auto parsed = ParseBasicBruteForceSettings(settings, &parseError);
    if (!parsed) {
        return parseError;
    }
    return ValidateBasicBruteForceSettings(*parsed, tickDurationMs);
}

std::unique_ptr<SearchAlgorithm> CreateBasicBruteForceSearch(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs) {
    std::string parseError;
    const auto parsed = ParseBasicBruteForceSettings(settings, &parseError);
    if (!parsed) {
        throw std::invalid_argument(parseError);
    }
    if (const auto error =
                ValidateBasicBruteForceSettings(*parsed, tickDurationMs)) {
        throw std::invalid_argument(*error);
    }
    return std::make_unique<BasicBruteForceSearch>(*parsed);
}

BasicBruteForceSearch::BasicBruteForceSearch(
        BasicBruteForceSettings settings)
    : settings_(settings) {}

SearchResult BasicBruteForceSearch::Run(
        const SearchExecutionContext &context) const {
    const auto started = std::chrono::steady_clock::now();
    if (const auto error = ValidateBasicBruteForceSettings(
                settings_, context.tickDurationMs)) {
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

    const std::uint64_t preEvaluationTimeMs =
            static_cast<std::uint64_t>(evaluationPlan.startTimeMs) -
            context.tickDurationMs;
    const std::uint64_t evaluationTicks =
            static_cast<std::uint64_t>(
                    (evaluationPlan.endTimeMs - evaluationPlan.startTimeMs) /
                    context.tickDurationMs) +
            1u;

    BestCandidate best;
    std::uint64_t evaluatorCalls = 0u;
    std::uint64_t mutationImprovementCount = 0u;
    std::uint64_t executedAttempts = 0u;
    std::uint64_t skippedAttempts = 0u;
    std::uint64_t totalMutationCount = 0u;

    const auto evaluateTimeline = [&](SearchWinnerSource source,
                                      std::optional<std::uint64_t> attempt,
                                      std::size_t mutationCount) {
        PhysicsSandboxStateView state = AdvanceTo(
                context.sandbox,
                branchTimeMs,
                preEvaluationTimeMs,
                context.tickDurationMs,
                context.control);
        std::optional<PhysicsSandboxStateView> previous = state;
        std::unique_ptr<CandidateEvaluationSession> session =
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
                        "candidate evaluator returned a non-finite result");
            }
            if (best.evaluation &&
                !context.evaluator.IsBetter(*sample, *best.evaluation)) {
                continue;
            }

            best.evaluation = sample;
            best.source = source;
            best.attempt = attempt;
            best.mutationCount = mutationCount;
            best.view = state;
            best.snapshot = Require(context.sandbox.CaptureState(),
                                    "capturing improved state");
            best.inputs = Require(context.sandbox.ReadInputs(),
                                  "reading improved inputs");
            if (source == SearchWinnerSource::Mutation) {
                ++mutationImprovementCount;
            }
        }
    };

    ReportProgress(context.control,
                   SearchProgressStage::Baseline,
                   0u,
                   settings_.attemptCount);
    evaluateTimeline(SearchWinnerSource::Baseline, std::nullopt, 0u);
    ReportProgress(context.control,
                   SearchProgressStage::Mutations,
                   0u,
                   settings_.attemptCount);

    for (std::uint64_t attempt = 0u;
         attempt < settings_.attemptCount;
         ++attempt) {
        CheckCancellation(context.control);
        Require(context.sandbox.RestoreState(branch),
                "restoring branch state");
        MutationResult mutation = context.mutator.Mutate(
                {baselineInputs, attempt, 0u, context.tickDurationMs});
        CheckCancellation(context.control);
        if (mutation.mutationCount == 0u) {
            ++skippedAttempts;
            ReportProgress(context.control,
                           SearchProgressStage::Mutations,
                           attempt + 1u,
                           settings_.attemptCount);
            continue;
        }

        ++executedAttempts;
        totalMutationCount += mutation.mutationCount;
        Require(context.sandbox.ReplaceInputs(std::move(mutation.inputs)),
                "replacing candidate inputs");
        evaluateTimeline(SearchWinnerSource::Mutation,
                         attempt,
                         mutation.mutationCount);
        ReportProgress(context.control,
                       SearchProgressStage::Mutations,
                       attempt + 1u,
                       settings_.attemptCount);
    }

    CheckCancellation(context.control);
    if (!best.evaluation || !best.snapshot) {
        throw std::runtime_error(
                "no candidate satisfied the selected evaluation target");
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
            best.attempt,
            best.mutationCount,
            best.evaluation->score,
            best.evaluation->timeMs,
            best.evaluation->description,
            best.view,
            std::move(best.inputs),
            {},
            settings_.attemptCount,
            executedAttempts,
            skippedAttempts,
            evaluatorCalls,
            mutationImprovementCount,
            totalMutationCount,
            std::chrono::steady_clock::now() - started,
            *best.snapshot};
}

}  // namespace forevertas
