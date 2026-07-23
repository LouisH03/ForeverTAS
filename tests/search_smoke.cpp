#include "searches/search_runner.h"

#include <exception>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "expected Packs directory and replay path\n";
        return 2;
    }

    try {
        const forevertas::SearchResult result =
                forevertas::RunSearch({
                        argv[1],
                        argv[2]});
        const bool mutationWon =
                result.winnerSource ==
                forevertas::SearchWinnerSource::Mutation;
        if (mutationWon != (result.mutationImprovementCount > 0u)) {
            std::cerr << "winner and mutation improvement count disagree\n";
            return 1;
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
                  << " attempts=" << result.requestedAttempts << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
