#ifndef FOREVERTAS_EVALUATORS_POSE_TARGET_EVALUATOR_H
#define FOREVERTAS_EVALUATORS_POSE_TARGET_EVALUATOR_H

#include "evaluators/candidate_evaluator.h"
#include "searches/option_configuration.h"

#include <memory>
#include <optional>
#include <string>

namespace forevertas {

OptionSettings DefaultPoseTargetOptionSettings();
std::optional<std::string> ValidatePoseTargetOptionSettings(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs);
std::unique_ptr<CandidateEvaluator> CreatePoseTargetEvaluator(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs);

}  // namespace forevertas

#endif
