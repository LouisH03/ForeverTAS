#include "evaluators/max_speed_evaluator.h"
#include "mutations/random_steering_mutator.h"
#include "searches/serial_brute_force_search.h"
#include "searches/serial_search_runner.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using forevervalidator::experimental::PhysicsSandboxInputAction;
using forevervalidator::experimental::PhysicsSandboxInputEvent;
using forevervalidator::experimental::PhysicsSandboxInputValueKind;
using forevervalidator::experimental::PhysicsSandboxStateView;

PhysicsSandboxInputEvent Analog(std::int32_t timeMs,
                                PhysicsSandboxInputAction action,
                                float value) {
    PhysicsSandboxInputEvent event;
    event.timeMs = timeMs;
    event.action = action;
    event.value.kind = PhysicsSandboxInputValueKind::Analog;
    event.value.analog = value;
    return event;
}

bool SameEvent(const PhysicsSandboxInputEvent &left,
               const PhysicsSandboxInputEvent &right) {
    return left.timeMs == right.timeMs && left.action == right.action &&
           left.value.kind == right.value.kind &&
           left.value.switchState == right.value.switchState &&
           left.value.analog == right.value.analog;
}

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool TestClosedMutationWindow() {
    const std::vector<PhysicsSandboxInputEvent> baseline{
            Analog(990, PhysicsSandboxInputAction::Steer, -0.1f),
            Analog(1000, PhysicsSandboxInputAction::Steer, -0.2f),
            Analog(1500, PhysicsSandboxInputAction::Accelerate, 1.0f),
            Analog(1500, PhysicsSandboxInputAction::Steer, 0.3f),
            Analog(2000, PhysicsSandboxInputAction::Steer, 0.4f),
            Analog(2010, PhysicsSandboxInputAction::Steer, 0.5f)};

    const forevertas::RandomSteeringMutator mutator;
    const forevertas::MutationRequest request{
            baseline, 1000, 2000, 123u, 7u};
    const forevertas::MutationResult result = mutator.Mutate(request);

    bool okay = true;
    okay &= Check(result.mutationCount == 3u,
                  "closed mutation window did not include both endpoints");
    okay &= Check(SameEvent(result.inputs[0], baseline[0]),
                  "mutation changed an event before the window");
    okay &= Check(!SameEvent(result.inputs[1], baseline[1]),
                  "mutation did not change the lower endpoint");
    okay &= Check(SameEvent(result.inputs[2], baseline[2]),
                  "mutation changed a non-steering input");
    okay &= Check(!SameEvent(result.inputs[3], baseline[3]),
                  "mutation did not change an interior steering event");
    okay &= Check(!SameEvent(result.inputs[4], baseline[4]),
                  "mutation did not change the upper endpoint");
    okay &= Check(SameEvent(result.inputs[5], baseline[5]),
                  "mutation changed an event after the window");

    const forevertas::MutationResult repeated = mutator.Mutate(request);
    for (std::size_t index = 0u; index < result.inputs.size(); ++index) {
        okay &= Check(SameEvent(result.inputs[index], repeated.inputs[index]),
                      "same mutation request was not deterministic");
    }

    const forevertas::MutationResult otherAttempt = mutator.Mutate({
            baseline, 1000, 2000, 123u, 8u});
    bool differs = false;
    for (std::size_t index = 0u; index < result.inputs.size(); ++index) {
        differs |= !SameEvent(result.inputs[index], otherAttempt.inputs[index]);
    }
    okay &= Check(differs, "different attempt produced the same candidate");

    const forevertas::MutationResult noEligible = mutator.Mutate({
            baseline, 3000, 4000, 123u, 9u});
    okay &= Check(noEligible.mutationCount == 0u,
                  "empty mutation window reported changed inputs");
    return okay;
}

bool TestMaxSpeedEvaluator() {
    PhysicsSandboxStateView state;
    state.car.linearSpeed = {3.0f, 4.0f, 12.0f};
    const forevertas::MaxSpeedEvaluator evaluator;
    return Check(evaluator.Evaluate(state) == 13.0,
                 "max-speed evaluator returned the wrong magnitude");
}

bool IsValid(const forevertas::SerialBruteForceSettings &settings) {
    return !forevertas::ValidateSerialBruteForceSettings(settings, 10u);
}

bool TestSearchSettings() {
    const forevertas::SerialBruteForceSettings valid{
            1000, 2000, 1500, 3000, 10u, 123u};
    bool okay = Check(IsValid(valid), "valid search settings were rejected");

    okay &= Check(
            forevertas::ValidateSerialBruteForceSettings(valid, 0u)
                    .has_value(),
            "zero tick duration was accepted");

    auto changed = valid;
    changed.minMutateMs = 0;
    okay &= Check(!IsValid(changed),
                  "mutation before the first tick was accepted");

    changed = valid;
    changed.minEvalTimeMs = 990;
    okay &= Check(!IsValid(changed),
                  "evaluation before mutation was accepted");

    changed = valid;
    changed.maxMutateMs = 999;
    okay &= Check(!IsValid(changed),
                  "reversed mutation window was accepted");

    changed = valid;
    changed.maxEvalTimeMs = 1490;
    okay &= Check(!IsValid(changed),
                  "reversed evaluation window was accepted");

    changed = valid;
    changed.minMutateMs = 1001;
    okay &= Check(!IsValid(changed),
                  "unaligned minimum mutation time was accepted");

    changed = valid;
    changed.maxMutateMs = 2001;
    okay &= Check(!IsValid(changed),
                  "unaligned maximum mutation time was accepted");

    changed = valid;
    changed.minEvalTimeMs = 1501;
    okay &= Check(!IsValid(changed),
                  "unaligned minimum evaluation time was accepted");

    changed = valid;
    changed.maxEvalTimeMs = 3001;
    okay &= Check(!IsValid(changed),
                  "unaligned maximum evaluation time was accepted");

    changed = valid;
    changed.attemptCount = 0u;
    okay &= Check(!IsValid(changed), "zero attempts were accepted");

    const forevertas::SerialBruteForceSettings identical{
            1000, 2000, 1000, 2000, 1u, 123u};
    okay &= Check(IsValid(identical),
                  "identical mutation and evaluation windows were rejected");

    const forevertas::SerialBruteForceSettings disjoint{
            1000, 1500, 2000, 3000, 10000u, 123u};
    okay &= Check(IsValid(disjoint),
                  "disjoint mutation and evaluation windows were rejected");

    const forevertas::SerialBruteForceSettings largeAligned{
            9223372036854775800LL,
            9223372036854775800LL,
            9223372036854775800LL,
            9223372036854775800LL,
            std::numeric_limits<std::uint64_t>::max(),
            std::numeric_limits<std::uint32_t>::max()};
    okay &= Check(IsValid(largeAligned),
                  "aligned integer boundary settings were rejected");
    return okay;
}

bool TestImmediateCancellation() {
    forevertas::SearchRunControl control;
    control.cancellationRequested = []() { return true; };
    try {
        static_cast<void>(forevertas::RunSerialSearch(
                {"unused",
                 "unused",
                 forevertas::DefaultSerialBruteForceSettings()},
                &control));
    } catch (const forevertas::SearchCancelled &) {
        return true;
    } catch (...) {
        return Check(false,
                     "immediate cancellation returned the wrong failure");
    }
    return Check(false, "immediate cancellation was ignored");
}

}  // namespace

int main() {
    const bool okay = TestClosedMutationWindow() &&
                      TestMaxSpeedEvaluator() &&
                      TestSearchSettings() &&
                      TestImmediateCancellation();
    return okay ? 0 : 1;
}
