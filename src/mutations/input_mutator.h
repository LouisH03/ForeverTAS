#ifndef FOREVERTAS_MUTATIONS_INPUT_MUTATOR_H
#define FOREVERTAS_MUTATIONS_INPUT_MUTATOR_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include <forevervalidator/experimental/physics_sandbox.h>

namespace forevertas {

struct MutationRequest {
    const std::vector<
            forevervalidator::experimental::PhysicsSandboxInputEvent>
            &baselineInputs;
    std::uint64_t attemptIndex = 0u;
    std::uint32_t passIndex = 0u;
    std::uint32_t tickDurationMs = 10u;
};

struct MutationResult {
    std::vector<forevervalidator::experimental::PhysicsSandboxInputEvent>
            inputs;
    std::size_t mutationCount = 0u;
};

class InputMutator {
public:
    virtual ~InputMutator() = default;
    virtual MutationResult Mutate(const MutationRequest &request) const = 0;
    virtual std::int64_t EarliestMutationTimeMs() const = 0;
};

}  // namespace forevertas

#endif
