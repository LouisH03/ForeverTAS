#ifndef FOREVERTAS_MUTATIONS_COMPOSITE_INPUT_MUTATOR_H
#define FOREVERTAS_MUTATIONS_COMPOSITE_INPUT_MUTATOR_H

#include "mutations/input_mutator.h"

#include <memory>
#include <optional>
#include <vector>

namespace forevertas {

class CompositeInputMutator final : public InputMutator {
public:
    explicit CompositeInputMutator(
            std::vector<std::unique_ptr<InputMutator>> modifiers);

    MutationResult Mutate(const MutationRequest &request) const override;
    std::int64_t EarliestMutationTimeMs() const override;
    MutationTimeRange AffectedTimeRange() const override;

private:
    struct LocalBaselineCache {
        const std::vector<SandboxInputEvent> *source = nullptr;
        std::uint64_t generation = 0u;
        std::uint32_t tickDurationMs = 0u;
        bool usable = false;
        std::vector<SandboxInputEvent> events;
    };

    MutationResult MutateFull(const MutationRequest &request) const;
    void PrepareLocalBaseline(const MutationRequest &request) const;

    std::vector<std::unique_ptr<InputMutator>> modifiers_;
    std::optional<MutationTimeRange> affectedTimeRange_;
    mutable LocalBaselineCache localBaselineCache_;
};

}  // namespace forevertas

#endif
