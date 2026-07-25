#ifndef FOREVERTAS_PHYSICS_BACKEND_H
#define FOREVERTAS_PHYSICS_BACKEND_H

#include <forevervalidator/validation.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace forevertas {

enum class PhysicsBackend : std::uint8_t {
    Reference,
    OptimizedCpu,
};

constexpr std::string_view PhysicsBackendId(PhysicsBackend backend) noexcept {
    switch (backend) {
    case PhysicsBackend::Reference:
        return "reference";
    case PhysicsBackend::OptimizedCpu:
        return "optimized-cpu";
    }
    return "reference";
}

inline std::optional<PhysicsBackend> ParsePhysicsBackend(
        std::string_view id) noexcept {
    if (id == PhysicsBackendId(PhysicsBackend::Reference)) {
        return PhysicsBackend::Reference;
    }
    if (id == PhysicsBackendId(PhysicsBackend::OptimizedCpu)) {
        return PhysicsBackend::OptimizedCpu;
    }
    return std::nullopt;
}

constexpr forevervalidator::SimulationBackend ToForeverValidatorBackend(
        PhysicsBackend backend) noexcept {
    switch (backend) {
    case PhysicsBackend::Reference:
        return forevervalidator::SimulationBackend::Reference;
    case PhysicsBackend::OptimizedCpu:
        return forevervalidator::SimulationBackend::OptimizedCpu;
    }
    return forevervalidator::SimulationBackend::Reference;
}

}  // namespace forevertas

#endif
