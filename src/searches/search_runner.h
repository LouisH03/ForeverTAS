#ifndef FOREVERTAS_SEARCHES_SEARCH_RUNNER_H
#define FOREVERTAS_SEARCHES_SEARCH_RUNNER_H

#include "input_timeline_time.h"
#include "physics_backend.h"
#include "searches/algorithm_registry.h"
#include "searches/search_algorithm.h"

#include <string>
#include <vector>

namespace forevertas {

inline constexpr std::uint32_t kSearchTickDurationMs =
        kInputTimelineTickDurationMs;

struct SearchRequest {
    std::string packDirectory;
    std::string replayPath;
    PhysicsBackend backend = PhysicsBackend::Reference;
    OptionConfiguration searchAlgorithm =
            DefaultSearchAlgorithmConfiguration();
    std::vector<OptionConfiguration> modifiers =
            DefaultModifierConfigurations();
    OptionConfiguration evaluationTarget =
            DefaultEvaluationTargetConfiguration();
};

SearchResult RunSearch(
        const SearchRequest &request,
        const SearchRunControl *control = nullptr);

}  // namespace forevertas

#endif
