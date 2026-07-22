#include "mutations/random_steering_mutator.h"

#include <limits>
#include <random>

namespace forevertas {
namespace {

float RandomSteering(std::mt19937 &random) {
    const double unit = static_cast<double>(random()) /
                        static_cast<double>(
                                std::numeric_limits<std::uint32_t>::max());
    return static_cast<float>(unit * 2.0 - 1.0);
}

}  // namespace

MutationResult RandomSteeringMutator::Mutate(
        const MutationRequest &request) const {
    using forevervalidator::experimental::PhysicsSandboxInputAction;
    using forevervalidator::experimental::PhysicsSandboxInputEvent;
    using forevervalidator::experimental::PhysicsSandboxInputValueKind;

    std::seed_seq sequence{
            request.seed,
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
