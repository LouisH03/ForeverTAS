#include "mutations/replay_input_script.h"

#include "input_timeline_time.h"
#include "mutations/input_event_formatter.h"
#include "replay_file_io.h"

#include <stdexcept>
#include <utility>
#include <vector>

#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

namespace forevertas {
namespace {

template<typename T, typename Error>
T Require(forevervalidator::DiscriminatedResult<T, Error> result,
          const char *operation) {
    if (!result) {
        std::string message = std::string(operation) + " failed";
        if (!result.Error().diagnostic.empty()) {
            message += ": " + result.Error().diagnostic;
        }
        throw std::runtime_error(std::move(message));
    }
    return std::move(result).Value();
}

}  // namespace

std::string ExtractReplayInputScript(
        const std::string &packsDirectory,
        const std::string &replayPath) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    const ReplayIdentity identity{replayPath};
    AssetSource source = Require(
            OpenInstalledPackDirectory(packsDirectory),
            "opening Packs directory");
    AssetBytes replay = Require(
            ReadReplayFileUtf8(replayPath, identity),
            "reading replay");
    PhysicsSandboxOptions options;
    options.backend = SimulationBackend::Reference;
    options.tickDurationMs = kInputTimelineTickDurationMs;
    PhysicsSandbox sandbox = Require(
            CreatePhysicsSandbox(std::move(source), options),
            "creating input extraction sandbox");
    Require(
            sandbox.LoadReplay({replay.data(), replay.size()}, identity),
            "loading replay");
    return FormatInputScript(
            Require(sandbox.ReadInputs(), "reading replay inputs"));
}

}  // namespace forevertas
