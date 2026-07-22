#include "searches/serial_brute_force_search.h"

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

std::uint32_t TickCount(std::uint64_t durationMs,
                        std::uint32_t tickDurationMs) {
    if (durationMs % tickDurationMs != 0u) {
        throw std::runtime_error(
                "sandbox state time is not aligned to the tick duration");
    }
    const std::uint64_t count = durationMs / tickDurationMs;
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("advance exceeds the sandbox tick range");
    }
    return static_cast<std::uint32_t>(count);
}

PhysicsSandboxStateView AdvanceTo(PhysicsSandbox &sandbox,
                                  std::uint64_t currentTimeMs,
                                  std::uint64_t targetTimeMs,
                                  std::uint32_t tickDurationMs) {
    if (currentTimeMs > targetTimeMs) {
        throw std::runtime_error("sandbox is already past the requested time");
    }
    if (currentTimeMs == targetTimeMs) {
        return Require(sandbox.ReadState(), "reading sandbox state");
    }
    return Require(
            sandbox.AdvanceTicks(TickCount(targetTimeMs - currentTimeMs,
                                           tickDurationMs)),
            "advancing sandbox");
}

struct BestCandidate {
    double score = -std::numeric_limits<double>::infinity();
    SearchWinnerSource source = SearchWinnerSource::Baseline;
    std::optional<std::uint64_t> attempt;
    std::size_t mutationCount = 0u;
    PhysicsSandboxStateView view;
    std::optional<PhysicsSandboxState> snapshot;
};

}  // namespace

std::optional<std::string> ValidateSerialBruteForceSettings(
        const SerialBruteForceSettings &settings,
        std::uint32_t tickDurationMs) {
    if (tickDurationMs == 0u) {
        return "tick duration must be greater than zero";
    }
    if (settings.minMutateMs < static_cast<std::int64_t>(tickDurationMs)) {
        return "minimum mutation time must be at least one tick";
    }
    if (settings.maxMutateMs < settings.minMutateMs) {
        return "maximum mutation time must not precede its minimum";
    }
    if (settings.minEvalTimeMs < settings.minMutateMs) {
        return "minimum evaluation time must not precede mutation";
    }
    if (settings.maxEvalTimeMs < settings.minEvalTimeMs) {
        return "maximum evaluation time must not precede its minimum";
    }
    if (settings.attemptCount == 0u) {
        return "attempt count must be greater than zero";
    }

    const std::int64_t tick = static_cast<std::int64_t>(tickDurationMs);
    if (settings.minMutateMs % tick != 0 ||
        settings.maxMutateMs % tick != 0 ||
        settings.minEvalTimeMs % tick != 0 ||
        settings.maxEvalTimeMs % tick != 0) {
        return "mutation and evaluation times must align to whole ticks";
    }
    return std::nullopt;
}

SerialBruteForceSearch::SerialBruteForceSearch(
        SerialBruteForceSettings settings)
    : settings_(settings) {}

SearchResult SerialBruteForceSearch::Run(
        const SearchExecutionContext &context) const {
    const auto started = std::chrono::steady_clock::now();
    if (const auto error = ValidateSerialBruteForceSettings(
                settings_, context.tickDurationMs)) {
        throw std::invalid_argument(*error);
    }

    PhysicsSandboxStateView current = Require(
            context.sandbox.ReadState(), "reading initial sandbox state");
    const std::uint64_t branchTimeMs =
            static_cast<std::uint64_t>(settings_.minMutateMs) -
            context.tickDurationMs;
    current = AdvanceTo(context.sandbox,
                        current.timeMs,
                        branchTimeMs,
                        context.tickDurationMs);

    const std::vector<PhysicsSandboxInputEvent> baselineInputs = Require(
            context.sandbox.ReadInputs(), "reading baseline inputs");
    const PhysicsSandboxState branch = Require(
            context.sandbox.CaptureState(), "capturing branch state");

    const std::uint64_t preEvaluationTimeMs =
            static_cast<std::uint64_t>(settings_.minEvalTimeMs) -
            context.tickDurationMs;
    const std::uint64_t evaluationTicks =
            static_cast<std::uint64_t>(
                    (settings_.maxEvalTimeMs - settings_.minEvalTimeMs) /
                    context.tickDurationMs) +
            1u;

    BestCandidate best;
    std::uint64_t evaluatorCalls = 0u;
    std::uint64_t improvementCount = 0u;
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
                context.tickDurationMs);
        for (std::uint64_t tick = 0u; tick < evaluationTicks; ++tick) {
            state = Require(context.sandbox.AdvanceTicks(1u),
                            "advancing evaluation tick");
            const double score = context.evaluator.Evaluate(state);
            ++evaluatorCalls;
            if (!std::isfinite(score)) {
                throw std::runtime_error(
                        "candidate evaluator returned a non-finite score");
            }
            if (score <= best.score) {
                continue;
            }

            best.score = score;
            best.source = source;
            best.attempt = attempt;
            best.mutationCount = mutationCount;
            best.view = state;
            best.snapshot = Require(context.sandbox.CaptureState(),
                                    "capturing improved state");
            ++improvementCount;
        }
    };

    evaluateTimeline(SearchWinnerSource::Baseline, std::nullopt, 0u);

    for (std::uint64_t attempt = 0u;
         attempt < settings_.attemptCount;
         ++attempt) {
        Require(context.sandbox.RestoreState(branch),
                "restoring branch state");
        MutationResult mutation = context.mutator.Mutate(MutationRequest{
                baselineInputs,
                settings_.minMutateMs,
                settings_.maxMutateMs,
                settings_.mutationSeed,
                attempt});
        if (mutation.mutationCount == 0u) {
            ++skippedAttempts;
            continue;
        }

        ++executedAttempts;
        totalMutationCount += mutation.mutationCount;
        Require(context.sandbox.ReplaceInputs(std::move(mutation.inputs)),
                "replacing candidate inputs");
        evaluateTimeline(SearchWinnerSource::Mutation,
                         attempt,
                         mutation.mutationCount);
    }

    if (!best.snapshot) {
        throw std::runtime_error("search produced no evaluated state");
    }
    const PhysicsSandboxStateView restored = Require(
            context.sandbox.RestoreState(*best.snapshot),
            "restoring global best state");
    if (!SameState(restored, best.view)) {
        throw std::runtime_error(
                "restored global best does not match its captured state");
    }

    const auto elapsed = std::chrono::steady_clock::now() - started;
    return SearchResult{
            best.source,
            best.attempt,
            best.mutationCount,
            best.score,
            best.view,
            settings_.attemptCount,
            executedAttempts,
            skippedAttempts,
            evaluatorCalls,
            improvementCount,
            totalMutationCount,
            elapsed,
            *best.snapshot};
}

}  // namespace forevertas
