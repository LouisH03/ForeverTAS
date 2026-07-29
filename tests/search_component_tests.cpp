#include "app/rolling_throughput.h"
#include "evaluators/iteration_evaluator.h"
#include "input_timeline_time.h"
#include "mutations/composite_input_mutator.h"
#include "mutations/input_event_formatter.h"
#include "mutations/input_event_utils.h"
#include "searches/algorithm_registry.h"
#include "searches/basic_brute_force_search.h"
#include "searches/cuda_batch_calibrator.h"
#include "searches/cuda_search_configuration.h"
#include "searches/option_settings_utils.h"
#include "searches/search_runner.h"
#include "time_format.h"

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

using forevertas::AnalogInputState;
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
        constexpr const char *localeNames[] = {
                "fr_FR.utf8", "fr_FR.UTF-8", "de_DE.utf8", "de_DE.UTF-8"};
        for (const char *const localeName : localeNames) {
            if (std::setlocale(LC_NUMERIC, localeName) == nullptr) continue;
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

SandboxInputEvent Analog(std::int32_t timeMs,
                         SandboxInputAction action,
                         AnalogInputState value) {
    SandboxInputEvent event;
    event.timeMs = timeMs;
    event.action = action;
    event.value.kind = PhysicsSandboxInputValueKind::Analog;
    event.value.analog = value;
    return event;
}

SandboxInputEvent Steering(std::int32_t timeMs,
                           AnalogInputState value) {
    return Analog(timeMs, SandboxInputAction::Steer, value);
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

bool AllAnalogInputsValid(
        const std::vector<SandboxInputEvent> &events) {
    for (const SandboxInputEvent &event : events) {
        if (event.value.kind == PhysicsSandboxInputValueKind::Analog &&
            !forevervalidator::IsAnalogInputStateValid(
                    event.value.analog)) {
            return false;
        }
    }
    return true;
}

bool SameEvents(const std::vector<SandboxInputEvent> &left,
                const std::vector<SandboxInputEvent> &right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0u; index < left.size(); ++index) {
        if (!forevertas::SameInputEvent(left[index], right[index])) return false;
    }
    return true;
}

std::unique_ptr<forevertas::IterationEvaluator> Evaluator(
        const char *id,
        const OptionSettings *overrideSettings = nullptr) {
    const auto *const registration = forevertas::FindEvaluationTarget(id);
    if (registration == nullptr) return {};
    const OptionSettings &settings = overrideSettings == nullptr
            ? registration->defaultSettings
            : *overrideSettings;
    return registration->create(settings, 10u);
}


bool TestUserTimelineTimeOrigin() {
    const auto firstInput =
            forevertas::SimulationTimelineTimeFromUserTime(
                    0, forevertas::kInputTimelineTickDurationMs);
    const auto laterInput =
            forevertas::SimulationTimelineTimeFromUserTime(
                    1000, forevertas::kInputTimelineTickDurationMs);
    bool okay = Check(firstInput && *firstInput == 10 &&
                              laterInput && *laterInput == 1010,
                      "user timeline times were not shifted by one tick");
    okay &= Check(
            forevertas::UserTimelineTimeFromSimulationTime(
                    10, forevertas::kInputTimelineTickDurationMs) == 0 &&
                    forevertas::UserTimelineTimeFromSimulationTime(
                            1010,
                            forevertas::kInputTimelineTickDurationMs) == 1000,
            "simulation timeline times did not map back to the user origin");

    const OptionSettings sample{{"minTimeMs", "0"},
                                {"maxTimeMs", "1000"},
                                {"maxTimeShiftMs", "50"},
                                {"radiusMs", "200"},
                                {"maxSteerHoldMs", "300"}};
    const auto converted =
            forevertas::SimulationSettingsFromUserTimeline(
                    sample, forevertas::kInputTimelineTickDurationMs);
    okay &= Check(converted && converted->at("minTimeMs") == "10" &&
                              converted->at("maxTimeMs") == "1010" &&
                              converted->at("maxTimeShiftMs") == "50" &&
                              converted->at("radiusMs") == "200" &&
                              converted->at("maxSteerHoldMs") == "300",
                      "timeline conversion changed a duration setting");

    const auto verifySettings = [&okay](const OptionSettings &settings) {
        const auto simulation =
                forevertas::SimulationSettingsFromUserTimeline(
                        settings,
                        forevertas::kInputTimelineTickDurationMs);
        if (!simulation) {
            okay &= Check(false,
                          "registered settings could not be timeline-converted");
            return;
        }
        for (const auto &[key, value] : settings) {
            if (forevertas::IsUserTimelineTimeSetting(key)) {
                const auto parsed = forevertas::ParseSignedDecimal(value);
                okay &= Check(parsed &&
                                      simulation->at(key) ==
                                              std::to_string(*parsed + 10),
                              "a registered absolute time was not shifted");
            } else {
                okay &= Check(simulation->at(key) == value,
                              "a registered non-time setting was shifted");
            }
        }
    };
    for (const auto &registration : forevertas::SearchAlgorithmRegistry()) {
        verifySettings(registration.defaultSettings);
    }
    for (const auto &registration : forevertas::ModifierRegistry()) {
        verifySettings(registration.defaultSettings);
    }
    for (const auto &registration : forevertas::EvaluationTargetRegistry()) {
        verifySettings(registration.defaultSettings);
    }
    return okay;
}

bool TestHumanDurationFormatting() {
    bool okay = Check(
            forevertas::FormatHumanDurationMilliseconds(0.0) ==
                    "00:00:00",
            "zero duration formatting was incorrect");
    okay &= Check(
            forevertas::FormatHumanDurationMilliseconds(13500.0) ==
                    "00:00:13.5",
            "trailing duration zeroes were not trimmed");
    okay &= Check(
            forevertas::FormatHumanDurationMilliseconds(13.0) ==
                    "00:00:00.013",
            "millisecond duration formatting was incorrect");
    okay &= Check(
            forevertas::FormatHumanDurationMilliseconds(3723004.0) ==
                    "01:02:03.004",
            "hour duration formatting was incorrect");
    okay &= Check(
            forevertas::FormatHumanDurationNanoseconds(1234567890u) ==
                    "1.234567890",
            "sub-minute nanosecond formatting was incorrect");
    okay &= Check(
            forevertas::FormatHumanDurationNanoseconds(
                    62000000003u) ==
                    "1:02.000000003",
            "minute nanosecond formatting was incorrect");
    okay &= Check(
            forevertas::FormatHumanDurationNanoseconds(
                    3723000000004u) ==
                    "1:02:03.000000004",
            "hour nanosecond formatting was incorrect");
    return okay;
}

bool TestMutableSuffixNormalization() {
    const std::vector<SandboxInputEvent> baseline{
            Steering(90, 1234),
            Switch(70, SandboxInputAction::Accelerate, true),
            Steering(80, -4321),
            Steering(100, 1000),
            Steering(110, 2000)};
    std::vector<SandboxInputEvent> iteration{
            Steering(90, 65536),
            Switch(70, SandboxInputAction::Accelerate, true),
            Steering(80, -4321),
            Steering(100, 1000),
            Steering(110, 2000),
            Steering(50, 9999),
            Steering(115, 70000),
            Steering(110, 3000)};

    forevertas::NormalizeMutableInputEvents(
            iteration, baseline, 10u, 100);

    bool okay = Check(iteration.size() == 5u,
                      "mutable suffix normalization returned the wrong size");
    okay &= Check(forevertas::SameInputEvent(iteration[0], baseline[0]) &&
                          forevertas::SameInputEvent(iteration[1], baseline[1]) &&
                          forevertas::SameInputEvent(iteration[2], baseline[2]),
                  "immutable input prefix was reordered or rewritten");
    okay &= Check(iteration[3].timeMs == 100 &&
                          iteration[3].value.analog == 1000 &&
                          iteration[4].timeMs == 110 &&
                          iteration[4].value.analog == 3000,
                  "mutable input suffix was not normalized independently");
    return okay;
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
        auto evaluator = Evaluator(
                forevertas::kPreciseFinishTimeEvaluationId);
        auto session = evaluator->CreateSession();
        PhysicsSandboxStateView previous;
        previous.timeMs = 1230u;
        PhysicsSandboxStateView current = previous;
        current.timeMs = 1240u;
        current.raceCompleted = true;
        current.finishTimeMs = 1234u;
        current.finishTime =
                forevervalidator::FinishTimeEstimate{
                        1234567889u,
                        1234567890u,
                        1234567890u};
        const auto sample = session->Observe(previous, current);
        okay &= Check(
                sample && sample->score == 1234567890.0 &&
                        std::abs(sample->timeMs - 1234.56789) < 1e-9,
                "precise finish target ignored the inclusive upper bound");
        okay &= Check(sample &&
                              sample->description ==
                                      "Precise finish time: 1.234567890",
                      "precise finish target did not show nanoseconds");
        auto laterSession = evaluator->CreateSession();
        current.finishTime =
                forevervalidator::FinishTimeEstimate{
                        1234567890u,
                        1234567891u,
                        1234567891u};
        const auto laterSample =
                laterSession->Observe(previous, current);
        okay &= Check(
                sample && laterSample &&
                        evaluator->IsBetter(*sample, *laterSample),
                "precise finish target did not rank within-tick nanoseconds");
        auto tickOnlySession = evaluator->CreateSession();
        current.finishTime.reset();
        okay &= Check(
                !tickOnlySession->Observe(previous, current),
                "precise finish target fell back to tick time");
    }

    return okay;
}

class AppendSteeringMutator final : public InputMutator {
public:
    explicit AppendSteeringMutator(AnalogInputState value)
        : value_(value) {}

    MutationResult Mutate(const MutationRequest &request) const override {
        std::vector<SandboxInputEvent> inputs = request.baselineInputs;
        inputs.push_back(Steering(1000, value_));
        return {inputs, 1u};
    }

    std::int64_t EarliestMutationTimeMs() const override { return 1000; }

private:
    AnalogInputState value_;
};

class NoOpMutator final : public InputMutator {
public:
    MutationResult Mutate(const MutationRequest &request) const override {
        return {request.baselineInputs, 0u};
    }
    std::int64_t EarliestMutationTimeMs() const override { return 1000; }
};

bool TestModifierComposition() {
    const std::vector<SandboxInputEvent> baseline{Steering(500, 6554)};
    std::vector<std::unique_ptr<InputMutator>> passes;
    passes.push_back(std::make_unique<AppendSteeringMutator>(13107));
    passes.push_back(std::make_unique<AppendSteeringMutator>(45875));
    forevertas::CompositeInputMutator composite(std::move(passes));
    const MutationResult result = composite.Mutate({baseline, 4u, 0u, 10u});
    bool okay = Check(result.mutationCount > 0u,
                      "composed modifiers reported no effective change");
    okay &= Check(result.inputs.size() == 2u,
                  "normalization did not merge same-tick steering events");
    okay &= Check(result.inputs.back().timeMs == 1000 &&
                          result.inputs.back().value.analog == 45875,
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
            Steering(1000, -6554),
            Steering(1010, -13107),
            Steering(1500, 19661),
            Steering(2010, 26214),
            Steering(2020, 32768)};
    const MutationResult first = modifier->Mutate({baseline, 7u, 0u, 10u});
    const MutationResult repeated = modifier->Mutate({baseline, 7u, 0u, 10u});
    const MutationResult otherIteration = modifier->Mutate(
            {baseline, 8u, 0u, 10u});
    bool okay = Check(SameEvents(first.inputs, repeated.inputs),
                      "same seed and iteration index were not deterministic");
    okay &= Check(!SameEvents(first.inputs, otherIteration.inputs),
                  "different iteration indices produced identical inputs");
    okay &= Check(AllAnalogInputsValid(first.inputs) &&
                          AllAnalogInputsValid(otherIteration.inputs),
                  "random steering produced an out-of-range input state");
    okay &= Check(forevertas::SameInputEvent(first.inputs.front(),
                                             baseline.front()) &&
                          forevertas::SameInputEvent(first.inputs.back(),
                                                     baseline.back()),
                  "modifier changed events outside its window");
    return okay;
}

bool TestInputScriptFormatting() {
    NumericLocaleGuard locale;
    if (!locale.ActivateCommaDecimalLocale()) {
        return Check(false, "no comma-decimal locale is installed for testing");
    }
    const std::vector<SandboxInputEvent> inputs{
            Switch(1350, SandboxInputAction::SteerLeft, true),
            Analog(130, SandboxInputAction::Gas, -16384),
            Switch(110, SandboxInputAction::Accelerate, true),
            Steering(125, 32768),
            Steering(140, -16384),
            Steering(150, forevertas::kAnalogInputMaximum),
            Switch(1340, SandboxInputAction::Brake, false),
            Switch(1360, SandboxInputAction::SteerRight, false),
            Switch(1370, SandboxInputAction::Respawn, true),
            Switch(1380, SandboxInputAction::Respawn, false),
            Switch(90, SandboxInputAction::Brake, true),
            Switch(100, SandboxInputAction::RaceRunning, true),
            Switch(1390, SandboxInputAction::FinishLine, true),
            Switch(1400, SandboxInputAction::Accelerate, false)};
    const std::string formatted =
            forevertas::FormatInputScript(inputs);
    return Check(
            formatted ==
                    "0.00 press up\n"
                    "0.02 steer 32768\n"
                    "0.02 gas -16384\n"
                    "0.03 steer -16384\n"
                    "0.04 steer 65536\n"
                    "1.23 rel down\n"
                    "1.24 press left\n"
                    "1.25 rel right\n"
                    "1.26 press enter",
            "input script formatting was incorrect or locale-sensitive");
}

bool TestInputScriptParsingAndBaseline() {
    const forevertas::InputScriptParseResult parsed =
            forevertas::ParseInputScript(
                    "# Base controls\n"
                    "1.00 PRESS up\n"
                    "0.00 steer 32768\n"
                    "0.00 STEER -16384 // last command wins\n"
                    "0.20 release down\n"
                    "0.30 gas -65536\n");
    bool okay = Check(
            parsed && parsed.commands.size() == 5u,
            "valid input script was not parsed");
    if (!parsed) return false;

    const std::vector<SandboxInputEvent> replayInputs{
            Switch(-10, SandboxInputAction::Accelerate, true),
            Switch(90, SandboxInputAction::Brake, true),
            Switch(100, SandboxInputAction::RaceRunning, true),
            Steering(110, 1234),
            Switch(250, SandboxInputAction::Unmapped, true),
            Switch(500, SandboxInputAction::FinishLine, true)};
    const forevertas::InputScriptBaselineResult baseline =
            forevertas::BuildInputScriptBaseline(
                    replayInputs, parsed.commands, 1200, 10u);
    okay &= Check(
            baseline && baseline.events.size() == 9u,
            "input script baseline was not materialized");
    if (baseline) {
        okay &= Check(
                baseline.events[0].timeMs == -10 &&
                        baseline.events[0].action ==
                                SandboxInputAction::Accelerate &&
                        baseline.events[1].timeMs == 90 &&
                        baseline.events[1].action ==
                                SandboxInputAction::Brake &&
                        baseline.events[2].timeMs == 100 &&
                        baseline.events[2].action ==
                                SandboxInputAction::RaceRunning &&
                        baseline.events[3].timeMs == 110 &&
                        baseline.events[3].action ==
                                SandboxInputAction::Steer &&
                        baseline.events[3].value.analog == -16384 &&
                        baseline.events[4].timeMs == 250 &&
                        baseline.events[4].action ==
                                SandboxInputAction::Unmapped &&
                        baseline.events[5].timeMs == 310 &&
                        baseline.events[6].timeMs == 410 &&
                        baseline.events[7].timeMs == 500 &&
                        baseline.events[7].action ==
                                SandboxInputAction::FinishLine &&
                        baseline.events[8].timeMs == 1110,
                "script controls did not replace replay controls correctly");
    }

    const forevertas::InputScriptParseResult empty =
            forevertas::ParseInputScript(" \n# no controls\n// still empty");
    okay &= Check(empty && empty.commands.empty(),
                  "empty input script was not accepted");

    const auto expectError = [&okay](std::string_view script,
                                     std::string_view fragment) {
        const forevertas::InputScriptParseResult result =
                forevertas::ParseInputScript(script);
        okay &= Check(
                !result && result.error &&
                        result.error->find(fragment) != std::string::npos,
                "invalid input script did not report the expected error");
    };
    expectError("0.001 press up", "10 ms-aligned");
    expectError("-0.10 press up", "non-negative");
    expectError("0,10 press up", "10 ms-aligned");
    expectError("0.10.0 press up", "10 ms-aligned");
    expectError("0.00 steer 65537", "[-65536, 65536]");
    expectError("0.00 gas -65537", "[-65536, 65536]");
    expectError("0.00 gas 999999999999999999999", "[-65536, 65536]");
    expectError("0.00 launch up", "command must be");
    expectError("0.00 press space", "switch must be");
    expectError("0.00 press up trailing", "expected");
    expectError("\n0.00 rel enter", "Line 2");
    expectError("9223372036854776.00 press up", "10 ms-aligned");

    const forevertas::InputScriptParseResult tooLate =
            forevertas::ParseInputScript("2.00 press up");
    const forevertas::InputScriptBaselineResult rejected =
            forevertas::BuildInputScriptBaseline(
                    replayInputs, tooLate.commands, 1200, 10u);
    okay &= Check(!rejected && rejected.error &&
                          rejected.error->find("Line 1") != std::string::npos,
                  "script timestamp beyond the replay was accepted");

    const std::string formatted = forevertas::FormatInputScript(replayInputs);
    const forevertas::InputScriptParseResult roundTrip =
            forevertas::ParseInputScript(formatted);
    const forevertas::InputScriptBaselineResult rebuilt =
            forevertas::BuildInputScriptBaseline(
                    replayInputs, roundTrip.commands, 500, 10u);
    okay &= Check(
            roundTrip && rebuilt &&
                    forevertas::FormatInputScript(rebuilt.events) == formatted,
            "formatted input script did not round-trip");
    return okay;
}

bool TestAnalogInputRepresentation() {
    const auto half = forevertas::ParseNormalizedAnalogInput("0.5");
    const auto quarterLeft =
            forevertas::ParseNormalizedAnalogInput("-0.25");
    bool okay = Check(half && *half == 32768 &&
                              quarterLeft && *quarterLeft == -16384,
                      "normalized settings were not quantized exactly");
    okay &= Check(!forevertas::ParseNormalizedAnalogInput("1.0001") &&
                          !forevertas::ParseNormalizedAnalogInput("-1.0001"),
                  "out-of-range normalized analog settings were accepted");
    okay &= Check(forevertas::SaturateAnalogInputState(70000) == 65536 &&
                          forevertas::SaturateAnalogInputState(-70000) ==
                                  -65536,
                  "integer analog saturation was incorrect");

    std::vector<SandboxInputEvent> events{
            Steering(100, 70000), Steering(110, -70000)};
    forevertas::NormalizeInputEvents(events, 10u);
    okay &= Check(events.size() == 2u &&
                          events[0].value.analog == 65536 &&
                          events[1].value.analog == -65536 &&
                          AllAnalogInputsValid(events),
                  "input normalization did not enforce integer bounds");
    return okay;
}

bool TestAllModifierAnalogInvariants() {
    const std::vector<SandboxInputEvent> baseline{
            Steering(1000, -32768),
            Switch(1200, SandboxInputAction::Accelerate, true),
            Steering(2000, 0),
            Switch(2500, SandboxInputAction::Brake, true),
            Steering(4000, 32768),
            Switch(5000, SandboxInputAction::Brake, false)};
    for (const auto &registration : forevertas::ModifierRegistry()) {
        std::unique_ptr<InputMutator> modifier = registration.create(
                registration.defaultSettings, 10u);
        for (std::uint64_t iteration = 0u; iteration < 32u; ++iteration) {
            const MutationResult result = modifier->Mutate(
                    {baseline, iteration, 0u, 10u});
            if (!AllAnalogInputsValid(result.inputs)) {
                return Check(false,
                             "modifier produced an out-of-range analog state");
            }
        }
    }
    return true;
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
    const forevertas::InputScriptParseResult parsedScript =
            forevertas::ParseInputScript("12.50 steer -32768");
    okay &= Check(
            parsedScript && parsedScript.commands.size() == 1u &&
                    parsedScript.commands.front().userTimeMs == 12500,
            "input script parsing followed LC_NUMERIC");
    okay &= Check(!forevertas::ParseFiniteDouble("12,5"),
                  "comma decimal input was accepted");
    okay &= Check(!forevertas::ParseFiniteDouble("12.5x"),
                  "floating setting accepted trailing characters");

    const auto analogQuarter =
            forevertas::ParseNormalizedAnalogInput("0.25");
    okay &= Check(analogQuarter && *analogQuarter == 16384,
                  "analog setting quantization followed LC_NUMERIC");
    okay &= Check(!forevertas::ParseNormalizedAnalogInput("0,25"),
                  "comma-decimal analog setting was accepted");

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

bool TestSearchControl() {
    bool okay = Check(
            !forevertas::ValidateBasicBruteForceOptionSettings({}, 10u),
            "parameterless Basic search settings were rejected");
    okay &= Check(
            forevertas::ValidateBasicBruteForceOptionSettings(
                    {{"unexpected", "1"}}, 10u)
                    .has_value(),
            "an unexpected Basic search setting was accepted");
    okay &= Check(
            forevertas::ValidateBasicBruteForceOptionSettings({}, 0u)
                    .has_value(),
            "zero tick duration was accepted");

    forevertas::SearchRunControl control;
    control.cancellationRequested = []() { return true; };
    try {
        static_cast<void>(forevertas::RunSearch(
                {"unused", "unused"}, &control));
        okay &= Check(false, "immediate hard abort was ignored");
    } catch (const forevertas::SearchCancelled &) {
    } catch (...) {
        okay &= Check(false, "immediate hard abort returned wrong failure");
    }
    return okay;
}

bool TestRollingThroughput() {
    using namespace std::chrono_literals;

    forevertas::app::RollingThroughput throughput;
    bool okay = Check(
            throughput.Observe(0u, 0s) == 0.0,
            "zero-duration throughput was not zero");
    okay &= Check(
            std::abs(throughput.Observe(50u, 5s) - 10.0) < 1e-9,
            "startup throughput average was incorrect");
    okay &= Check(
            std::abs(throughput.Observe(100u, 10s) - 10.0) < 1e-9,
            "ten-second throughput average was incorrect");
    okay &= Check(
            std::abs(throughput.Observe(250u, 15s) - 20.0) < 1e-9,
            "throughput included samples older than ten seconds");

    throughput.Reset();
    static_cast<void>(throughput.Observe(70u, 7s));
    static_cast<void>(throughput.Observe(190u, 13s));
    okay &= Check(
            std::abs(throughput.Observe(290u, 18s) - 20.0) < 1e-9,
            "throughput did not interpolate the ten-second boundary");

    throughput.Reset();
    static_cast<void>(throughput.Observe(100u, 5s));
    okay &= Check(
            throughput.Observe(100u, 15s) == 0.0,
            "idle throughput did not decay over the rolling window");
    return okay;
}

bool TestCudaBatchCalibrationStrategy() {
    const auto observe = [](
                                 forevertas::CudaBatchCalibrator *calibrator,
                                 double throughput) {
        const std::uint32_t batchSize =
                calibrator->CurrentBatchSize();
        const auto elapsed =
                std::chrono::duration_cast<
                        std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>(
                                static_cast<double>(batchSize) /
                                throughput));
        for (int sample = 0; sample < 5; ++sample) {
            calibrator->Observe(batchSize, elapsed);
        }
    };

    const auto realisticThroughput = [](std::uint32_t batchSize) {
        if (batchSize <= 7500u) {
            constexpr double saturationScale = 1000.0;
            constexpr double peakSize = 7500.0;
            const double size = static_cast<double>(batchSize);
            return 9100.0 *
                    (size / (size + saturationScale)) /
                    (peakSize / (peakSize + saturationScale));
        }
        const double distance = std::abs(
                static_cast<double>(batchSize) - 7500.0);
        return 9100.0 - distance * (950.0 / 5300.0);
    };

    const auto calibrate = [&observe](
                                   const auto &throughputForSize,
                                   std::uint32_t capacity) {
        forevertas::CudaBatchCalibrator calibrator;
        for (int measurement = 0;
             measurement < 128 && !calibrator.Complete();
             ++measurement) {
            const std::uint32_t batchSize =
                    calibrator.CurrentBatchSize();
            if (batchSize > capacity) {
                calibrator.CapacityUnavailable();
            } else {
                observe(
                        &calibrator,
                        throughputForSize(batchSize));
            }
        }
        return calibrator;
    };

    auto calibrator = calibrate(
            realisticThroughput,
            102400u);
    const std::uint32_t calibratedSize =
            calibrator.BestBatchSize();
    bool okay = Check(
            calibrator.Complete(),
            "CUDA calibration did not converge");
    okay &= Check(
            calibratedSize >= 7000u &&
                    calibratedSize <= 8000u,
            "CUDA calibration missed the measured peak");
    okay &= Check(
            realisticThroughput(calibratedSize) >
                    realisticThroughput(12800u),
            "CUDA calibration retained the slower coarse batch");
    okay &= Check(
            calibrator.CurrentBatchSize() == calibratedSize,
            "CUDA calibration did not restore its measured winner");

    forevertas::CudaBatchCalibrator capacityLimited;
    for (std::uint32_t batchSize = 1u;
         batchSize <= 4096u;
         batchSize *= 2u) {
        observe(
                &capacityLimited,
                static_cast<double>(batchSize));
    }
    okay &= Check(
            capacityLimited.CurrentBatchSize() == 8192u,
            "CUDA calibration did not grow from the universal minimum");
    capacityLimited.CapacityUnavailable();
    okay &= Check(
            capacityLimited.CurrentBatchSize() > 4096u &&
                    capacityLimited.CurrentBatchSize() < 8192u,
            "CUDA calibration discarded the allocation boundary");
    for (int measurement = 0;
         measurement < 64 && !capacityLimited.Complete();
         ++measurement) {
        observe(
                &capacityLimited,
                static_cast<double>(
                        capacityLimited.CurrentBatchSize()));
    }
    okay &= Check(
            capacityLimited.Complete() &&
                    capacityLimited.BestBatchSize() > 4096u &&
                    capacityLimited.BestBatchSize() < 8192u,
            "CUDA calibration did not approach its measured capacity");
    return okay;
}

bool TestCudaConfigurationCoverage() {
    bool okay = true;
    for (const auto &registration :
         forevertas::ModifierRegistry()) {
        try {
            const auto modifiers = forevertas::BuildCudaModifiers(
                    {{registration.id,
                      registration.defaultSettings}},
                    10u);
            okay &= Check(
                    modifiers.size() == 1u,
                    "a registered modifier was not translated for CUDA");
        } catch (...) {
            okay &= Check(
                    false,
                    "a registered modifier was rejected by CUDA");
        }
    }
    for (const auto &registration :
         forevertas::EvaluationTargetRegistry()) {
        try {
            static_cast<void>(forevertas::BuildCudaEvaluator(
                    {registration.id,
                     registration.defaultSettings},
                    10u));
        } catch (...) {
            okay &= Check(
                    false,
                    "a registered evaluator was rejected by CUDA");
        }
    }
    try {
        static_cast<void>(forevertas::BuildCudaModifiers(
                {{"unsupported-cuda-modifier", {}}}, 10u));
        okay &= Check(
                false,
                "unsupported CUDA modifier did not produce an error");
    } catch (const std::invalid_argument &) {
    }
    try {
        static_cast<void>(forevertas::BuildCudaEvaluator(
                {"unsupported-cuda-evaluator", {}}, 10u));
        okay &= Check(
                false,
                "unsupported CUDA evaluator did not produce an error");
    } catch (const std::invalid_argument &) {
    }
    return okay;
}

}  // namespace

int main() {
    const bool okay = TestUserTimelineTimeOrigin() &&
            TestHumanDurationFormatting() &&
            TestMutableSuffixNormalization() &&
            TestEvaluationTargets() &&
            TestModifierComposition() &&
            TestModifierDeterminism() &&
            TestInputScriptFormatting() &&
            TestInputScriptParsingAndBaseline() &&
            TestAnalogInputRepresentation() &&
            TestAllModifierAnalogInvariants() &&
            TestRegistries() &&
            TestLocaleIndependentFloatingPointSettings() &&
            TestSearchControl() &&
            TestRollingThroughput() &&
            TestCudaBatchCalibrationStrategy() &&
            TestCudaConfigurationCoverage();
    return okay ? 0 : 1;
}
