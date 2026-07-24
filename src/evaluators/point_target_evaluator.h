#ifndef FOREVERTAS_EVALUATORS_POINT_TARGET_EVALUATOR_H
#define FOREVERTAS_EVALUATORS_POINT_TARGET_EVALUATOR_H

#include "evaluators/candidate_evaluator.h"
#include "searches/option_configuration.h"

#include <memory>
#include <optional>
#include <string>

namespace forevertas {

OptionSettings DefaultPointTargetOptionSettings();
std::optional<std::string> ValidatePointTargetOptionSettings(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs);
std::unique_ptr<CandidateEvaluator> CreatePointTargetEvaluator(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs);

}  // namespace forevertas

#endif
