#ifndef FOREVERTAS_SEARCHES_SEARCH_ALGORITHM_H
#define FOREVERTAS_SEARCHES_SEARCH_ALGORITHM_H

#include "evaluators/candidate_evaluator.h"
#include "mutations/input_mutator.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>

#include <forevervalidator/experimental/physics_sandbox.h>

namespace forevertas {

enum class SearchWinnerSource : std::uint8_t {
    Baseline,
    Mutation,
};

enum class SearchProgressStage : std::uint8_t {
    Baseline,
    Mutations,
};

struct SearchProgress {
    SearchProgressStage stage = SearchProgressStage::Baseline;
    std::uint64_t completedAttempts = 0u;
    std::uint64_t requestedAttempts = 0u;
};

struct SearchRunControl {
    std::function<bool()> cancellationRequested;
    std::function<void(const SearchProgress &)> progressChanged;
};

class SearchCancelled final : public std::exception {
public:
    const char *what() const noexcept override {
        return "search cancelled";
    }
};

struct SearchExecutionContext {
    forevervalidator::experimental::PhysicsSandbox &sandbox;
    std::uint32_t tickDurationMs;
    const InputMutator &mutator;
    const CandidateEvaluator &evaluator;
    const SearchRunControl *control = nullptr;
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
