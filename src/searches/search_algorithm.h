#ifndef FOREVERTAS_SEARCHES_SEARCH_ALGORITHM_H
#define FOREVERTAS_SEARCHES_SEARCH_ALGORITHM_H

#include "evaluators/candidate_evaluator.h"
#include "mutations/input_mutator.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <forevervalidator/experimental/physics_sandbox.h>

namespace forevertas {

enum class SearchWinnerSource : std::uint8_t {
    Baseline,
    Mutation,
};

struct SearchExecutionContext {
    forevervalidator::experimental::PhysicsSandbox &sandbox;
    std::uint32_t tickDurationMs;
    const InputMutator &mutator;
    const CandidateEvaluator &evaluator;
};

struct SearchResult {
    SearchWinnerSource winnerSource = SearchWinnerSource::Baseline;
    std::optional<std::uint64_t> winningAttempt;
    std::size_t winningMutationCount = 0u;
    double bestScore = 0.0;
    forevervalidator::experimental::PhysicsSandboxStateView bestState;
    std::uint64_t requestedAttempts = 0u;
    std::uint64_t executedAttempts = 0u;
    std::uint64_t skippedAttempts = 0u;
    std::uint64_t evaluatorCalls = 0u;
    std::uint64_t improvementCount = 0u;
    std::uint64_t totalMutationCount = 0u;
    std::chrono::steady_clock::duration elapsed{};
    forevervalidator::experimental::PhysicsSandboxState bestSnapshot;
};

class SearchAlgorithm {
public:
    virtual ~SearchAlgorithm() = default;
    virtual SearchResult Run(const SearchExecutionContext &context) const = 0;
};

}  // namespace forevertas

#endif
