#include "evaluators/max_speed_evaluator.h"

#include "searches/option_settings_utils.h"

#include <cmath>
#include <stdexcept>

namespace forevertas {

OptionSettings DefaultMaxSpeedOptionSettings() {
    return {};
}

std::optional<std::string> ValidateMaxSpeedOptionSettings(
        const OptionSettings &settings) {
    return ValidateOptionSettingKeys(settings, DefaultMaxSpeedOptionSettings());
}

std::unique_ptr<CandidateEvaluator> CreateMaxSpeedEvaluator(
        const OptionSettings &settings) {
    if (const auto error = ValidateMaxSpeedOptionSettings(settings)) {
        throw std::invalid_argument(*error);
    }
    return std::make_unique<MaxSpeedEvaluator>();
}

double MaxSpeedEvaluator::Evaluate(
        const forevervalidator::experimental::PhysicsSandboxStateView &state)
        const {
    const auto &speed = state.car.linearSpeed;
    return std::hypot(static_cast<double>(speed.x),
                      static_cast<double>(speed.y),
                      static_cast<double>(speed.z));
}

}  // namespace forevertas
