#include "mutations/input_event_formatter.h"
#include "mutations/replay_input_script.h"
#include "replay_file_io.h"
#include "searches/search_runner.h"

#include <forevervalidator/native.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

template<typename T, typename Error>
T Require(forevervalidator::DiscriminatedResult<T, Error> result,
          const char *operation) {
    if (!result) {
        throw std::runtime_error(std::string(operation) + " failed");
    }
    return std::move(result).Value();
}

bool RunBackend(const char *packsDirectory,
                const char *replayPath,
                forevertas::PhysicsBackend backend) {
        constexpr std::uint64_t iterationsBeforeStop = 64u;
        bool stopRequested = false;
        bool sawLiveBest = false;
        bool sawFinalSampling = false;
        bool improvementTimelineInvalid = false;
        std::uint64_t lastSampledImprovement = 0u;
        std::size_t sampledImprovementCount = 0u;
        std::chrono::steady_clock::duration previousElapsed{};
        std::size_t liveUpdateCount = 0u;
        forevertas::SearchRunControl control;
        control.stopRequested = [&stopRequested]() {
            return stopRequested;
        };
        control.liveChanged = [&](const forevertas::SearchLiveUpdate &live) {
            if (sawFinalSampling) {
                std::cerr << "live search update arrived after final sampling started\n";
                return;
            }
            sawLiveBest |= !live.bestInputs.empty();
            if (!live.bestTimeline.empty()) {
                const bool completeTimeline =
                        live.winnerSource ==
                                forevertas::SearchWinnerSource::Mutation &&
                        live.mutationImprovementCount >
                                lastSampledImprovement &&
                        live.bestTimeline.front().timeMs == 0 &&
                        live.bestTimeline.back().timeMs ==
                                static_cast<std::int64_t>(
                                        live.bestState.durationMs);
                bool everyTick = true;
                for (std::size_t index = 1u;
                     index < live.bestTimeline.size();
                     ++index) {
                    everyTick &=
                            live.bestTimeline[index].timeMs -
                                    live.bestTimeline[index - 1u].timeMs ==
                            10;
                }
                improvementTimelineInvalid |=
                        !completeTimeline || !everyTick;
                lastSampledImprovement =
                        live.mutationImprovementCount;
                ++sampledImprovementCount;
            }
            if (liveUpdateCount != 0u && live.elapsed < previousElapsed) {
                std::cerr << "live elapsed time moved backwards\n";
            }
            previousElapsed = live.elapsed;
            ++liveUpdateCount;
            if (live.iterations >= iterationsBeforeStop) {
                stopRequested = true;
            }
        };
        control.progressChanged = [&sawFinalSampling](
                                          const forevertas::SearchProgress
                                                  &progress) {
            sawFinalSampling |= progress.stage ==
                    forevertas::SearchProgressStage::FinalSampling;
        };
        control.reuseLoadedSandbox = true;
        forevertas::SearchRequest request{packsDirectory, replayPath};
        const forevertas::InputScriptParseResult parsed =
                forevertas::ParseInputScript(
                        forevertas::ExtractReplayInputScript(
                                packsDirectory, replayPath));
        if (!parsed) {
            throw std::runtime_error(*parsed.error);
        }
        request.baseInputCommands = parsed.commands;
        request.backend = backend;
#if FOREVERVALIDATOR_HAS_CUDA
        if (backend == forevertas::PhysicsBackend::Cuda) {
            request.parallelSampleCount =
                    forevertas::kDefaultCudaParallelSampleCount;
        }
#endif
        const forevertas::SearchResult result =
                forevertas::RunSearch(request, &control);
        const bool mutationWon =
                result.winnerSource ==
                forevertas::SearchWinnerSource::Mutation;
        if (mutationWon != (result.mutationImprovementCount > 0u)) {
            std::cerr << "winner and mutation improvement count disagree\n";
            return false;
        }
        if (!stopRequested || result.iterations < iterationsBeforeStop) {
            std::cerr << "search returned before the stop request\n";
            return false;
        }
        if (!sawLiveBest || liveUpdateCount < 2u) {
            std::cerr << "search did not publish live updates while running\n";
            return false;
        }
        if (improvementTimelineInvalid ||
            (result.mutationImprovementCount > 0u) !=
                    (sampledImprovementCount > 0u) ||
            (result.mutationImprovementCount > 0u &&
             lastSampledImprovement !=
                     result.mutationImprovementCount)) {
            std::cerr
                    << "best-run improvements did not publish complete "
                       "timelines\n";
            return false;
        }
        if (result.bestInputs.empty()) {
            std::cerr << "best input timeline was not retained\n";
            return false;
        }
        for (const forevertas::SandboxInputEvent &event :
             result.bestInputs) {
            if (event.value.kind == forevervalidator::experimental::
                            PhysicsSandboxInputValueKind::Analog &&
                !forevervalidator::IsAnalogInputStateValid(
                        event.value.analog)) {
                std::cerr << "best inputs contain an out-of-range analog state\n";
                return false;
            }
        }
        const std::string inputScript =
                forevertas::FormatInputScript(result.bestInputs);
        if (inputScript.rfind("0.00 ", 0u) != 0u ||
            inputScript.find(" release ") != std::string::npos ||
            inputScript.find(" rel ") == std::string::npos) {
            std::cerr << "best inputs were not exported as an input script\n";
            return false;
        }
        if (!sawFinalSampling || result.bestTimeline.empty()) {
            std::cerr << "best run was not sampled after Stop\n";
            return false;
        }
        if (result.bestTimeline.front().timeMs != 0 ||
            result.bestTimeline.back().timeMs !=
                    static_cast<std::int64_t>(result.bestState.durationMs)) {
            std::cerr << "best run sampling did not cover the full replay\n";
            return false;
        }
        for (std::size_t index = 1u;
             index < result.bestTimeline.size();
             ++index) {
            if (result.bestTimeline[index].timeMs -
                        result.bestTimeline[index - 1u].timeMs !=
                10) {
                std::cerr << "best run was not sampled every tick\n";
                return false;
            }
        }
        const double searchSeconds =
                std::chrono::duration<double>(previousElapsed).count();
        const double iterationsPerSecond = searchSeconds > 0.0
                ? static_cast<double>(result.iterations) / searchSeconds
                : 0.0;
        std::cout << "backend=" << forevertas::PhysicsBackendId(backend)
                  << " winner="
                  << (mutationWon ? "Mutation" : "Baseline")
                  << " score=" << result.bestScore
                  << " improvements="
                  << result.mutationImprovementCount
                  << " iteration="
                  << (result.winningIterationIndex
                              ? std::to_string(
                                        *result.winningIterationIndex + 1u)
                              : std::string("none"))
                  << " iterations=" << result.iterations
                  << " iterations_per_second=" << iterationsPerSecond
                  << " inputs=" << result.bestInputs.size()
                  << " frames=" << result.bestTimeline.size() << '\n';
        return true;
}

bool CheckStuntTargetBackend(
        const char *packsDirectory,
        const char *replayPath,
        forevertas::PhysicsBackend backend) {
    const forevertas::InputScriptParseResult parsed =
            forevertas::ParseInputScript(
                    forevertas::ExtractReplayInputScript(
                            packsDirectory, replayPath));
    if (!parsed) {
        throw std::runtime_error(*parsed.error);
    }

    forevertas::SearchRunControl control;
    control.iterationLimit = 0u;
    control.sampleBestTimeline = false;
    forevertas::SearchRequest request{packsDirectory, replayPath};
    request.backend = backend;
    request.baseInputCommands = parsed.commands;
    request.evaluationTarget = {
            forevertas::kStuntPointsEvaluationId,
            {{"targetTimeMs", "6000"}}};
    const forevertas::SearchResult result =
            forevertas::RunSearch(request, &control);
    const bool valid =
            result.bestEvaluationTimeMs == 6010.0 &&
            result.bestState.timeMs == 6010u &&
            result.bestScore ==
                    static_cast<double>(
                            result.bestState.stuntsScore.value_or(0u)) &&
            result.bestEvaluationDescription.rfind(
                    "Stunt points: ", 0u) == 0u;
    if (!valid) {
        std::cerr
                << "stunt target did not observe the configured deadline "
                << "with backend "
                << forevertas::PhysicsBackendId(backend) << '\n';
    }
    return valid;
}

bool CheckMultiThreadedCpuBackend(
        const char *packsDirectory,
        const char *replayPath) {
    const forevertas::InputScriptParseResult parsed =
            forevertas::ParseInputScript(
                    forevertas::ExtractReplayInputScript(
                            packsDirectory, replayPath));
    if (!parsed) {
        throw std::runtime_error(*parsed.error);
    }

    forevertas::SearchRequest serialRequest{
            packsDirectory, replayPath};
    serialRequest.backend =
            forevertas::PhysicsBackend::OptimizedCpu;
    serialRequest.baseInputCommands = parsed.commands;
    forevertas::SearchRunControl serialControl;
    serialControl.iterationLimit = 6u;
    serialControl.evaluationEndTimeLimitMs = 1020;
    serialControl.sampleBestTimeline = false;
    const forevertas::SearchResult serial =
            forevertas::RunSearch(serialRequest, &serialControl);

    forevertas::SearchRequest parallelRequest = serialRequest;
    parallelRequest.backend =
            forevertas::PhysicsBackend::MultiThreadedCpu;
    parallelRequest.parallelSampleCount = 3u;
    forevertas::SearchRunControl parallelControl = serialControl;
    const std::thread::id callerThread = std::this_thread::get_id();
    std::uint64_t previousIterations = 0u;
    std::size_t liveUpdateCount = 0u;
    bool aggregateInvalid = false;
    parallelControl.liveChanged =
            [&](const forevertas::SearchLiveUpdate &live) {
                aggregateInvalid |=
                        std::this_thread::get_id() != callerThread ||
                        live.iterations < previousIterations ||
                        live.iterations > 6u;
                previousIterations = live.iterations;
                ++liveUpdateCount;
            };
    const forevertas::SearchResult parallel =
            forevertas::RunSearch(parallelRequest, &parallelControl);
    const bool sameWinner =
            parallel.bestScore == serial.bestScore &&
            parallel.bestEvaluationTimeMs ==
                    serial.bestEvaluationTimeMs &&
            parallel.winnerSource == serial.winnerSource &&
            parallel.winningIterationIndex ==
                    serial.winningIterationIndex &&
            forevertas::FormatInputScript(parallel.bestInputs) ==
                    forevertas::FormatInputScript(serial.bestInputs);
    if (parallel.iterations != 6u || liveUpdateCount == 0u ||
        previousIterations != 6u || aggregateInvalid ||
        !sameWinner ||
        (parallel.winnerSource ==
                 forevertas::SearchWinnerSource::Mutation &&
         parallel.mutationImprovementCount == 0u)) {
        std::cerr
                << "multi-threaded CPU did not aggregate the same six "
                   "independent candidates as serial optimized CPU\n";
        return false;
    }

    bool stop = true;
    forevertas::SearchRunControl stoppedControl;
    stoppedControl.stopRequested = [&]() { return stop; };
    stoppedControl.sampleBestTimeline = false;
    const forevertas::SearchResult stopped =
            forevertas::RunSearch(parallelRequest, &stoppedControl);
    if (stopped.iterations != 0u) {
        std::cerr
                << "multi-threaded CPU ignored a stop before mutations\n";
        return false;
    }

    parallelRequest.parallelSampleCount = 0u;
    try {
        static_cast<void>(
                forevertas::RunSearch(
                        parallelRequest, &serialControl));
        std::cerr << "multi-threaded CPU accepted zero workers\n";
        return false;
    } catch (const std::invalid_argument &) {
    }
    parallelRequest.parallelSampleCount = 2u;
    forevertas::SearchRunControl cancelledControl;
    cancelledControl.cancellationRequested = []() { return true; };
    try {
        static_cast<void>(
                forevertas::RunSearch(
                        parallelRequest, &cancelledControl));
        std::cerr
                << "multi-threaded CPU ignored startup cancellation\n";
        return false;
    } catch (const forevertas::SearchCancelled &) {
    }

    forevertas::SearchRunControl throwingCallbackControl =
            serialControl;
    throwingCallbackControl.liveChanged =
            [](const forevertas::SearchLiveUpdate &) {
                throw std::runtime_error(
                        "expected aggregate callback failure");
            };
    try {
        static_cast<void>(
                forevertas::RunSearch(
                        parallelRequest, &throwingCallbackControl));
        std::cerr
                << "multi-threaded CPU swallowed an aggregate callback "
                   "failure\n";
        return false;
    } catch (const std::runtime_error &error) {
        if (std::string(error.what()) !=
            "expected aggregate callback failure") {
            throw;
        }
    }
    return true;
}

bool CheckCachedScriptIsolation(const char *packsDirectory,
                                const char *replayPath) {
    const std::string replayScript =
            forevertas::ExtractReplayInputScript(
                    packsDirectory, replayPath);
    forevertas::InputScriptParseResult parsed =
            forevertas::ParseInputScript(replayScript);
    if (!parsed) {
        throw std::runtime_error(*parsed.error);
    }

    forevertas::SearchRunControl control;
    control.iterationLimit = 0u;
    control.reuseLoadedSandbox = true;
    control.sampleBestTimeline = false;
    forevertas::SearchRequest request{packsDirectory, replayPath};
    request.baseInputCommands = parsed.commands;
    const forevertas::SearchResult first =
            forevertas::RunSearch(request, &control);

    request.baseInputCommands.clear();
    const forevertas::SearchResult empty =
            forevertas::RunSearch(request, &control);

    request.baseInputCommands = parsed.commands;
    const forevertas::SearchResult restored =
            forevertas::RunSearch(request, &control);
    const std::string firstScript =
            forevertas::FormatInputScript(first.bestInputs);
    const std::string emptyScript =
            forevertas::FormatInputScript(empty.bestInputs);
    const std::string restoredScript =
            forevertas::FormatInputScript(restored.bestInputs);
    if (firstScript.empty() || !emptyScript.empty() ||
        firstScript != restoredScript) {
        std::cerr << "cached searches leaked base scripts between requests\n";
        return false;
    }
    return true;
}

bool CheckKeyboardSteeringBaseline(const char *packsDirectory,
                                   const char *replayPath) {
    const forevertas::InputScriptParseResult parsed =
            forevertas::ParseInputScript(
                    "0.00 press left\n"
                    "0.00 press right\n"
                    "0.10 rel left\n"
                    "0.20 steer -32768\n"
                    "0.30 press left\n"
                    "0.40 rel left\n"
                    "0.50 rel right");
    if (!parsed) {
        throw std::runtime_error(*parsed.error);
    }

    forevertas::SearchRunControl control;
    control.iterationLimit = 0u;
    control.reuseLoadedSandbox = true;
    control.sampleBestTimeline = false;
    forevertas::SearchRequest request{packsDirectory, replayPath};
    request.baseInputCommands = parsed.commands;
    const forevertas::SearchResult result =
            forevertas::RunSearch(request, &control);
    const std::string script =
            forevertas::FormatInputScript(result.bestInputs);
    const std::string expected =
            "0.00 steer -65536\n"
            "0.10 steer 65536\n"
            "0.20 steer -32768\n"
            "0.30 steer -65536\n"
            "0.40 steer 65536\n"
            "0.50 steer 0";
    if (script != expected) {
        std::cerr << "keyboard steering baseline was not converted exactly\n"
                  << "expected:\n" << expected << "\nactual:\n"
                  << script << '\n';
        return false;
    }
    return true;
}

bool CheckKeyboardSteeringPhysicsParity(const char *packsDirectory,
                                        const char *replayPath) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    const ReplayIdentity identity{replayPath};
    const AssetBytes replay = Require(
            forevertas::ReadReplayFileUtf8(replayPath, identity),
            "reading replay for keyboard parity");
    PhysicsSandboxOptions options;
    options.backend = SimulationBackend::Reference;
    options.tickDurationMs = forevertas::kSearchTickDurationMs;
    PhysicsSandbox keyboard = Require(
            CreatePhysicsSandbox(
                    Require(OpenInstalledPackDirectory(packsDirectory),
                            "opening keyboard parity Packs"),
                    options),
            "creating keyboard parity sandbox");
    PhysicsSandbox analog = Require(
            CreatePhysicsSandbox(
                    Require(OpenInstalledPackDirectory(packsDirectory),
                            "opening analog parity Packs"),
                    options),
            "creating analog parity sandbox");
    const PhysicsSandboxStateView initial = Require(
            keyboard.LoadReplay({replay.data(), replay.size()}, identity),
            "loading keyboard parity replay");
    Require(analog.LoadReplay({replay.data(), replay.size()}, identity),
            "loading analog parity replay");

    const forevertas::InputScriptParseResult parsed =
            forevertas::ParseInputScript(
                    "0.00 press left\n"
                    "0.00 press right\n"
                    "0.10 rel left\n"
                    "0.20 steer -32768\n"
                    "0.20 press right\n"
                    "0.30 press left\n"
                    "0.40 rel left\n"
                    "0.50 rel right\n"
                    "0.50 steer 500\n"
                    "0.60 steer 656");
    if (!parsed) {
        throw std::runtime_error(*parsed.error);
    }
    const std::vector<forevertas::SandboxInputEvent> replayInputs =
            Require(keyboard.ReadInputs(), "reading keyboard parity inputs");
    const forevertas::InputScriptBaselineResult materialized =
            forevertas::BuildInputScriptBaseline(
                    replayInputs,
                    parsed.commands,
                    static_cast<std::int64_t>(initial.durationMs),
                    forevertas::kSearchTickDurationMs);
    if (!materialized) {
        throw std::runtime_error(*materialized.error);
    }
    std::vector<forevertas::SandboxInputEvent> converted =
            materialized.events;
    forevertas::ConvertKeyboardSteeringToAnalog(converted);
    Require(keyboard.ReplaceInputs(materialized.events),
            "applying keyboard parity inputs");
    Require(analog.ReplaceInputs(std::move(converted)),
            "applying analog parity inputs");

    for (std::uint32_t tick = 0u; tick <= 70u; ++tick) {
        const PhysicsSandboxStateView keyboardState =
                Require(keyboard.ReadState(), "reading keyboard parity state");
        const PhysicsSandboxStateView analogState =
                Require(analog.ReadState(), "reading analog parity state");
        const bool equal =
                keyboardState.timeMs == analogState.timeMs &&
                keyboardState.steering == analogState.steering &&
                keyboardState.accelerate == analogState.accelerate &&
                keyboardState.brake == analogState.brake &&
                keyboardState.car.position.x == analogState.car.position.x &&
                keyboardState.car.position.y == analogState.car.position.y &&
                keyboardState.car.position.z == analogState.car.position.z &&
                keyboardState.car.linearSpeed.x ==
                        analogState.car.linearSpeed.x &&
                keyboardState.car.linearSpeed.y ==
                        analogState.car.linearSpeed.y &&
                keyboardState.car.linearSpeed.z ==
                        analogState.car.linearSpeed.z &&
                keyboardState.stuntsScore == analogState.stuntsScore;
        if (!equal) {
            std::cerr << "keyboard and analog simulations diverged at tick "
                      << tick << '\n';
            return false;
        }
        if (tick != 70u) {
            Require(keyboard.AdvanceTicks(1u),
                    "advancing keyboard parity sandbox");
            Require(analog.AdvanceTicks(1u),
                    "advancing analog parity sandbox");
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "expected Packs directory and replay path\n";
        return 2;
    }

    try {
        if (!CheckCachedScriptIsolation(argv[1], argv[2]) ||
            !CheckKeyboardSteeringBaseline(argv[1], argv[2]) ||
            !CheckKeyboardSteeringPhysicsParity(argv[1], argv[2]) ||
            !CheckMultiThreadedCpuBackend(argv[1], argv[2]) ||
            !CheckStuntTargetBackend(
                    argv[1],
                    argv[2],
                    forevertas::PhysicsBackend::Reference) ||
            !CheckStuntTargetBackend(
                    argv[1],
                    argv[2],
                    forevertas::PhysicsBackend::OptimizedCpu) ||
            !RunBackend(argv[1],
                        argv[2],
                        forevertas::PhysicsBackend::Reference) ||
            !RunBackend(argv[1],
                        argv[2],
                        forevertas::PhysicsBackend::OptimizedCpu)
#if FOREVERVALIDATOR_HAS_CUDA
            || !CheckStuntTargetBackend(
                    argv[1],
                    argv[2],
                    forevertas::PhysicsBackend::Cuda)
            || !RunBackend(argv[1],
                           argv[2],
                           forevertas::PhysicsBackend::Cuda)
#endif
        ) {
            return 1;
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
