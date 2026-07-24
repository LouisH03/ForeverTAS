#ifndef FOREVERTAS_EVALUATORS_FINISH_TIME_EVALUATOR_H
#define FOREVERTAS_EVALUATORS_FINISH_TIME_EVALUATOR_H

#include "evaluators/candidate_evaluator.h"
#include "searches/option_configuration.h"

#include <memory>
#include <optional>
#include <string>

namespace forevertas {

OptionSettings DefaultFinishTimeOptionSettings();
std::optional<std::string> ValidateFinishTimeOptionSettings(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs);
std::unique_ptr<CandidateEvaluator> CreateFinishTimeEvaluator(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs);

class FinishTimeEvaluator final : public CandidateEvaluator {
public:
    EvaluationPlan Plan(std::int64_t replayDurationMs,
                        std::int64_t earliestMutationTimeMs,
                        std::uint32_t tickDurationMs) const override;
    std::unique_ptr<CandidateEvaluationSession> CreateSession()
            const override;
    bool IsBetter(const EvaluationSample &candidate,
                  const EvaluationSample &incumbent) const override;
};

}  // namespace forevertas

#endif
