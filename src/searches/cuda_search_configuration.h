#ifndef FOREVERTAS_SEARCHES_CUDA_SEARCH_CONFIGURATION_H
#define FOREVERTAS_SEARCHES_CUDA_SEARCH_CONFIGURATION_H

#include "searches/option_configuration.h"

#include <cstdint>
#include <vector>

#include <forevervalidator/experimental/physics_sandbox.h>

namespace forevertas {

std::vector<
        forevervalidator::experimental::PhysicsSandboxCudaModifier>
BuildCudaModifiers(
        const std::vector<OptionConfiguration> &modifiers,
        std::uint32_t tickDurationMs);

forevervalidator::experimental::PhysicsSandboxCudaEvaluator
BuildCudaEvaluator(
        const OptionConfiguration &evaluator,
        std::uint32_t tickDurationMs);

}  // namespace forevertas

#endif
