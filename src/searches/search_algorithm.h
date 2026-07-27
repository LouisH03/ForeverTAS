#ifndef FOREVERTAS_SEARCHES_SEARCH_ALGORITHM_H
#define FOREVERTAS_SEARCHES_SEARCH_ALGORITHM_H

#include "evaluators/iteration_evaluator.h"
#include "mutations/input_mutator.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <forevervalidator/experimental/physics_sandbox.h>

namespace forevertas {

enum class SearchWinnerSource : std::uint8_t {
    Baseline,
    Mutation,
};

enum class SearchProgressStage : std::uint8_t {
    Baseline,
    Calibration,
    Mutations,
    FinalSampling,
};

struct SearchProgress {
    SearchProgressStage stage = SearchProgressStage::Baseline;
    std::uint64_t completedWork = 0u;
    std::uint64_t totalWork = 0u;
};

struct SearchLiveUpdate {
    SearchWinnerSource winnerSource = SearchWinnerSource::Baseline;
    std::optional<std::uint64_t> winningIterationIndex;
    std::size_t winningMutationCount = 0u;
    double bestScore = 0.0;
    double bestEvaluationTimeMs = 0.0;
    std::string bestEvaluationDescription;
    forevervalidator::experimental::PhysicsSandboxStateView bestState;
    std::vector<SandboxInputEvent> bestInputs;
    std::uint64_t iterations = 0u;
    std::uint64_t evaluatorCalls = 0u;
    std::uint64_t mutationImprovementCount = 0u;
    std::uint64_t totalMutationCount = 0u;
    std::chrono::steady_clock::duration elapsed{};
    std::optional<std::chrono::steady_clock::duration>
            lastImprovementElapsed;
};

struct SearchRunControl {
    std::function<bool()> stopRequested;
    std::function<bool()> cancellationRequested;
    std::function<void(const SearchProgress &)> progressChanged;
    std::function<void(const SearchLiveUpdate &)> liveChanged;
    std::function<void(std::uint32_t)> cudaBatchSizeChanged;
    std::optional<std::uint64_t> iterationLimit;
    std::optional<std::int64_t> evaluationEndTimeLimitMs;
    bool sampleBestTimeline = true;
    bool reuseLoadedSandbox = false;
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
    const IterationEvaluator &evaluator;
    const SearchRunControl *control = nullptr;
    std::uint32_t cudaBatchSize = 1u;
    bool calibrateCudaBatchSize = false;
    const std::vector<forevervalidator::experimental::
                              PhysicsSandboxCudaModifier>
            *cudaModifiers = nullptr;
    const forevervalidator::experimental::PhysicsSandboxCudaEvaluator
            *cudaEvaluator = nullptr;
};

struct SearchTimelineFrame {
    std::int64_t timeMs = 0;
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
    float rotationX = 0.0f;
    float rotationY = 0.0f;
    float rotationZ = 0.0f;
    float rotationW = 1.0f;
    float accelerate = 0.0f;
    float brake = 0.0f;
    float steering = 0.0f;
};

struct SearchResult {
    SearchWinnerSource winnerSource = SearchWinnerSource::Baseline;
    std::optional<std::uint64_t> winningIterationIndex;
    std::size_t winningMutationCount = 0u;
    double bestScore = 0.0;
    double bestEvaluationTimeMs = 0.0;
    std::string bestEvaluationDescription;
    forevervalidator::experimental::PhysicsSandboxStateView bestState;
    std::vector<SandboxInputEvent> bestInputs;
    std::vector<SearchTimelineFrame> bestTimeline;
    std::uint64_t iterations = 0u;
    std::uint64_t evaluatorCalls = 0u;
    std::uint64_t mutationImprovementCount = 0u;
    std::uint64_t totalMutationCount = 0u;
    std::chrono::steady_clock::duration elapsed{};
    std::optional<std::chrono::steady_clock::duration>
            lastImprovementElapsed;
    forevervalidator::experimental::PhysicsSandboxState bestSnapshot;
};

class SearchAlgorithm {
public:
    virtual ~SearchAlgorithm() = default;
    virtual SearchResult Run(const SearchExecutionContext &context) const = 0;
};

}  // namespace forevertas

#endif
