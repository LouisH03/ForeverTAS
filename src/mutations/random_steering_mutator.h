#ifndef FOREVERTAS_MUTATIONS_RANDOM_STEERING_MUTATOR_H
#define FOREVERTAS_MUTATIONS_RANDOM_STEERING_MUTATOR_H

#include "mutations/input_mutator.h"
#include "searches/option_configuration.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace forevertas {

struct RandomSteeringSettings {
    std::uint32_t seed = 0u;
};

RandomSteeringSettings DefaultRandomSteeringSettings();
OptionSettings DefaultRandomSteeringOptionSettings();
std::optional<std::string> ValidateRandomSteeringOptionSettings(
        const OptionSettings &settings);
std::unique_ptr<InputMutator> CreateRandomSteeringMutator(
        const OptionSettings &settings);

class RandomSteeringMutator final : public InputMutator {
public:
    explicit RandomSteeringMutator(RandomSteeringSettings settings);

    MutationResult Mutate(const MutationRequest &request) const override;

private:
    RandomSteeringSettings settings_;
};

}  // namespace forevertas

#endif
