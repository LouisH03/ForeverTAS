#ifndef FOREVERTAS_EVALUATORS_CANDIDATE_EVALUATOR_H
#define FOREVERTAS_EVALUATORS_CANDIDATE_EVALUATOR_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <forevervalidator/experimental/physics_sandbox.h>

namespace forevertas {

struct EvaluationPlan {
    std::int64_t startTimeMs = 0;
    std::int64_t endTimeMs = 0;
};

struct EvaluationSample {
    double score = 0.0;
    double timeMs = 0.0;
    std::string description;
};

class CandidateEvaluationSession {
public:
    virtual ~CandidateEvaluationSession() = default;
    virtual std::optional<EvaluationSample> Observe(
            const std::optional<
                    forevervalidator::experimental::PhysicsSandboxStateView>
                    &previous,
            const forevervalidator::experimental::PhysicsSandboxStateView
                    &current) = 0;
};

class CandidateEvaluator {
public:
    virtual ~CandidateEvaluator() = default;
    virtual EvaluationPlan Plan(std::int64_t replayDurationMs,
                                std::int64_t earliestMutationTimeMs,
                                std::uint32_t tickDurationMs) const = 0;
    virtual std::unique_ptr<CandidateEvaluationSession> CreateSession()
            const = 0;
    virtual bool IsBetter(const EvaluationSample &candidate,
                          const EvaluationSample &incumbent) const = 0;
};

}  // namespace forevertas

#endif
