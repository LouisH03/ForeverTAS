#include "mutations/composite_input_mutator.h"

#include "mutations/input_event_utils.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace forevertas {

CompositeInputMutator::CompositeInputMutator(
        std::vector<std::unique_ptr<InputMutator>> modifiers)
    : modifiers_(std::move(modifiers)) {
    for (const auto &modifier : modifiers_) {
        sparseMutationSupported_ &= modifier->SupportsSparseMutation();
        const MutationTimeRange range = modifier->AffectedTimeRange();
        if (!affectedTimeRange_) {
            affectedTimeRange_ = range;
        } else {
            affectedTimeRange_->minimumTimeMs = std::min(
                    affectedTimeRange_->minimumTimeMs,
                    range.minimumTimeMs);
            affectedTimeRange_->maximumTimeMs = std::max(
                    affectedTimeRange_->maximumTimeMs,
                    range.maximumTimeMs);
        }
    }
}

MutationResult CompositeInputMutator::Mutate(
        const MutationRequest &request) const {
    if (!request.preferWindowPatch || !affectedTimeRange_ ||
        !sparseMutationSupported_) {
        return MutateFull(request);
    }

    PrepareLocalBaseline(request);
    if (!localBaselineCache_.usable) return MutateFull(request);

    const std::int64_t patchMinimumTimeMs = std::max(
            affectedTimeRange_->minimumTimeMs,
            request.mutableFromTimeMs);
    if (patchMinimumTimeMs > affectedTimeRange_->maximumTimeMs) {
        return {};
    }

    SparseMutationTimeline timeline(localBaselineCache_.index);
    for (std::size_t index = 0u; index < modifiers_.size(); ++index) {
        modifiers_[index]->MutateSparse(timeline, {
                request.iterationIndex,
                static_cast<std::uint32_t>(index),
                request.tickDurationMs,
                request.mutableFromTimeMs});
    }

    MutationWindowPatch patch;
    patch.minimumTimeMs = patchMinimumTimeMs;
    patch.maximumTimeMs = affectedTimeRange_->maximumTimeMs;
    patch.events = timeline.MaterializeRange(
            patch.minimumTimeMs, patch.maximumTimeMs);
    const std::size_t mutationCount =
            EffectiveInputChangeCount(request.baselineInputs, patch);
    return {{}, mutationCount, std::move(patch)};
}

MutationResult CompositeInputMutator::MutateFull(
        const MutationRequest &request) const {
    std::vector<SandboxInputEvent> current = request.baselineInputs;
    for (std::size_t index = 0u; index < modifiers_.size(); ++index) {
        MutationResult pass = modifiers_[index]->Mutate({
                current,
                request.iterationIndex,
                static_cast<std::uint32_t>(index),
                request.tickDurationMs,
                request.mutableFromTimeMs,
                false,
                request.baselineGeneration});
        current = pass.windowPatch
                ? ApplyInputWindowPatch(current, *pass.windowPatch)
                : std::move(pass.inputs);
    }
    NormalizeMutableInputEvents(current,
                                request.baselineInputs,
                                request.tickDurationMs,
                                request.mutableFromTimeMs);
    return {current,
            EffectiveInputChangeCount(request.baselineInputs, current)};
}

void CompositeInputMutator::PrepareLocalBaseline(
        const MutationRequest &request) const {
    if (localBaselineCache_.source == &request.baselineInputs &&
        localBaselineCache_.generation == request.baselineGeneration &&
        localBaselineCache_.tickDurationMs == request.tickDurationMs) {
        return;
    }

    localBaselineCache_ = {};
    localBaselineCache_.source = &request.baselineInputs;
    localBaselineCache_.generation = request.baselineGeneration;
    localBaselineCache_.tickDurationMs = request.tickDurationMs;
    localBaselineCache_.usable = affectedTimeRange_ &&
            localBaselineCache_.index.Build(
                    request.baselineInputs,
                    affectedTimeRange_->minimumTimeMs,
                    affectedTimeRange_->maximumTimeMs,
                    request.tickDurationMs);
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

MutationTimeRange CompositeInputMutator::AffectedTimeRange() const {
    return affectedTimeRange_.value_or(MutationTimeRange{});
}

}  // namespace forevertas
