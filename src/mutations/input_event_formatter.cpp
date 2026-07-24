#include "mutations/input_event_formatter.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace forevertas {
namespace {

using forevervalidator::experimental::PhysicsSandboxInputValueKind;
using forevervalidator::experimental::PhysicsSandboxSwitchState;

constexpr std::int32_t kInputScriptCommandLeadMs = 10;
constexpr std::int64_t kInputScriptTickMs = 10;

std::string FormatTime(std::int32_t timeMs,
                       std::int32_t startTimeMs) {
    const std::int64_t shiftedTimeMs =
            static_cast<std::int64_t>(timeMs) - startTimeMs -
            kInputScriptCommandLeadMs;
    const std::int64_t scriptTimeMs = shiftedTimeMs <= 0
            ? 0
            : ((shiftedTimeMs + kInputScriptTickMs - 1) /
               kInputScriptTickMs) * kInputScriptTickMs;
    const std::int64_t hundredths = scriptTimeMs / 10;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << hundredths / 100 << '.' << std::setfill('0')
           << std::setw(2) << hundredths % 100;
    return output.str();
}

std::string_view SwitchCommand(
        forevervalidator::experimental::PhysicsSandboxInputAction action) {
    switch (action) {
    case SandboxInputAction::Accelerate:
        return "up";
    case SandboxInputAction::Brake:
        return "down";
    case SandboxInputAction::SteerLeft:
        return "left";
    case SandboxInputAction::SteerRight:
        return "right";
    case SandboxInputAction::Respawn:
        return "enter";
    case SandboxInputAction::Unmapped:
    case SandboxInputAction::Gas:
    case SandboxInputAction::Steer:
    case SandboxInputAction::RaceRunning:
    case SandboxInputAction::FinishLine:
        return {};
    }
    return {};
}

}  // namespace

std::string FormatInputScript(
        const std::vector<SandboxInputEvent> &events) {
    std::vector<const SandboxInputEvent *> sortedEvents;
    sortedEvents.reserve(events.size());
    for (const SandboxInputEvent &event : events) {
        sortedEvents.push_back(&event);
    }
    std::stable_sort(
            sortedEvents.begin(),
            sortedEvents.end(),
            [](const SandboxInputEvent *left,
               const SandboxInputEvent *right) {
                return left->timeMs < right->timeMs;
            });

    std::int32_t startTimeMs = 0;
    for (const SandboxInputEvent *const event : sortedEvents) {
        if (event->action == SandboxInputAction::RaceRunning) {
            startTimeMs = event->timeMs;
            break;
        }
    }

    std::ostringstream output;
    output.imbue(std::locale::classic());
    bool firstLine = true;
    for (const SandboxInputEvent *const eventPointer : sortedEvents) {
        const SandboxInputEvent &event = *eventPointer;
        if (event.timeMs < startTimeMs) {
            continue;
        }
        if (event.action == SandboxInputAction::FinishLine) {
            break;
        }
        std::string command;
        if ((event.action == SandboxInputAction::Steer ||
             event.action == SandboxInputAction::Gas) &&
            event.value.kind == PhysicsSandboxInputValueKind::Analog) {
            if (!forevervalidator::IsAnalogInputStateValid(
                        event.value.analog)) {
                throw std::invalid_argument(
                        "analog input state is outside [-65536, 65536]");
            }
            command = event.action == SandboxInputAction::Steer
                    ? "steer "
                    : "gas ";
            command += std::to_string(event.value.analog);
        } else if (event.value.kind ==
                   PhysicsSandboxInputValueKind::Switch) {
            const std::string_view action = SwitchCommand(event.action);
            if (action.empty()) {
                continue;
            }
            const bool pressed = event.value.switchState !=
                    PhysicsSandboxSwitchState::Released;
            if (event.action == SandboxInputAction::Respawn && !pressed) {
                continue;
            }
            command = pressed ? "press " : "rel ";
            command.append(action);
        } else {
            continue;
        }

        if (!firstLine) {
            output << '\n';
        }
        firstLine = false;
        output << FormatTime(event.timeMs, startTimeMs) << ' ' << command;
    }
    return output.str();
}

}  // namespace forevertas
