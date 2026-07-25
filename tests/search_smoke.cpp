#include "mutations/input_event_formatter.h"
#include "searches/search_runner.h"

#include <chrono>
#include <exception>
#include <iostream>

namespace {

bool RunBackend(const char *packsDirectory,
                const char *replayPath,
                forevertas::PhysicsBackend backend) {
        constexpr std::uint64_t iterationsBeforeStop = 64u;
        bool stopRequested = false;
        bool sawLiveBest = false;
        bool sawFinalSampling = false;
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
        forevertas::SearchRequest request{packsDirectory, replayPath};
        request.backend = backend;
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

}  // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "expected Packs directory and replay path\n";
        return 2;
    }

    try {
        if (!RunBackend(argv[1],
                        argv[2],
                        forevertas::PhysicsBackend::Reference) ||
            !RunBackend(argv[1],
                        argv[2],
                        forevertas::PhysicsBackend::OptimizedCpu)
#if defined(FOREVERVALIDATOR_HAS_SPECULATIVE_TICKING)
            || !RunBackend(argv[1],
                           argv[2],
                           forevertas::PhysicsBackend::SpeculativeTicking)
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
