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
    std::int64_t minMutateMs = 0;
    std::int64_t maxMutateMs = 0;
    std::uint32_t seed = 0u;
    std::uint64_t attemptIndex = 0u;
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
};

}  // namespace forevertas

#endif
