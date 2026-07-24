#include "evaluators/candidate_evaluator.h"
#include "mutations/composite_input_mutator.h"
#include "mutations/input_event_formatter.h"
#include "mutations/input_event_utils.h"
#include "searches/algorithm_registry.h"
#include "searches/basic_brute_force_search.h"
#include "searches/option_settings_utils.h"
#include "searches/search_runner.h"

#include <clocale>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using forevertas::EvaluationSample;
using forevertas::InputMutator;
using forevertas::MutationRequest;
using forevertas::MutationResult;
using forevertas::OptionSettings;
using forevertas::SandboxInputAction;
using forevertas::SandboxInputEvent;
using forevervalidator::experimental::PhysicsSandboxInputValueKind;
using forevervalidator::experimental::PhysicsSandboxStateView;
using forevervalidator::experimental::PhysicsSandboxSwitchState;

bool Check(bool condition, const char *message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

class NumericLocaleGuard final {
public:
    NumericLocaleGuard() {
        if (const char *const current = std::setlocale(LC_NUMERIC, nullptr)) {
            original_ = current;
        }
    }

    ~NumericLocaleGuard() {
        if (!original_.empty()) {
            std::setlocale(LC_NUMERIC, original_.c_str());
        }
    }

    bool ActivateCommaDecimalLocale() {
        constexpr const char *candidates[] = {
                "fr_FR.utf8", "fr_FR.UTF-8", "de_DE.utf8", "de_DE.UTF-8"};
        for (const char *const candidate : candidates) {
            if (std::setlocale(LC_NUMERIC, candidate) == nullptr) continue;
            const lconv *const details = std::localeconv();
            if (details != nullptr && details->decimal_point != nullptr &&
                std::string(details->decimal_point) == ",") {
                return true;
            }
        }
        return false;
    }

private:
    std::string original_;
};

void ApplyOverrides(
        OptionSettings *settings,
        std::initializer_list<std::pair<const char *, const char *>> overrides) {
    for (const auto &[key, value] : overrides) {
        (*settings)[key] = value;
    }
}

bool ModifierAcceptsDotDecimals(
        const char *id,
        std::initializer_list<std::pair<const char *, const char *>> overrides) {
    const auto *const registration = forevertas::FindModifier(id);
    if (registration == nullptr) return false;
    OptionSettings settings = registration->defaultSettings;
    ApplyOverrides(&settings, overrides);
    return !registration->validateSettings(settings, 10u) &&
            registration->create(settings, 10u) != nullptr;
}

bool EvaluatorAcceptsDotDecimals(
        const char *id,
        std::initializer_list<std::pair<const char *, const char *>> overrides) {
    const auto *const registration = forevertas::FindEvaluationTarget(id);
    if (registration == nullptr) return false;
    OptionSettings settings = registration->defaultSettings;
    ApplyOverrides(&settings, overrides);
    return !registration->validateSettings(settings, 10u) &&
            registration->create(settings, 10u) != nullptr;
}

SandboxInputEvent Steering(std::int32_t timeMs, float value) {
    SandboxInputEvent event;
    event.timeMs = timeMs;
    event.action = SandboxInputAction::Steer;
    event.value.kind = PhysicsSandboxInputValueKind::Analog;
    event.value.analog = value;
    return event;
}

SandboxInputEvent Switch(std::int32_t timeMs,
                         SandboxInputAction action,
                         bool pressed) {
    SandboxInputEvent event;
    event.timeMs = timeMs;
    event.action = action;
    event.value.kind = PhysicsSandboxInputValueKind::Switch;
    event.value.switchState = pressed
            ? PhysicsSandboxSwitchState::Pressed
            : PhysicsSandboxSwitchState::Released;
    return event;
}

bool SameEvents(const std::vector<SandboxInputEvent> &left,
                const std::vector<SandboxInputEvent> &right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0u; index < left.size(); ++index) {
        if (!forevertas::SameInputEvent(left[index], right[index])) return false;
    }
    return true;
}

std::unique_ptr<forevertas::CandidateEvaluator> Evaluator(
        const char *id,
        const OptionSettings *overrideSettings = nullptr) {
    const auto *const registration = forevertas::FindEvaluationTarget(id);
    if (registration == nullptr) return {};
    const OptionSettings &settings = overrideSettings == nullptr
            ? registration->defaultSettings
            : *overrideSettings;
    return registration->create(settings, 10u);
}

bool TestEvaluationTargets() {
    bool okay = true;

    {
        auto evaluator = Evaluator(forevertas::kVelocityEvaluationId);
        auto session = evaluator->CreateSession();
        PhysicsSandboxStateView state;
        state.timeMs = 1000u;
        state.car.linearSpeed = {3.0f, 4.0f, 12.0f};
        const auto sample = session->Observe(std::nullopt, state);
        okay &= Check(sample && std::abs(sample->score - 13.0) < 1e-9,
                      "velocity target returned the wrong total speed");
    }

    {
        OptionSettings settings =
                forevertas::FindEvaluationTarget(
                        forevertas::kVelocityEvaluationId)->defaultSettings;
        settings["mode"] = "projected";
        settings["directionX"] = "1";
        settings["directionY"] = "0";
        settings["directionZ"] = "0";
        settings["alignmentEnabled"] = "true";
        settings["minAlignmentPercent"] = "80";
        auto evaluator = Evaluator(
                forevertas::kVelocityEvaluationId, &settings);
        auto session = evaluator->CreateSession();
        PhysicsSandboxStateView state;
        state.timeMs = 1000u;
        state.car.linearSpeed = {10.0f, 1.0f, 0.0f};
        const auto accepted = session->Observe(std::nullopt, state);
        state.car.linearSpeed = {1.0f, 10.0f, 0.0f};
        const auto rejected = session->Observe(std::nullopt, state);
        okay &= Check(accepted && std::abs(accepted->score - 10.0) < 1e-9,
                      "projected velocity was incorrect");
        okay &= Check(!rejected,
                      "velocity alignment threshold did not reject a sample");
    }

    {
        OptionSettings settings =
                forevertas::FindEvaluationTarget(
                        forevertas::kPointTargetEvaluationId)->defaultSettings;
        settings["x"] = "5";
        auto evaluator = Evaluator(
                forevertas::kPointTargetEvaluationId, &settings);
        auto session = evaluator->CreateSession();
        PhysicsSandboxStateView state;
        state.timeMs = 1000u;
        state.car.position = {2.0f, 0.0f, 0.0f};
        const auto sample = session->Observe(std::nullopt, state);
        okay &= Check(sample && std::abs(sample->score - 3.0) < 1e-9,
                      "point target returned the wrong distance");
    }

    {
        auto evaluator = Evaluator(forevertas::kPoseTargetEvaluationId);
        auto session = evaluator->CreateSession();
        PhysicsSandboxStateView state;
        state.timeMs = 1000u;
        state.car.rotationW = 1.0f;
        const auto sample = session->Observe(std::nullopt, state);
        okay &= Check(sample && std::abs(sample->score) < 1e-9,
                      "matching pose did not produce zero error");
    }

    {
        OptionSettings settings =
                forevertas::FindEvaluationTarget(
                        forevertas::kVolumeEntryEvaluationId)->defaultSettings;
        settings["sizeX"] = "2";
        settings["sizeY"] = "2";
        settings["sizeZ"] = "2";
        auto evaluator = Evaluator(
                forevertas::kVolumeEntryEvaluationId, &settings);
        auto session = evaluator->CreateSession();
        PhysicsSandboxStateView previous;
        previous.timeMs = 100u;
        previous.car.position = {-10.0f, 0.0f, 0.0f};
        PhysicsSandboxStateView current = previous;
        current.timeMs = 110u;
        current.car.position = {0.0f, 0.0f, 0.0f};
        const auto sample = session->Observe(previous, current);
        okay &= Check(sample && std::abs(sample->timeMs - 109.0) < 1e-9,
                      "volume entry interpolation was incorrect");
    }

    {
        auto evaluator = Evaluator(forevertas::kFinishTimeEvaluationId);
        auto session = evaluator->CreateSession();
        PhysicsSandboxStateView previous;
        previous.timeMs = 1230u;
        PhysicsSandboxStateView current = previous;
        current.timeMs = 1240u;
        current.raceCompleted = true;
        current.finishTimeMs = 1234u;
        const auto sample = session->Observe(previous, current);
        okay &= Check(sample && sample->timeMs == 1234.0,
                      "finish target ignored the recorded finish time");
    }

    return okay;
}

class AppendSteeringMutator final : public InputMutator {
public:
    explicit AppendSteeringMutator(float value) : value_(value) {}

    MutationResult Mutate(const MutationRequest &request) const override {
        std::vector<SandboxInputEvent> inputs = request.baselineInputs;
        inputs.push_back(Steering(1000, value_));
        return {inputs, 1u};
    }

    std::int64_t EarliestMutationTimeMs() const override { return 1000; }

private:
    float value_;
};

class NoOpMutator final : public InputMutator {
public:
    MutationResult Mutate(const MutationRequest &request) const override {
        return {request.baselineInputs, 0u};
    }
    std::int64_t EarliestMutationTimeMs() const override { return 1000; }
};

bool TestModifierComposition() {
    const std::vector<SandboxInputEvent> baseline{Steering(500, 0.1f)};
    std::vector<std::unique_ptr<InputMutator>> passes;
    passes.push_back(std::make_unique<AppendSteeringMutator>(0.2f));
    passes.push_back(std::make_unique<AppendSteeringMutator>(0.7f));
    forevertas::CompositeInputMutator composite(std::move(passes));
    const MutationResult result = composite.Mutate({baseline, 4u, 0u, 10u});
    bool okay = Check(result.mutationCount > 0u,
                      "composed modifiers reported no effective change");
    okay &= Check(result.inputs.size() == 2u,
                  "normalization did not merge same-tick steering events");
    okay &= Check(result.inputs.back().timeMs == 1000 &&
                          result.inputs.back().value.analog == 0.7f,
                  "normalization did not keep the last pass value");

    std::vector<std::unique_ptr<InputMutator>> noOpPasses;
    noOpPasses.push_back(std::make_unique<NoOpMutator>());
    forevertas::CompositeInputMutator noOp(std::move(noOpPasses));
    const MutationResult unchanged = noOp.Mutate({baseline, 0u, 0u, 10u});
    okay &= Check(unchanged.mutationCount == 0u &&
                          SameEvents(unchanged.inputs, baseline),
                  "no-op composition was not recognized");
    return okay;
}

bool TestModifierDeterminism() {
    const auto *const registration = forevertas::FindModifier(
            forevertas::kRandomSteeringModifierId);
    if (registration == nullptr) {
        return Check(false, "random steering modifier was not registered");
    }
    OptionSettings settings = registration->defaultSettings;
    settings["minTimeMs"] = "1000";
    settings["maxTimeMs"] = "2000";
    std::unique_ptr<InputMutator> modifier = registration->create(settings, 10u);
    const std::vector<SandboxInputEvent> baseline{
            Steering(990, -0.1f),
            Steering(1000, -0.2f),
            Steering(1500, 0.3f),
            Steering(2000, 0.4f),
            Steering(2010, 0.5f)};
    const MutationResult first = modifier->Mutate({baseline, 7u, 0u, 10u});
    const MutationResult repeated = modifier->Mutate({baseline, 7u, 0u, 10u});
    const MutationResult otherAttempt = modifier->Mutate(
            {baseline, 8u, 0u, 10u});
    bool okay = Check(SameEvents(first.inputs, repeated.inputs),
                      "same seed and attempt were not deterministic");
    okay &= Check(!SameEvents(first.inputs, otherAttempt.inputs),
                  "different attempts produced identical candidates");
    okay &= Check(forevertas::SameInputEvent(first.inputs.front(),
                                             baseline.front()) &&
                          forevertas::SameInputEvent(first.inputs.back(),
                                                     baseline.back()),
                  "modifier changed events outside its window");
    return okay;
}

bool TestTmInterfaceInputFormatting() {
    NumericLocaleGuard locale;
    if (!locale.ActivateCommaDecimalLocale()) {
        return Check(false, "no comma-decimal locale is installed for testing");
    }
    const std::vector<SandboxInputEvent> inputs{
            Switch(0, SandboxInputAction::Accelerate, true),
            Steering(10, -0.5f),
            Switch(1230, SandboxInputAction::Brake, false),
            Switch(1240, SandboxInputAction::SteerLeft, true),
            Switch(1250, SandboxInputAction::Respawn, true),
            Switch(1260, SandboxInputAction::RaceRunning, true)};
    const std::string formatted =
            forevertas::FormatTmInterfaceInputs(inputs);
    return Check(
            formatted ==
                    "0.00 press up\n"
                    "0.01 steer -32768\n"
                    "1.23 release down\n"
                    "1.24 press left\n"
                    "1.25 press enter",
            "TMInterface input formatting was incorrect or locale-sensitive");
}

bool TestRegistries() {
    bool okay = true;
    for (const auto &registration : forevertas::SearchAlgorithmRegistry()) {
        okay &= Check(!registration.settingsComponent.empty(),
                      "search option is missing its QML component");
        okay &= Check(!registration.validateSettings(
                              registration.defaultSettings, 10u),
                      "search option defaults are invalid");
        okay &= Check(registration.create(
                              registration.defaultSettings, 10u) != nullptr,
                      "search option factory returned null");
    }
    for (const auto &registration : forevertas::ModifierRegistry()) {
        okay &= Check(!registration.settingsComponent.empty(),
                      "modifier is missing its QML component");
        okay &= Check(!registration.validateSettings(
                              registration.defaultSettings, 10u),
                      "modifier defaults are invalid");
        okay &= Check(registration.create(
                              registration.defaultSettings, 10u) != nullptr,
                      "modifier factory returned null");
    }
    for (const auto &registration : forevertas::EvaluationTargetRegistry()) {
        okay &= Check(!registration.settingsComponent.empty(),
                      "evaluation target is missing its QML component");
        okay &= Check(!registration.validateSettings(
                              registration.defaultSettings, 10u),
                      "evaluation target defaults are invalid");
        okay &= Check(registration.create(
                              registration.defaultSettings, 10u) != nullptr,
                      "evaluation target factory returned null");
    }
    okay &= Check(forevertas::ModifierRegistry().size() == 5u,
                  "not all required modifiers are registered");
    okay &= Check(forevertas::EvaluationTargetRegistry().size() == 5u,
                  "not all required evaluation targets are registered");
    return okay;
}

bool TestLocaleIndependentFloatingPointSettings() {
    NumericLocaleGuard locale;
    if (!locale.ActivateCommaDecimalLocale()) {
        return Check(false, "no comma-decimal locale is installed for testing");
    }

    const auto parsed = forevertas::ParseFiniteDouble("-12.5e-1");
    bool okay = Check(parsed && std::abs(*parsed + 1.25) < 1e-12,
                      "dot decimal parsing followed LC_NUMERIC");
    okay &= Check(!forevertas::ParseFiniteDouble("12,5"),
                  "comma decimal input was accepted");
    okay &= Check(!forevertas::ParseFiniteDouble("12.5x"),
                  "floating setting accepted trailing characters");

    okay &= Check(
            ModifierAcceptsDotDecimals(
                    forevertas::kExistingEventPerturbationModifierId,
                    {{"steerDeltaMin", "-0.25"},
                     {"steerDeltaMax", "0.25"},
                     {"steerAbsoluteMin", "-0.75"},
                     {"steerAbsoluteMax", "0.75"}}),
            "existing-event decimal settings followed LC_NUMERIC");
    okay &= Check(
            ModifierAcceptsDotDecimals(
                    forevertas::kSmoothSteeringModifierId,
                    {{"amplitudeMin", "-0.25"},
                     {"amplitudeMax", "0.25"}}),
            "smooth-steering decimal settings followed LC_NUMERIC");
    okay &= Check(
            ModifierAcceptsDotDecimals(
                    forevertas::kInputInsertionModifierId,
                    {{"steerAbsoluteMin", "-0.75"},
                     {"steerAbsoluteMax", "0.75"},
                     {"steerOffsetMin", "-0.25"},
                     {"steerOffsetMax", "0.25"}}),
            "input-insertion decimal settings followed LC_NUMERIC");

    okay &= Check(
            EvaluatorAcceptsDotDecimals(
                    forevertas::kVelocityEvaluationId,
                    {{"mode", "projected"},
                     {"alignmentEnabled", "true"},
                     {"directionX", "0.5"},
                     {"directionY", "0.25"},
                     {"directionZ", "0.75"},
                     {"minAlignmentPercent", "12.5"}}),
            "velocity decimal settings followed LC_NUMERIC");
    okay &= Check(
            EvaluatorAcceptsDotDecimals(
                    forevertas::kPointTargetEvaluationId,
                    {{"x", "12.5"}, {"y", "-3.25"}, {"z", "0.75"}}),
            "point-target decimal settings followed LC_NUMERIC");
    okay &= Check(
            EvaluatorAcceptsDotDecimals(
                    forevertas::kPoseTargetEvaluationId,
                    {{"x", "12.5"},
                     {"yawDegrees", "22.5"},
                     {"pitchDegrees", "-7.25"},
                     {"rotationWeightPercent", "37.5"}}),
            "pose-target decimal settings followed LC_NUMERIC");
    okay &= Check(
            EvaluatorAcceptsDotDecimals(
                    forevertas::kVolumeEntryEvaluationId,
                    {{"centerX", "12.5"},
                     {"centerY", "-3.25"},
                     {"sizeX", "1.5"},
                     {"sizeY", "2.25"}}),
            "volume-entry decimal settings followed LC_NUMERIC");
    return okay;
}

bool TestSearchSettingsAndCancellation() {
    bool okay = Check(
            !forevertas::ValidateBasicBruteForceSettings({10u}, 10u),
            "valid Basic search settings were rejected");
    okay &= Check(
            forevertas::ValidateBasicBruteForceSettings({0u}, 10u)
                    .has_value(),
            "zero attempts were accepted");
    okay &= Check(
            forevertas::ValidateBasicBruteForceSettings({10u}, 0u)
                    .has_value(),
            "zero tick duration was accepted");

    forevertas::SearchRunControl control;
    control.cancellationRequested = []() { return true; };
    try {
        static_cast<void>(forevertas::RunSearch(
                {"unused", "unused"}, &control));
        okay &= Check(false, "immediate cancellation was ignored");
    } catch (const forevertas::SearchCancelled &) {
    } catch (...) {
        okay &= Check(false, "immediate cancellation returned wrong failure");
    }
    return okay;
}

}  // namespace

int main() {
    const bool okay = TestEvaluationTargets() &&
            TestModifierComposition() &&
            TestModifierDeterminism() &&
            TestTmInterfaceInputFormatting() &&
            TestRegistries() &&
            TestLocaleIndependentFloatingPointSettings() &&
            TestSearchSettingsAndCancellation();
    return okay ? 0 : 1;
}
