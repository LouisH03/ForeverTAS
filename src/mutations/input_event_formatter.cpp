#include "mutations/input_event_formatter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace forevertas {
namespace {

using forevervalidator::experimental::PhysicsSandboxInputValueKind;
using forevervalidator::experimental::PhysicsSandboxSwitchState;

std::string FormatTime(std::int32_t timeMs) {
    const std::int64_t hundredths =
            std::max<std::int64_t>(0, timeMs) / 10;
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
    case SandboxInputAction::Gas:
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
    case SandboxInputAction::Steer:
    case SandboxInputAction::RaceRunning:
    case SandboxInputAction::FinishLine:
        return {};
    }
    return {};
}

}  // namespace

std::string FormatTmInterfaceInputs(
        const std::vector<SandboxInputEvent> &events) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    bool firstLine = true;
    for (const SandboxInputEvent &event : events) {
        std::string command;
        if (event.action == SandboxInputAction::Steer &&
            event.value.kind == PhysicsSandboxInputValueKind::Analog) {
            const long steering = std::lround(
                    static_cast<double>(ClampSteering(event.value.analog)) *
                    65536.0);
            command = "steer " + std::to_string(
                    std::clamp<long>(steering, -65536, 65536));
        } else if (event.value.kind ==
                   PhysicsSandboxInputValueKind::Switch) {
            const std::string_view action = SwitchCommand(event.action);
            if (action.empty()) {
                continue;
            }
            const bool pressed = event.value.switchState !=
                    PhysicsSandboxSwitchState::Released;
            command = pressed ? "press " : "release ";
            command.append(action);
        } else {
            continue;
        }

        if (!firstLine) {
            output << '\n';
        }
        firstLine = false;
        output << FormatTime(event.timeMs) << ' ' << command;
    }
    return output.str();
}

}  // namespace forevertas
