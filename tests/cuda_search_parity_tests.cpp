#include "mutations/input_event_utils.h"
#include "searches/algorithm_registry.h"
#include "searches/search_runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using forevertas::OptionConfiguration;
using forevertas::SearchRequest;
using forevertas::SearchResult;

bool SameInputs(
        const std::vector<forevertas::SandboxInputEvent> &left,
        const std::vector<forevertas::SandboxInputEvent> &right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0u; index < left.size(); ++index) {
        if (!forevertas::SameInputEvent(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

SearchResult Run(const char *packs,
                 const char *replay,
                 forevertas::PhysicsBackend backend,
                 std::uint32_t batchSize,
                 std::uint64_t iterations,
                 std::vector<OptionConfiguration> modifiers,
                 OptionConfiguration evaluator,
                 bool calibrate = false,
                 std::vector<std::uint32_t> *calibrationUpdates = nullptr,
                 bool sampleBestTimeline = false,
                 std::optional<std::int64_t>
                         evaluationEndTimeLimitMs = std::nullopt) {
    SearchRequest request{packs, replay};
    request.backend = backend;
    request.parallelSampleCount = batchSize;
    request.calibrateCudaParallelSampleCount = calibrate;
    request.modifiers = std::move(modifiers);
    request.evaluationTarget = std::move(evaluator);
    forevertas::SearchRunControl control;
    control.iterationLimit = iterations;
    control.sampleBestTimeline = sampleBestTimeline;
    control.evaluationEndTimeLimitMs = evaluationEndTimeLimitMs;
    control.reuseLoadedSandbox = true;
    control.cudaBatchSizeChanged =
            [calibrationUpdates](std::uint32_t value) {
                if (calibrationUpdates != nullptr &&
                    (calibrationUpdates->empty() ||
                     calibrationUpdates->back() != value)) {
                    calibrationUpdates->push_back(value);
                }
            };
    return forevertas::RunSearch(request, &control);
}

bool SameAuthoritativeResult(const SearchResult &reference,
                             const SearchResult &cuda,
                             const std::string &label) {
    const bool same =
            reference.winnerSource == cuda.winnerSource &&
            reference.winningIterationIndex ==
                    cuda.winningIterationIndex &&
            reference.winningMutationCount ==
                    cuda.winningMutationCount &&
            reference.bestScore == cuda.bestScore &&
            reference.bestEvaluationTimeMs ==
                    cuda.bestEvaluationTimeMs &&
            reference.iterations == cuda.iterations &&
            reference.evaluatorCalls == cuda.evaluatorCalls &&
            reference.mutationImprovementCount ==
                    cuda.mutationImprovementCount &&
            reference.totalMutationCount ==
                    cuda.totalMutationCount &&
            SameInputs(reference.bestInputs, cuda.bestInputs);
    if (!same) {
        std::cerr << label
                  << " parity failed: reference winner="
                  << (reference.winningIterationIndex
                              ? std::to_string(
                                        *reference.winningIterationIndex)
                              : "baseline")
                  << " CUDA winner="
                  << (cuda.winningIterationIndex
                              ? std::to_string(
                                        *cuda.winningIterationIndex)
                              : "baseline")
                  << " reference score=" << reference.bestScore
                  << " CUDA score=" << cuda.bestScore
                  << " reference mutations="
                  << reference.totalMutationCount
                  << " CUDA mutations=" << cuda.totalMutationCount
                  << '\n';
    }
    return same;
}

OptionConfiguration DefaultModifier(const std::string &id) {
    const auto *registration = forevertas::FindModifier(id);
    if (registration == nullptr) {
        throw std::runtime_error("missing modifier registration: " + id);
    }
    return {registration->id, registration->defaultSettings};
}

OptionConfiguration DefaultEvaluator(const std::string &id) {
    const auto *registration =
            forevertas::FindEvaluationTarget(id);
    if (registration == nullptr) {
        throw std::runtime_error(
                "missing evaluator registration: " + id);
    }
    return {registration->id, registration->defaultSettings};
}

bool CheckParity(const char *packs,
                 const char *replay,
                 const std::string &label,
                 std::uint32_t batchSize,
                 std::uint64_t iterations,
                 const std::vector<OptionConfiguration> &modifiers,
                 const OptionConfiguration &evaluator,
                 bool requireMutationWinner = false,
                 double *cudaSeconds = nullptr,
                 std::optional<std::int64_t>
                         evaluationEndTimeLimitMs = std::nullopt) {
    const auto started = std::chrono::steady_clock::now();
    std::cout << "checking " << label << std::endl;
    std::future<SearchResult> reference = std::async(
            std::launch::async,
            [=]() {
                return Run(
                        packs,
                        replay,
                        forevertas::PhysicsBackend::Reference,
                        1u,
                        iterations,
                        modifiers,
                        evaluator,
                        false,
                        nullptr,
                        false,
                        evaluationEndTimeLimitMs);
            });
    const auto cudaStarted = std::chrono::steady_clock::now();
    const SearchResult cuda = Run(
            packs, replay, forevertas::PhysicsBackend::Cuda,
            batchSize, iterations, modifiers, evaluator,
            false, nullptr, false, evaluationEndTimeLimitMs);
    const SearchResult authoritative = reference.get();
    if (cudaSeconds != nullptr) {
        *cudaSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - cudaStarted)
                               .count();
    }
    std::cout << label << " winner="
              << (cuda.winningIterationIndex
                          ? std::to_string(
                                    *cuda.winningIterationIndex)
                          : "baseline")
              << " seconds="
              << std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - started)
                         .count()
              << std::endl;
    const bool mutationWinner =
            cuda.winningIterationIndex.has_value();
    if (requireMutationWinner && !mutationWinner) {
        std::cerr << label
                  << " did not exercise winning candidate data\n";
    }
    return SameAuthoritativeResult(authoritative, cuda, label) &&
            (!requireMutationWinner || mutationWinner);
}

bool CheckCancellation(const char *packs, const char *replay) {
    SearchRequest request{packs, replay};
    request.backend = forevertas::PhysicsBackend::Cuda;
    request.parallelSampleCount = 4096u;
    forevertas::SearchRunControl control;
    control.reuseLoadedSandbox = true;
    std::chrono::steady_clock::time_point mutationStarted{};
    control.progressChanged =
            [&](const forevertas::SearchProgress &progress) {
                if (progress.stage ==
                    forevertas::SearchProgressStage::Mutations) {
                    mutationStarted = std::chrono::steady_clock::now();
                }
            };
    control.cancellationRequested = [&]() {
        return mutationStarted.time_since_epoch().count() != 0 &&
                std::chrono::steady_clock::now() - mutationStarted >
                        std::chrono::milliseconds(5);
    };
    try {
        static_cast<void>(forevertas::RunSearch(request, &control));
    } catch (const forevertas::SearchCancelled &) {
        return true;
    }
    std::cerr << "running CUDA batch ignored cancellation\n";
    return false;
}

bool CheckCalibration(const char *packs, const char *replay) {
    OptionConfiguration insertion = DefaultModifier(
            forevertas::kInputInsertionModifierId);
    insertion.settings["minTimeMs"] = "1000";
    insertion.settings["maxTimeMs"] = "1000";
    insertion.settings["steerMinCount"] = "1";
    insertion.settings["steerMaxCount"] = "1";
    insertion.settings["steerMaxHoldMs"] = "0";
    insertion.settings["steerOffsetMin"] = "0.1";
    insertion.settings["steerOffsetMax"] = "0.1";
    OptionConfiguration velocity = DefaultEvaluator(
            forevertas::kVelocityEvaluationId);
    velocity.settings["minTimeMs"] = "1000";
    velocity.settings["maxTimeMs"] = "1000";

    const SearchResult reference = Run(
            packs,
            replay,
            forevertas::PhysicsBackend::Reference,
            1u,
            1024u,
            {insertion},
            velocity);
    std::vector<std::uint32_t> updates;
    const SearchResult calibrated = Run(
            packs,
            replay,
            forevertas::PhysicsBackend::Cuda,
            64u,
            1024u,
            {insertion},
            velocity,
            true,
            &updates);
    const bool grew = std::any_of(
            updates.begin(),
            updates.end(),
            [](std::uint32_t value) {
                return value > 64u;
            });
    if (updates.size() < 3u || updates.front() != 1u || !grew) {
        std::cerr
                << "real CUDA calibration depended on the configured "
                   "batch size or did not grow the live value\n";
        return false;
    }
    return SameAuthoritativeResult(
            reference, calibrated, "calibrated CUDA winner");
}

bool CheckPreciseFinishParity(const char *packs, const char *replay) {
    const std::vector<OptionConfiguration> modifiers{
            DefaultModifier(
                    forevertas::kRandomSteeringModifierId)};
    const OptionConfiguration evaluator = DefaultEvaluator(
            forevertas::kPreciseFinishTimeEvaluationId);
    const SearchResult reference = Run(
            packs,
            replay,
            forevertas::PhysicsBackend::Reference,
            1u,
            2u,
            modifiers,
            evaluator);
    const SearchResult optimized = Run(
            packs,
            replay,
            forevertas::PhysicsBackend::OptimizedCpu,
            1u,
            2u,
            modifiers,
            evaluator);
    const SearchResult cuda = Run(
            packs,
            replay,
            forevertas::PhysicsBackend::Cuda,
            2u,
            2u,
            modifiers,
            evaluator);
    return SameAuthoritativeResult(
                   reference, optimized, "precise finish optimized CPU") &&
            SameAuthoritativeResult(
                    reference, cuda, "precise finish CUDA");
}

}  // namespace

int main(int argc, char **argv) {
    const bool calibrationOnly =
            argc == 4 &&
            std::string(argv[1]) == "--calibration-only";
    const bool preciseFinishOnly =
            argc == 4 &&
            std::string(argv[1]) == "--precise-finish-only";
    if ((!calibrationOnly && !preciseFinishOnly && argc != 3) ||
        ((calibrationOnly || preciseFinishOnly) && argc != 4)) {
        std::cerr << "expected Packs directory and replay path\n";
        return 2;
    }
    const bool focusedMode = calibrationOnly || preciseFinishOnly;
    const char *const packs = argv[focusedMode ? 2 : 1];
    const char *const replay = argv[focusedMode ? 3 : 2];
    try {
        if (calibrationOnly) {
            return CheckCalibration(packs, replay) ? 0 : 1;
        }
        if (preciseFinishOnly) {
            return CheckPreciseFinishParity(packs, replay) ? 0 : 1;
        }
        bool okay = true;
        const OptionConfiguration velocity =
                DefaultEvaluator(forevertas::kVelocityEvaluationId);
        OptionConfiguration coverageVelocity = velocity;
        coverageVelocity.settings["minTimeMs"] = "1000";
        coverageVelocity.settings["maxTimeMs"] = "1000";
        constexpr std::int64_t shortEvaluationEndTimeMs = 1020;

        std::vector<OptionConfiguration> completeModifierPipeline;
        for (const auto &registration :
             forevertas::ModifierRegistry()) {
            OptionConfiguration modifier{
                    registration.id,
                    registration.defaultSettings};
            modifier.settings["minTimeMs"] = "1000";
            modifier.settings["maxTimeMs"] = "1000";
            completeModifierPipeline.push_back(std::move(modifier));
        }
        okay &= CheckParity(
                argv[1],
                argv[2],
                "complete modifier pipeline",
                2u,
                3u,
                completeModifierPipeline,
                coverageVelocity,
                false,
                nullptr,
                shortEvaluationEndTimeMs);

        const OptionConfiguration random = DefaultModifier(
                forevertas::kRandomSteeringModifierId);
        const SearchResult baselineProbe = Run(
                packs,
                replay,
                forevertas::PhysicsBackend::Reference,
                1u,
                0u,
                {random},
                velocity,
                false,
                nullptr,
                true);
        if (baselineProbe.bestTimeline.size() < 3u) {
            throw std::runtime_error(
                    "baseline sampling did not produce a timeline");
        }
        const auto volumeTargetPosition = std::find_if(
                baselineProbe.bestTimeline.begin(),
                baselineProbe.bestTimeline.end(),
                [](const forevertas::SearchTimelineFrame &frame) {
                    return frame.timeMs == 1020;
                });
        if (volumeTargetPosition ==
            baselineProbe.bestTimeline.end()) {
            throw std::runtime_error(
                    "baseline sampling missed the short parity target");
        }
        const forevertas::SearchTimelineFrame &volumeTarget =
                *volumeTargetPosition;
        const auto decimal = [](float value) {
            std::ostringstream stream;
            stream << std::setprecision(17)
                   << static_cast<double>(value);
            return stream.str();
        };
        const forevertas::SearchTimelineFrame &steeringTarget =
                baselineProbe.bestTimeline[
                        std::min<std::size_t>(
                                500u,
                                baselineProbe.bestTimeline.size() - 1u)];
        const forevertas::SearchTimelineFrame &steeringPrevious =
                baselineProbe.bestTimeline[
                        std::min<std::size_t>(
                                499u,
                                baselineProbe.bestTimeline.size() - 1u)];
        const double tangentX =
                steeringTarget.positionX -
                steeringPrevious.positionX;
        const double tangentZ =
                steeringTarget.positionZ -
                steeringPrevious.positionZ;
        const double tangentLength =
                std::hypot(tangentX, tangentZ);
        const double lateralX = tangentLength == 0.0
                ? 20.0
                : -20.0 * tangentZ / tangentLength;
        const double lateralZ = tangentLength == 0.0
                ? 0.0
                : 20.0 * tangentX / tangentLength;
        OptionConfiguration offLinePoint = DefaultEvaluator(
                forevertas::kPointTargetEvaluationId);
        offLinePoint.settings["minTimeMs"] = "4000";
        offLinePoint.settings["maxTimeMs"] = "6000";
        offLinePoint.settings["x"] =
                decimal(static_cast<float>(
                        steeringTarget.positionX + lateralX));
        offLinePoint.settings["y"] =
                decimal(steeringTarget.positionY);
        offLinePoint.settings["z"] =
                decimal(static_cast<float>(
                        steeringTarget.positionZ + lateralZ));
        okay &= CheckParity(
                argv[1],
                argv[2],
                "random-steering winning candidate",
                32u,
                64u,
                {random},
                offLinePoint,
                true);
        okay &= CheckParity(
                argv[1],
                argv[2],
                "existing-event winning candidate",
                32u,
                64u,
                {DefaultModifier(
                        forevertas::
                                kExistingEventPerturbationModifierId)},
                offLinePoint,
                true);
        for (const auto &registration :
             forevertas::EvaluationTargetRegistry()) {
            OptionConfiguration configured{
                    registration.id,
                    registration.defaultSettings};
            const auto minimum =
                    configured.settings.find("minTimeMs");
            const auto maximum =
                    configured.settings.find("maxTimeMs");
            if (minimum != configured.settings.end() &&
                maximum != configured.settings.end()) {
                minimum->second = "1000";
                maximum->second = "1000";
            }
            if (registration.id ==
                forevertas::kVolumeEntryEvaluationId) {
                configured.settings["centerX"] =
                        decimal(volumeTarget.positionX);
                configured.settings["centerY"] =
                        decimal(volumeTarget.positionY);
                configured.settings["centerZ"] =
                        decimal(volumeTarget.positionZ);
                configured.settings["sizeX"] = "0.01";
                configured.settings["sizeY"] = "0.01";
                configured.settings["sizeZ"] = "0.01";
            }
            okay &= CheckParity(
                    argv[1],
                    argv[2],
                    "evaluator " + registration.id,
                    2u,
                    2u,
                    {random},
                    configured,
                    false,
                    nullptr,
                    registration.id ==
                                    forevertas::
                                            kPreciseFinishTimeEvaluationId
                            ? std::nullopt
                            : std::optional<std::int64_t>(
                                      shortEvaluationEndTimeMs));
        }

        double batchOneSeconds = 0.0;
        okay &= CheckParity(
                argv[1], argv[2], "batch size one",
                1u, 4u, {random}, velocity, false,
                &batchOneSeconds);
        const auto largeStarted = std::chrono::steady_clock::now();
        const SearchResult large = Run(
                argv[1],
                argv[2],
                forevertas::PhysicsBackend::Cuda,
                256u,
                257u,
                {random},
                velocity);
        const double largeSeconds =
                std::chrono::duration<double>(
                        std::chrono::steady_clock::now() -
                        largeStarted)
                        .count();
        const double batchOneRate =
                batchOneSeconds > 0.0
                ? 4.0 / batchOneSeconds
                : 0.0;
        const double largeRate =
                largeSeconds > 0.0
                ? static_cast<double>(large.iterations) /
                          largeSeconds
                : 0.0;
        if (large.iterations != 257u ||
            largeRate <= batchOneRate) {
            std::cerr << "large partial CUDA batch did not complete\n";
            okay = false;
        }
        std::cout << "stadium_cuda_batch_one_candidates_per_second="
                  << batchOneRate << '\n';
        std::cout << "realistic_stadium_cuda_candidates_per_second="
                  << largeRate
                  << '\n';

        OptionConfiguration shortInsertion = DefaultModifier(
                forevertas::kInputInsertionModifierId);
        shortInsertion.settings["minTimeMs"] = "1000";
        shortInsertion.settings["maxTimeMs"] = "1000";
        shortInsertion.settings["steerMinCount"] = "1";
        shortInsertion.settings["steerMaxCount"] = "1";
        shortInsertion.settings["steerMaxHoldMs"] = "0";
        shortInsertion.settings["steerOffsetMin"] = "0.1";
        shortInsertion.settings["steerOffsetMax"] = "0.1";
        OptionConfiguration shortVelocity = velocity;
        shortVelocity.settings["minTimeMs"] = "1000";
        shortVelocity.settings["maxTimeMs"] = "1000";
        const auto aboveOldCapStarted =
                std::chrono::steady_clock::now();
        const SearchResult aboveOldCap = Run(
                argv[1],
                argv[2],
                forevertas::PhysicsBackend::Cuda,
                8192u,
                8192u,
                {shortInsertion},
                shortVelocity);
        const double aboveOldCapSeconds =
                std::chrono::duration<double>(
                        std::chrono::steady_clock::now() -
                        aboveOldCapStarted)
                        .count();
        if (aboveOldCap.iterations != 8192u ||
            aboveOldCap.evaluatorCalls != 8193u) {
            std::cerr
                    << "CUDA did not fully evaluate a batch above the "
                       "old cap\n";
            okay = false;
        }
        std::cout << "cuda_8192_batch_candidates_per_second="
                  << 8192.0 / aboveOldCapSeconds << '\n';

        const SearchResult replayA = Run(
                argv[1], argv[2],
                forevertas::PhysicsBackend::Cuda,
                13u, 31u, {random}, velocity);
        const SearchResult replayB = Run(
                argv[1], argv[2],
                forevertas::PhysicsBackend::Cuda,
                13u, 31u, {random}, velocity);
        okay &= SameAuthoritativeResult(
                replayA, replayB, "deterministic CUDA replay");
        okay &= CheckCancellation(argv[1], argv[2]);
        return okay ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
