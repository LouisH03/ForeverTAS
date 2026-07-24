#ifndef FOREVERTAS_MUTATIONS_COMPOSITE_INPUT_MUTATOR_H
#define FOREVERTAS_MUTATIONS_COMPOSITE_INPUT_MUTATOR_H

#include "mutations/input_mutator.h"

#include <memory>
#include <vector>

namespace forevertas {

class CompositeInputMutator final : public InputMutator {
public:
    explicit CompositeInputMutator(
            std::vector<std::unique_ptr<InputMutator>> modifiers);

    MutationResult Mutate(const MutationRequest &request) const override;
    std::int64_t EarliestMutationTimeMs() const override;

private:
    std::vector<std::unique_ptr<InputMutator>> modifiers_;
};

}  // namespace forevertas

#endif
