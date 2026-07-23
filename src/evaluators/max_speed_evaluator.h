#ifndef FOREVERTAS_EVALUATORS_MAX_SPEED_EVALUATOR_H
#define FOREVERTAS_EVALUATORS_MAX_SPEED_EVALUATOR_H

#include "evaluators/candidate_evaluator.h"
#include "searches/option_configuration.h"

#include <memory>
#include <optional>
#include <string>

namespace forevertas {

OptionSettings DefaultMaxSpeedOptionSettings();
std::optional<std::string> ValidateMaxSpeedOptionSettings(
        const OptionSettings &settings);
std::unique_ptr<CandidateEvaluator> CreateMaxSpeedEvaluator(
        const OptionSettings &settings);

class MaxSpeedEvaluator final : public CandidateEvaluator {
public:
    double Evaluate(
            const forevervalidator::experimental::PhysicsSandboxStateView
                    &state) const override;
};

}  // namespace forevertas

#endif
