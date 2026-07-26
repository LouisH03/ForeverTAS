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
inline constexpr std::uint32_t kDefaultCudaParallelSampleCount = 256u;

struct SearchRequest {
    std::string packDirectory;
    std::string replayPath;
    PhysicsBackend backend = PhysicsBackend::Reference;
    std::uint32_t parallelSampleCount = 1u;
    bool calibrateCudaParallelSampleCount = false;
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
