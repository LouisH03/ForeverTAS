#include "mutations/composite_input_mutator.h"

#include "mutations/input_event_utils.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace forevertas {

CompositeInputMutator::CompositeInputMutator(
        std::vector<std::unique_ptr<InputMutator>> modifiers)
    : modifiers_(std::move(modifiers)) {}

MutationResult CompositeInputMutator::Mutate(
        const MutationRequest &request) const {
    std::vector<SandboxInputEvent> current = request.baselineInputs;
    for (std::size_t index = 0u; index < modifiers_.size(); ++index) {
        const MutationResult pass = modifiers_[index]->Mutate({
                current,
                request.iterationIndex,
                static_cast<std::uint32_t>(index),
                request.tickDurationMs,
                request.mutableFromTimeMs});
        current = pass.inputs;
    }
    NormalizeMutableInputEvents(current,
                                request.baselineInputs,
                                request.tickDurationMs,
                                request.mutableFromTimeMs);
    return {current,
            EffectiveInputChangeCount(request.baselineInputs, current)};
}

std::int64_t CompositeInputMutator::EarliestMutationTimeMs() const {
    std::int64_t earliest = std::numeric_limits<std::int64_t>::max();
    for (const auto &modifier : modifiers_) {
        earliest = std::min(earliest, modifier->EarliestMutationTimeMs());
    }
    return earliest == std::numeric_limits<std::int64_t>::max()
            ? 0
            : earliest;
}

}  // namespace forevertas
