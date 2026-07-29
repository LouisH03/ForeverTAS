#ifndef FOREVERTAS_MUTATIONS_REPLAY_INPUT_SCRIPT_H
#define FOREVERTAS_MUTATIONS_REPLAY_INPUT_SCRIPT_H

#include <string>

namespace forevertas {

std::string ExtractReplayInputScript(
        const std::string &packsDirectory,
        const std::string &replayPath);

}  // namespace forevertas

#endif
