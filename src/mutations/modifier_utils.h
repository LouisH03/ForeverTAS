#ifndef FOREVERTAS_MUTATIONS_MODIFIER_UTILS_H
#define FOREVERTAS_MUTATIONS_MODIFIER_UTILS_H

#include "mutations/input_event_utils.h"
#include "searches/option_configuration.h"
#include "searches/option_settings_utils.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <string>

namespace forevertas {

struct ModifierWindow {
    std::int64_t minimumTimeMs = 0;
    std::int64_t maximumTimeMs = 0;
    std::uint32_t seed = 0u;
};

inline std::mt19937 ModifierRandom(std::uint32_t seed,
                                  std::uint64_t attemptIndex,
                                  std::uint32_t passIndex) {
    std::seed_seq sequence{
            seed,
            static_cast<std::uint32_t>(attemptIndex),
            static_cast<std::uint32_t>(attemptIndex >> 32u),
            passIndex};
    return std::mt19937(sequence);
}

template<typename Integer>
Integer RandomInteger(std::mt19937 &random, Integer minimum, Integer maximum) {
    if (minimum > maximum) std::swap(minimum, maximum);
    std::uniform_int_distribution<Integer> distribution(minimum, maximum);
    return distribution(random);
}

inline double RandomDouble(std::mt19937 &random,
                           double minimum,
                           double maximum) {
    if (minimum > maximum) std::swap(minimum, maximum);
    std::uniform_real_distribution<double> distribution(minimum, maximum);
    return distribution(random);
}

inline std::optional<ModifierWindow> ParseModifierWindow(
        const OptionSettings &settings) {
    const auto minimum = ParseSignedDecimal(settings.at("minTimeMs"));
    const auto maximum = ParseSignedDecimal(settings.at("maxTimeMs"));
    const auto seed = ParseUnsignedDecimal32(settings.at("seed"));
    if (!minimum || !maximum || !seed) return std::nullopt;
    return ModifierWindow{*minimum, *maximum, *seed};
}

inline std::optional<std::string> ValidateModifierWindow(
        const ModifierWindow &window,
        std::uint32_t tickDurationMs) {
    return ValidateTimeWindow(window.minimumTimeMs,
                              window.maximumTimeMs,
                              tickDurationMs,
                              "mutation");
}

inline bool IsAccelerateAction(SandboxInputAction action) {
    return action == SandboxInputAction::Accelerate ||
           action == SandboxInputAction::Gas;
}

inline bool IsBrakeAction(SandboxInputAction action) {
    return action == SandboxInputAction::Brake;
}

inline bool IsSteerAction(SandboxInputAction action) {
    return action == SandboxInputAction::Steer;
}

inline SandboxInputEvent AnalogEvent(std::int64_t timeMs,
                                     SandboxInputAction action,
                                     float value) {
    SandboxInputEvent event;
    event.timeMs = timeMs;
    event.action = action;
    event.value.kind = forevervalidator::experimental::
            PhysicsSandboxInputValueKind::Analog;
    event.value.analog = value;
    return event;
}

inline SandboxInputEvent SwitchEvent(std::int64_t timeMs,
                                     SandboxInputAction action,
                                     bool active) {
    SandboxInputEvent event;
    event.timeMs = timeMs;
    event.action = action;
    event.value.kind = forevervalidator::experimental::
            PhysicsSandboxInputValueKind::Switch;
    event.value.switchState = active
            ? forevervalidator::experimental::
                      PhysicsSandboxSwitchState::Pressed
            : forevervalidator::experimental::
                      PhysicsSandboxSwitchState::Released;
    return event;
}

}  // namespace forevertas

#endif
