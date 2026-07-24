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
    AnalogInputState steeringDeltaMinimum = 0;
    AnalogInputState steeringDeltaMaximum = 0;
    AnalogInputState steeringAbsoluteMinimum = kAnalogInputMinimum;
    AnalogInputState steeringAbsoluteMaximum = kAnalogInputMaximum;
    bool toggleAccelerate = true;
    bool toggleBrake = true;
};

std::optional<Settings> ParseSettings(const OptionSettings &settings) {
    const auto window = ParseModifierWindow(settings);
    const auto minimumCount = ParseUnsignedDecimal32(settings.at("minCount"));
    const auto maximumCount = ParseUnsignedDecimal32(settings.at("maxCount"));
    const auto maximumShift = ParseSignedDecimal(settings.at("maxTimeShiftMs"));
    const auto deltaMinimum =
            ParseNormalizedAnalogInput(settings.at("steerDeltaMin"));
    const auto deltaMaximum =
            ParseNormalizedAnalogInput(settings.at("steerDeltaMax"));
    const auto absoluteMinimum =
            ParseNormalizedAnalogInput(settings.at("steerAbsoluteMin"));
    const auto absoluteMaximum =
            ParseNormalizedAnalogInput(settings.at("steerAbsoluteMax"));
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
        std::vector<std::size_t> eligibleIndices;
        for (std::size_t index = 0u; index < inputs.size(); ++index) {
            const SandboxInputEvent &event = inputs[index];
            if (event.timeMs < settings_.window.minimumTimeMs ||
                event.timeMs > settings_.window.maximumTimeMs) {
                continue;
            }
            if ((IsSteerAction(event.action) &&
                 event.value.kind == forevervalidator::experimental::
                         PhysicsSandboxInputValueKind::Analog) ||
                (settings_.toggleAccelerate &&
                 IsAccelerateAction(event.action)) ||
                (settings_.toggleBrake && IsBrakeAction(event.action))) {
                eligibleIndices.push_back(index);
            }
        }
        if (eligibleIndices.empty()) return {inputs, 0u};

        std::mt19937 random = ModifierRandom(
                settings_.window.seed, request.iterationIndex, request.passIndex);
        std::shuffle(eligibleIndices.begin(), eligibleIndices.end(), random);
        const std::uint32_t requested = RandomInteger(
                random, settings_.minimumCount, settings_.maximumCount);
        const std::size_t count = std::min<std::size_t>(
                requested, eligibleIndices.size());
        const std::int64_t tick = request.tickDurationMs;
        const std::int64_t maximumShiftTicks = tick == 0
                ? 0
                : settings_.maximumTimeShiftMs / tick;
        for (std::size_t n = 0u; n < count; ++n) {
            SandboxInputEvent &event = inputs[eligibleIndices[n]];
            const std::int64_t shiftTicks = RandomInteger<std::int64_t>(
                    random, -maximumShiftTicks, maximumShiftTicks);
            event.timeMs = static_cast<std::int32_t>(std::clamp<std::int64_t>(
                    static_cast<std::int64_t>(event.timeMs) +
                            shiftTicks * tick,
                    settings_.window.minimumTimeMs,
                    settings_.window.maximumTimeMs));
            if (IsSteerAction(event.action)) {
                if (settings_.absoluteSteering) {
                    event.value.analog = RandomInteger<AnalogInputState>(
                            random,
                            settings_.steeringAbsoluteMinimum,
                            settings_.steeringAbsoluteMaximum);
                } else {
                    const AnalogInputState delta =
                            RandomInteger<AnalogInputState>(
                                    random,
                                    settings_.steeringDeltaMinimum,
                                    settings_.steeringDeltaMaximum);
                    event.value.analog = SaturateAnalogInputState(
                            static_cast<std::int64_t>(event.value.analog) +
                            delta);
                }
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
        NormalizeMutableInputEvents(inputs,
                                    request.baselineInputs,
                                    request.tickDurationMs,
                                    request.mutableFromTimeMs);
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
        parsed->steeringAbsoluteMinimum >
                parsed->steeringAbsoluteMaximum) {
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
