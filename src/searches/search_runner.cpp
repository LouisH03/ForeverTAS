#include "searches/search_runner.h"

#include "searches/algorithm_registry.h"

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

SearchResult RunSearch(const SearchRequest &request,
                       const SearchRunControl *control) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    const SearchAlgorithmRegistration *const searchRegistration =
            FindSearchAlgorithm(request.searchAlgorithm.id);
    const MutationAlgorithmRegistration *const mutationRegistration =
            FindMutationAlgorithm(request.mutationAlgorithm.id);
    const EvaluationTargetRegistration *const evaluationRegistration =
            FindEvaluationTarget(request.evaluationTarget.id);
    if (searchRegistration == nullptr) {
        throw std::invalid_argument("unknown search algorithm: " +
                                    request.searchAlgorithm.id);
    }
    if (mutationRegistration == nullptr) {
        throw std::invalid_argument("unknown mutation algorithm: " +
                                    request.mutationAlgorithm.id);
    }
    if (evaluationRegistration == nullptr) {
        throw std::invalid_argument("unknown evaluation target: " +
                                    request.evaluationTarget.id);
    }
    if (const auto error = searchRegistration->validateSettings(
                request.searchAlgorithm.settings, kSearchTickDurationMs)) {
        throw std::invalid_argument(*error);
    }
    if (const auto error = mutationRegistration->validateSettings(
                request.mutationAlgorithm.settings)) {
        throw std::invalid_argument(*error);
    }
    if (const auto error = evaluationRegistration->validateSettings(
                request.evaluationTarget.settings)) {
        throw std::invalid_argument(*error);
    }

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

    std::unique_ptr<SearchAlgorithm> search =
            searchRegistration->create(
                    request.searchAlgorithm.settings,
                    kSearchTickDurationMs);
    std::unique_ptr<InputMutator> mutator = mutationRegistration->create(
            request.mutationAlgorithm.settings);
    std::unique_ptr<CandidateEvaluator> evaluator =
            evaluationRegistration->create(request.evaluationTarget.settings);
    return search->Run({
            sandbox,
            options.tickDurationMs,
            *mutator,
            *evaluator,
            control});
}

}  // namespace forevertas
