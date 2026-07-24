#ifndef FOREVERTAS_MUTATIONS_INPUT_MUTATOR_H
#define FOREVERTAS_MUTATIONS_INPUT_MUTATOR_H

#include "mutations/input_event_utils.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace forevertas {

struct MutationRequest {
    const std::vector<SandboxInputEvent> &baselineInputs;
    std::uint64_t iterationIndex = 0u;
    std::uint32_t passIndex = 0u;
    std::uint32_t tickDurationMs = 10u;
    std::int64_t mutableFromTimeMs =
            std::numeric_limits<std::int64_t>::min();
};

struct MutationResult {
    std::vector<SandboxInputEvent> inputs;
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
