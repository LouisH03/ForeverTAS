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
    MultiThreadedCpu,
#if FOREVERVALIDATOR_HAS_CUDA
    Cuda,
#endif
};

constexpr std::string_view PhysicsBackendId(PhysicsBackend backend) noexcept {
    switch (backend) {
    case PhysicsBackend::Reference:
        return "reference";
    case PhysicsBackend::OptimizedCpu:
        return "optimized-cpu";
    case PhysicsBackend::MultiThreadedCpu:
        return "multi-threaded-cpu";
#if FOREVERVALIDATOR_HAS_CUDA
    case PhysicsBackend::Cuda:
        return "cuda";
#endif
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
    if (id == PhysicsBackendId(PhysicsBackend::MultiThreadedCpu)) {
        return PhysicsBackend::MultiThreadedCpu;
    }
#if FOREVERVALIDATOR_HAS_CUDA
    if (id == PhysicsBackendId(PhysicsBackend::Cuda)) {
        return PhysicsBackend::Cuda;
    }
#endif
    return std::nullopt;
}

constexpr forevervalidator::SimulationBackend ToForeverValidatorBackend(
        PhysicsBackend backend) noexcept {
    switch (backend) {
    case PhysicsBackend::Reference:
        return forevervalidator::SimulationBackend::Reference;
    case PhysicsBackend::OptimizedCpu:
    case PhysicsBackend::MultiThreadedCpu:
        return forevervalidator::SimulationBackend::OptimizedCpu;
#if FOREVERVALIDATOR_HAS_CUDA
    case PhysicsBackend::Cuda:
        return forevervalidator::SimulationBackend::Cuda;
#endif
    }
    return forevervalidator::SimulationBackend::Reference;
}

}  // namespace forevertas

#endif
