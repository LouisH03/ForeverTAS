#include "mutations/existing_event_perturbation_mutator.h"

#include "mutations/input_event_utils.h"
#include "mutations/modifier_utils.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace forevertas {
namespace {

struct Settings {
    ModifierWindow window;
    std::uint32_t minimumCount = 1u;
    std::uint32_t maximumCount = 1u;
    std::int64_t maximumTimeShiftMs = 0;
    bool absoluteSteering = false;
    double steeringDeltaMinimum = -0.1;
    double steeringDeltaMaximum = 0.1;
    double steeringAbsoluteMinimum = -1.0;
    double steeringAbsoluteMaximum = 1.0;
    bool toggleAccelerate = true;
    bool toggleBrake = true;
};

std::optional<Settings> ParseSettings(const OptionSettings &settings) {
    const auto window = ParseModifierWindow(settings);
    const auto minimumCount = ParseUnsignedDecimal32(settings.at("minCount"));
    const auto maximumCount = ParseUnsignedDecimal32(settings.at("maxCount"));
    const auto maximumShift = ParseSignedDecimal(settings.at("maxTimeShiftMs"));
    const auto deltaMinimum = ParseFiniteDouble(settings.at("steerDeltaMin"));
    const auto deltaMaximum = ParseFiniteDouble(settings.at("steerDeltaMax"));
    const auto absoluteMinimum = ParseFiniteDouble(settings.at("steerAbsoluteMin"));
    const auto absoluteMaximum = ParseFiniteDouble(settings.at("steerAbsoluteMax"));
    const auto toggleAccelerate = ParseBoolean(settings.at("toggleAccelerate"));
    const auto toggleBrake = ParseBoolean(settings.at("toggleBrake"));
    const std::string &mode = settings.at("steerMode");
    if (!window || !minimumCount || !maximumCount || !maximumShift ||
        !deltaMinimum || !deltaMaximum || !absoluteMinimum ||
        !absoluteMaximum || !toggleAccelerate || !toggleBrake ||
        (mode != "delta" && mode != "absolute")) {
        return std::nullopt;
    }
    return Settings{*window,
                    *minimumCount,
                    *maximumCount,
                    *maximumShift,
                    mode == "absolute",
                    *deltaMinimum,
                    *deltaMaximum,
                    *absoluteMinimum,
                    *absoluteMaximum,
                    *toggleAccelerate,
                    *toggleBrake};
}

class ExistingEventPerturbationMutator final : public InputMutator {
public:
    explicit ExistingEventPerturbationMutator(Settings settings)
        : settings_(settings) {}

    MutationResult Mutate(const MutationRequest &request) const override {
        std::vector<SandboxInputEvent> inputs = request.baselineInputs;
        std::vector<std::size_t> candidates;
        for (std::size_t index = 0u; index < inputs.size(); ++index) {
            const SandboxInputEvent &event = inputs[index];
            if (event.timeMs < settings_.window.minimumTimeMs ||
                event.timeMs > settings_.window.maximumTimeMs) continue;
            if ((IsSteerAction(event.action) &&
                 event.value.kind == forevervalidator::experimental::
                         PhysicsSandboxInputValueKind::Analog) ||
                (settings_.toggleAccelerate && IsAccelerateAction(event.action)) ||
                (settings_.toggleBrake && IsBrakeAction(event.action))) {
                candidates.push_back(index);
            }
        }
        if (candidates.empty()) return {inputs, 0u};

        std::mt19937 random = ModifierRandom(
                settings_.window.seed, request.attemptIndex, request.passIndex);
        std::shuffle(candidates.begin(), candidates.end(), random);
        const std::uint32_t requested = RandomInteger(
                random, settings_.minimumCount, settings_.maximumCount);
        const std::size_t count = std::min<std::size_t>(requested,
                                                       candidates.size());
        const std::int64_t tick = request.tickDurationMs;
        const std::int64_t maximumShiftTicks = tick == 0
                ? 0
                : settings_.maximumTimeShiftMs / tick;
        for (std::size_t n = 0u; n < count; ++n) {
            SandboxInputEvent &event = inputs[candidates[n]];
            const std::int64_t shiftTicks = RandomInteger<std::int64_t>(
                    random, -maximumShiftTicks, maximumShiftTicks);
            event.timeMs = std::clamp(
                    event.timeMs + shiftTicks * tick,
                    settings_.window.minimumTimeMs,
                    settings_.window.maximumTimeMs);
            if (IsSteerAction(event.action)) {
                const double value = settings_.absoluteSteering
                        ? RandomDouble(random,
                                       settings_.steeringAbsoluteMinimum,
                                       settings_.steeringAbsoluteMaximum)
                        : static_cast<double>(event.value.analog) +
                                  RandomDouble(random,
                                               settings_.steeringDeltaMinimum,
                                               settings_.steeringDeltaMaximum);
                event.value.analog = ClampSteering(static_cast<float>(value));
            } else if (event.value.kind == forevervalidator::experimental::
                               PhysicsSandboxInputValueKind::Switch) {
                event.value.switchState =
                        event.value.switchState !=
                                        forevervalidator::experimental::
                                                PhysicsSandboxSwitchState::
                                                        Released
                        ? forevervalidator::experimental::
                                  PhysicsSandboxSwitchState::Released
                        : forevervalidator::experimental::
                                  PhysicsSandboxSwitchState::Pressed;
            }
        }
        NormalizeInputEvents(inputs, request.tickDurationMs);
        return {inputs,
                EffectiveInputChangeCount(request.baselineInputs, inputs)};
    }

    std::int64_t EarliestMutationTimeMs() const override {
        return settings_.window.minimumTimeMs;
    }

private:
    Settings settings_;
};

}  // namespace

OptionSettings DefaultExistingEventPerturbationSettings() {
    return {{"minTimeMs", "1000"},
            {"maxTimeMs", "6000"},
            {"seed", "1179926867"},
            {"minCount", "1"},
            {"maxCount", "3"},
            {"maxTimeShiftMs", "100"},
            {"steerMode", "delta"},
            {"steerDeltaMin", "-0.15"},
            {"steerDeltaMax", "0.15"},
            {"steerAbsoluteMin", "-1"},
            {"steerAbsoluteMax", "1"},
            {"toggleAccelerate", "true"},
            {"toggleBrake", "true"}};
}

std::optional<std::string> ValidateExistingEventPerturbationSettings(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs) {
    if (const auto error = ValidateOptionSettingKeys(
                settings, DefaultExistingEventPerturbationSettings())) {
        return error;
    }
    const auto parsed = ParseSettings(settings);
    if (!parsed) return "existing-event perturbation settings are invalid";
    if (const auto error = ValidateModifierWindow(parsed->window,
                                                   tickDurationMs)) {
        return error;
    }
    if (parsed->minimumCount > parsed->maximumCount) {
        return "minimum perturbation count must not exceed maximum";
    }
    if (parsed->maximumTimeShiftMs < 0 ||
        parsed->maximumTimeShiftMs % tickDurationMs != 0) {
        return "maximum timing shift must be a non-negative whole-tick value";
    }
    if (parsed->steeringDeltaMinimum > parsed->steeringDeltaMaximum ||
        parsed->steeringAbsoluteMinimum > parsed->steeringAbsoluteMaximum ||
        parsed->steeringAbsoluteMinimum < -1.0 ||
        parsed->steeringAbsoluteMaximum > 1.0) {
        return "steering ranges are invalid";
    }
    return std::nullopt;
}

std::unique_ptr<InputMutator> CreateExistingEventPerturbationMutator(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs) {
    if (const auto error = ValidateExistingEventPerturbationSettings(
                settings, tickDurationMs)) {
        throw std::invalid_argument(*error);
    }
    return std::make_unique<ExistingEventPerturbationMutator>(
            *ParseSettings(settings));
}

}  // namespace forevertas
