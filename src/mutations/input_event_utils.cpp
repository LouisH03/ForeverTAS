#include "mutations/input_event_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace forevertas {
namespace {

using forevervalidator::experimental::PhysicsSandboxInputValueKind;
using forevervalidator::experimental::PhysicsSandboxSwitchState;

bool SameValue(const SandboxInputEvent &left,
               const SandboxInputEvent &right) {
    if (left.value.kind != right.value.kind) return false;
    switch (left.value.kind) {
    case PhysicsSandboxInputValueKind::None:
        return true;
    case PhysicsSandboxInputValueKind::Switch:
        return left.value.switchState == right.value.switchState;
    case PhysicsSandboxInputValueKind::Analog:
        return left.value.analog == right.value.analog;
    }
    return false;
}

}  // namespace

std::int64_t AlignInputTime(std::int64_t timeMs,
                            std::uint32_t tickDurationMs) {
    if (tickDurationMs == 0u) return timeMs;
    const std::int64_t tick = static_cast<std::int64_t>(tickDurationMs);
    if (timeMs <= 0) return 0;
    return (timeMs / tick) * tick;
}

float ClampSteering(float value) {
    return std::clamp(value, -1.0f, 1.0f);
}

bool SameInputEvent(const SandboxInputEvent &left,
                    const SandboxInputEvent &right) {
    return left.timeMs == right.timeMs && left.action == right.action &&
           SameValue(left, right);
}

void NormalizeInputEvents(std::vector<SandboxInputEvent> &events,
                          std::uint32_t tickDurationMs) {
    for (SandboxInputEvent &event : events) {
        event.timeMs = AlignInputTime(event.timeMs, tickDurationMs);
        if (event.value.kind == PhysicsSandboxInputValueKind::Analog &&
            event.action == SandboxInputAction::Steer) {
            event.value.analog = ClampSteering(event.value.analog);
        } else if (event.value.kind == PhysicsSandboxInputValueKind::Switch) {
            event.value.switchState =
                    event.value.switchState !=
                                    PhysicsSandboxSwitchState::Released
                    ? PhysicsSandboxSwitchState::Pressed
                    : PhysicsSandboxSwitchState::Released;
        }
    }

    std::stable_sort(events.begin(), events.end(),
                     [](const SandboxInputEvent &left,
                        const SandboxInputEvent &right) {
                         return left.timeMs < right.timeMs;
                     });

    std::vector<SandboxInputEvent> normalized;
    normalized.reserve(events.size());
    for (const SandboxInputEvent &event : events) {
        auto duplicate = std::find_if(
                normalized.rbegin(), normalized.rend(),
                [&event](const SandboxInputEvent &existing) {
                    return existing.timeMs == event.timeMs &&
                           existing.action == event.action;
                });
        if (duplicate != normalized.rend()) {
            *duplicate = event;
        } else {
            normalized.push_back(event);
        }
    }
    events.swap(normalized);
}

std::size_t EffectiveInputChangeCount(
        const std::vector<SandboxInputEvent> &baseline,
        const std::vector<SandboxInputEvent> &candidate) {
    const std::size_t common = std::min(baseline.size(), candidate.size());
    std::size_t count = baseline.size() > candidate.size()
            ? baseline.size() - candidate.size()
            : candidate.size() - baseline.size();
    for (std::size_t index = 0u; index < common; ++index) {
        if (!SameInputEvent(baseline[index], candidate[index])) ++count;
    }
    return count;
}

float SteeringStateAt(const std::vector<SandboxInputEvent> &events,
                      std::int64_t timeMs) {
    float state = 0.0f;
    std::int64_t bestTime = std::numeric_limits<std::int64_t>::min();
    for (const SandboxInputEvent &event : events) {
        if (event.action != SandboxInputAction::Steer ||
            event.value.kind != PhysicsSandboxInputValueKind::Analog ||
            event.timeMs > timeMs || event.timeMs < bestTime) {
            continue;
        }
        state = event.value.analog;
        bestTime = event.timeMs;
    }
    return state;
}

bool SwitchStateAt(const std::vector<SandboxInputEvent> &events,
                   SandboxInputAction action,
                   std::int64_t timeMs) {
    bool state = false;
    std::int64_t bestTime = std::numeric_limits<std::int64_t>::min();
    for (const SandboxInputEvent &event : events) {
        if (event.action != action ||
            event.value.kind != PhysicsSandboxInputValueKind::Switch ||
            event.timeMs > timeMs || event.timeMs < bestTime) {
            continue;
        }
        state = event.value.switchState !=
                PhysicsSandboxSwitchState::Released;
        bestTime = event.timeMs;
    }
    return state;
}

}  // namespace forevertas
