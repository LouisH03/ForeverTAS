#include "evaluators/max_speed_evaluator.h"
#include "mutations/random_steering_mutator.h"
#include "searches/serial_brute_force_search.h"

#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using forevervalidator::DiscriminatedResult;
using forevervalidator::experimental::PhysicsSandboxStateView;

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

forevertas::SerialBruteForceSettings SearchSettings() {
    forevertas::SerialBruteForceSettings settings;
    settings.minMutateMs = 1000;
    settings.maxMutateMs = 6000;
    settings.minEvalTimeMs = 1000;
    settings.maxEvalTimeMs = 6000;
    settings.attemptCount = 10u;
    settings.mutationSeed = 0x46544153u;
    return settings;
}

void PrintUsage(const char *program) {
    std::cout << "Usage: " << program << " PACK_DIRECTORY REPLAY\n";
}

void PrintResult(const forevertas::SearchResult &result) {
    const PhysicsSandboxStateView &state = result.bestState;
    const std::chrono::duration<double, std::milli> elapsed = result.elapsed;

    std::cout << std::fixed << std::setprecision(3)
              << "winner="
              << (result.winnerSource == forevertas::SearchWinnerSource::Baseline
                          ? "baseline"
                          : "mutation");
    if (result.winningAttempt) {
        std::cout << " attempt=" << *result.winningAttempt;
    }
    std::cout << " score=" << result.bestScore
              << " winning_mutations=" << result.winningMutationCount
              << " tick=" << state.tick
              << " time_ms=" << state.timeMs
              << " position=" << state.car.position.x << ','
              << state.car.position.y << ',' << state.car.position.z
              << " speed=" << state.car.linearSpeed.x << ','
              << state.car.linearSpeed.y << ',' << state.car.linearSpeed.z
              << " checkpoints=" << state.checkpointsCollected << '/'
              << state.checkpointsTotal
              << " laps=" << state.completedLaps << '/' << state.totalLaps
              << " finished=" << (state.raceCompleted ? "yes" : "no")
              << '\n';

    std::cout << "attempts=" << result.requestedAttempts
              << " executed=" << result.executedAttempts
              << " skipped=" << result.skippedAttempts
              << " evaluator_calls=" << result.evaluatorCalls
              << " improvements=" << result.improvementCount
              << " total_mutations=" << result.totalMutationCount
              << " elapsed_ms=" << elapsed.count() << '\n';
}

}  // namespace

int main(int argc, char **argv) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    if (argc == 2 && std::string(argv[1]) == "--help") {
        PrintUsage(argv[0]);
        return 0;
    }
    if (argc != 3) {
        PrintUsage(argv[0]);
        return 2;
    }

    try {
        const ReplayIdentity identity{argv[2]};
        AssetSource source = Require(
                OpenInstalledPackDirectory(argv[1]),
                "opening pack directory");
        AssetBytes replay = Require(
                ReadNativeReplayFile(argv[2], identity), "reading replay");

        PhysicsSandboxOptions options;
        options.backend = SimulationBackend::Reference;
        options.tickDurationMs = 10u;
        PhysicsSandbox sandbox = Require(
                CreatePhysicsSandbox(std::move(source), options),
                "creating sandbox");
        Require(sandbox.LoadReplay({replay.data(), replay.size()}, identity),
                "loading replay");

        const forevertas::SerialBruteForceSettings settings = SearchSettings();
        const forevertas::RandomSteeringMutator mutator;
        const forevertas::MaxSpeedEvaluator evaluator;
        const forevertas::SerialBruteForceSearch search(settings);
        const forevertas::SearchResult result = search.Run({
                sandbox,
                options.tickDurationMs,
                mutator,
                evaluator});
        PrintResult(result);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
