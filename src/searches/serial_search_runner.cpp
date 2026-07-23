#include "searches/serial_search_runner.h"

#include "evaluators/max_speed_evaluator.h"
#include "mutations/random_steering_mutator.h"

#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

#include <stdexcept>
#include <utility>

namespace forevertas {
namespace {

using forevervalidator::DiscriminatedResult;

template<typename T, typename Error>
T Require(DiscriminatedResult<T, Error> result, const char *operation) {
    if (!result) {
        std::string message = std::string(operation) + " failed";
        if (!result.Error().diagnostic.empty()) {
            message += ": " + result.Error().diagnostic;
        }
        throw std::runtime_error(std::move(message));
    }
    return std::move(result).Value();
}

void CheckCancellation(const SearchRunControl *control) {
    if (control != nullptr && control->cancellationRequested &&
        control->cancellationRequested()) {
        throw SearchCancelled();
    }
}

}  // namespace

SerialBruteForceSettings DefaultSerialBruteForceSettings() {
    return {
            1000,
            6000,
            1000,
            6000,
            10u,
            1179926867u,
    };
}

SearchResult RunSerialSearch(const SerialSearchRequest &request,
                             const SearchRunControl *control) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    CheckCancellation(control);
    const ReplayIdentity identity{request.replayPath};
    AssetSource source = Require(
            OpenInstalledPackDirectory(request.packDirectory),
            "opening pack directory");
    CheckCancellation(control);
    AssetBytes replay = Require(
            ReadNativeReplayFile(request.replayPath, identity),
            "reading replay");
    CheckCancellation(control);

    PhysicsSandboxOptions options;
    options.backend = SimulationBackend::Reference;
    options.tickDurationMs = kSearchTickDurationMs;
    PhysicsSandbox sandbox = Require(
            CreatePhysicsSandbox(std::move(source), options),
            "creating sandbox");
    CheckCancellation(control);
    Require(sandbox.LoadReplay({replay.data(), replay.size()}, identity),
            "loading replay");
    CheckCancellation(control);

    const RandomSteeringMutator mutator;
    const MaxSpeedEvaluator evaluator;
    const SerialBruteForceSearch search(request.settings);
    return search.Run({
            sandbox,
            options.tickDurationMs,
            mutator,
            evaluator,
            control});
}

}  // namespace forevertas
