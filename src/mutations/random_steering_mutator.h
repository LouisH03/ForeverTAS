#ifndef FOREVERTAS_MUTATIONS_RANDOM_STEERING_MUTATOR_H
#define FOREVERTAS_MUTATIONS_RANDOM_STEERING_MUTATOR_H

#include "mutations/input_mutator.h"

namespace forevertas {

class RandomSteeringMutator final : public InputMutator {
public:
    MutationResult Mutate(const MutationRequest &request) const override;
};

}  // namespace forevertas

#endif
