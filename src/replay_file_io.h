#ifndef FOREVERTAS_REPLAY_FILE_IO_H
#define FOREVERTAS_REPLAY_FILE_IO_H

#include <string>

#include <forevervalidator/validation.h>

namespace forevertas {

forevervalidator::Result<forevervalidator::AssetBytes> ReadReplayFileUtf8(
        const std::string &path,
        const forevervalidator::ReplayIdentity &identity = {}) noexcept;

}  // namespace forevertas

#endif
