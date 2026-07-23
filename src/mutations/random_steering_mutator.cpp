#include "mutations/random_steering_mutator.h"

#include "searches/option_settings_utils.h"

#include <limits>
#include <random>
#include <stdexcept>

namespace forevertas {
namespace {

float RandomSteering(std::mt19937 &random) {
    const double unit = static_cast<double>(random()) /
                        static_cast<double>(
                                std::numeric_limits<std::uint32_t>::max());
    return static_cast<float>(unit * 2.0 - 1.0);
}

std::optional<RandomSteeringSettings> ParseRandomSteeringSettings(
        const OptionSettings &settings,
        std::string *error) {
    const OptionSettings defaults = DefaultRandomSteeringOptionSettings();
    if (const auto keyError = ValidateOptionSettingKeys(settings, defaults)) {
        *error = *keyError;
        return std::nullopt;
    }
    const auto seed = ParseUnsignedDecimal32(settings.at("seed"));
    if (!seed) {
        *error = "mutation seed must be an unsigned 32-bit decimal integer";
        return std::nullopt;
    }
    return RandomSteeringSettings{*seed};
}

}  // namespace

RandomSteeringSettings DefaultRandomSteeringSettings() {
    return {1179926867u};
}

OptionSettings DefaultRandomSteeringOptionSettings() {
    return {{"seed", std::to_string(DefaultRandomSteeringSettings().seed)}};
}

std::optional<std::string> ValidateRandomSteeringOptionSettings(
        const OptionSettings &settings) {
    std::string error;
    static_cast<void>(ParseRandomSteeringSettings(settings, &error));
    return error.empty() ? std::nullopt
                         : std::optional<std::string>(std::move(error));
}

std::unique_ptr<InputMutator> CreateRandomSteeringMutator(
        const OptionSettings &settings) {
    std::string error;
    const auto parsed = ParseRandomSteeringSettings(settings, &error);
    if (!parsed) {
        throw std::invalid_argument(error);
    }
    return std::make_unique<RandomSteeringMutator>(*parsed);
}

RandomSteeringMutator::RandomSteeringMutator(
        RandomSteeringSettings settings)
    : settings_(settings) {}

MutationResult RandomSteeringMutator::Mutate(
        const MutationRequest &request) const {
    using forevervalidator::experimental::PhysicsSandboxInputAction;
    using forevervalidator::experimental::PhysicsSandboxInputEvent;
    using forevervalidator::experimental::PhysicsSandboxInputValueKind;

    std::seed_seq sequence{
            settings_.seed,
            static_cast<std::uint32_t>(request.attemptIndex),
            static_cast<std::uint32_t>(request.attemptIndex >> 32u)};
    std::mt19937 random(sequence);

    MutationResult result;
    result.inputs = request.baselineInputs;
    for (PhysicsSandboxInputEvent &event : result.inputs) {
        if (event.timeMs < request.minMutateMs ||
            event.timeMs > request.maxMutateMs ||
            event.action != PhysicsSandboxInputAction::Steer ||
            event.value.kind != PhysicsSandboxInputValueKind::Analog) {
            continue;
        }

        float value = RandomSteering(random);
        if (value == event.value.analog) {
            value = value == 1.0f ? -1.0f : 1.0f;
        }
        event.value.analog = value;
        ++result.mutationCount;
    }
    return result;
}

}  // namespace forevertas
