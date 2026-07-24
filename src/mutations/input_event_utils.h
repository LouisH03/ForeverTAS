#ifndef FOREVERTAS_MUTATIONS_INPUT_EVENT_UTILS_H
#define FOREVERTAS_MUTATIONS_INPUT_EVENT_UTILS_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/input_state.h>

namespace forevertas {

using AnalogInputState = forevervalidator::AnalogInputState;
using SandboxInputEvent =
        forevervalidator::experimental::PhysicsSandboxInputEvent;
using SandboxInputAction =
        forevervalidator::experimental::PhysicsSandboxInputAction;

inline constexpr AnalogInputState kAnalogInputMinimum =
        forevervalidator::kAnalogInputMinimum;
inline constexpr AnalogInputState kAnalogInputMaximum =
        forevervalidator::kAnalogInputMaximum;
inline constexpr AnalogInputState kAnalogInputScale =
        forevervalidator::kAnalogInputScale;

std::int64_t AlignInputTime(std::int64_t timeMs,
                            std::uint32_t tickDurationMs);
AnalogInputState SaturateAnalogInputState(std::int64_t value);
bool SameInputEvent(const SandboxInputEvent &left,
                    const SandboxInputEvent &right);
void NormalizeInputEvents(std::vector<SandboxInputEvent> &events,
                          std::uint32_t tickDurationMs);
void NormalizeMutableInputEvents(
        std::vector<SandboxInputEvent> &events,
        const std::vector<SandboxInputEvent> &baseline,
        std::uint32_t tickDurationMs,
        std::int64_t mutableFromTimeMs);
std::size_t EffectiveInputChangeCount(
        const std::vector<SandboxInputEvent> &baseline,
        const std::vector<SandboxInputEvent> &iterationInputs);

AnalogInputState SteeringStateAt(
        const std::vector<SandboxInputEvent> &events,
        std::int64_t timeMs);
bool SwitchStateAt(const std::vector<SandboxInputEvent> &events,
                   SandboxInputAction action,
                   std::int64_t timeMs);

}  // namespace forevertas

#endif
