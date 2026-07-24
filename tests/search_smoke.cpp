#include "searches/search_runner.h"

#include <exception>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "expected Packs directory and replay path\n";
        return 2;
    }

    try {
        bool sawFinalSampling = false;
        forevertas::SearchRunControl control;
        control.progressChanged = [&sawFinalSampling](
                                          const forevertas::SearchProgress
                                                  &progress) {
            sawFinalSampling |= progress.stage ==
                    forevertas::SearchProgressStage::FinalSampling;
        };
        const forevertas::SearchResult result = forevertas::RunSearch(
                {argv[1], argv[2]}, &control);
        const bool mutationWon =
                result.winnerSource ==
                forevertas::SearchWinnerSource::Mutation;
        if (mutationWon != (result.mutationImprovementCount > 0u)) {
            std::cerr << "winner and mutation improvement count disagree\n";
            return 1;
        }
        if (result.bestInputs.empty()) {
            std::cerr << "best input timeline was not retained\n";
            return 1;
        }
        if (!sawFinalSampling || result.bestTimeline.empty()) {
            std::cerr << "best run was not sampled after search\n";
            return 1;
        }
        if (result.bestTimeline.front().timeMs != 0 ||
            result.bestTimeline.back().timeMs !=
                    static_cast<std::int64_t>(result.bestState.durationMs)) {
            std::cerr << "best run sampling did not cover the full replay\n";
            return 1;
        }
        for (std::size_t index = 1u;
             index < result.bestTimeline.size();
             ++index) {
            if (result.bestTimeline[index].timeMs -
                        result.bestTimeline[index - 1u].timeMs !=
                10) {
                std::cerr << "best run was not sampled every tick\n";
                return 1;
            }
        }
        std::cout << "winner="
                  << (mutationWon ? "Mutation" : "Baseline")
                  << " score=" << result.bestScore
                  << " improvements="
                  << result.mutationImprovementCount
                  << " attempt="
                  << (result.winningAttempt
                              ? std::to_string(*result.winningAttempt + 1u)
                              : std::string("none"))
                  << " attempts=" << result.requestedAttempts
                  << " inputs=" << result.bestInputs.size()
                  << " frames=" << result.bestTimeline.size() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
