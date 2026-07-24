#ifndef FOREVERTAS_MUTATIONS_INPUT_EVENT_UTILS_H
#define FOREVERTAS_MUTATIONS_INPUT_EVENT_UTILS_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include <forevervalidator/experimental/physics_sandbox.h>

namespace forevertas {

using SandboxInputEvent =
        forevervalidator::experimental::PhysicsSandboxInputEvent;
using SandboxInputAction =
        forevervalidator::experimental::PhysicsSandboxInputAction;

std::int64_t AlignInputTime(std::int64_t timeMs,
                            std::uint32_t tickDurationMs);
float ClampSteering(float value);
bool SameInputEvent(const SandboxInputEvent &left,
                    const SandboxInputEvent &right);
void NormalizeInputEvents(std::vector<SandboxInputEvent> &events,
                          std::uint32_t tickDurationMs);
std::size_t EffectiveInputChangeCount(
        const std::vector<SandboxInputEvent> &baseline,
        const std::vector<SandboxInputEvent> &candidate);

float SteeringStateAt(const std::vector<SandboxInputEvent> &events,
                      std::int64_t timeMs);
bool SwitchStateAt(const std::vector<SandboxInputEvent> &events,
                   SandboxInputAction action,
                   std::int64_t timeMs);

}  // namespace forevertas

#endif
