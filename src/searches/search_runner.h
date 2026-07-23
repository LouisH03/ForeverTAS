#ifndef FOREVERTAS_SEARCHES_SEARCH_RUNNER_H
#define FOREVERTAS_SEARCHES_SEARCH_RUNNER_H

#include "searches/algorithm_registry.h"
#include "searches/search_algorithm.h"

#include <string>

namespace forevertas {

inline constexpr std::uint32_t kSearchTickDurationMs = 10u;

struct SearchRequest {
    std::string packDirectory;
    std::string replayPath;
    OptionConfiguration searchAlgorithm =
            DefaultSearchAlgorithmConfiguration();
    OptionConfiguration mutationAlgorithm =
            DefaultMutationAlgorithmConfiguration();
    OptionConfiguration evaluationTarget =
            DefaultEvaluationTargetConfiguration();
};

SearchResult RunSearch(
        const SearchRequest &request,
        const SearchRunControl *control = nullptr);

}  // namespace forevertas

#endif
