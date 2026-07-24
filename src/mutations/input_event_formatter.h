#ifndef FOREVERTAS_MUTATIONS_INPUT_EVENT_FORMATTER_H
#define FOREVERTAS_MUTATIONS_INPUT_EVENT_FORMATTER_H

#include "mutations/input_event_utils.h"

#include <string>
#include <vector>

namespace forevertas {

std::string FormatTmInterfaceInputs(
        const std::vector<SandboxInputEvent> &events);

}  // namespace forevertas

#endif
