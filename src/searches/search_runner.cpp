#include "searches/search_runner.h"

#include "mutations/composite_input_mutator.h"
#include "mutations/input_event_utils.h"
#include "replay_file_io.h"
#include "searches/algorithm_registry.h"
#include "searches/cuda_search_configuration.h"

#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>
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

void ReportProgress(const SearchRunControl *control,
                    SearchProgressStage stage,
                    std::uint64_t completedWork,
                    std::uint64_t totalWork) {
    if (control != nullptr && control->progressChanged) {
        control->progressChanged(
                {stage, completedWork, totalWork});
    }
}

SearchTimelineFrame ToTimelineFrame(
        const forevervalidator::experimental::PhysicsSandboxStateView &view) {
    return {
            static_cast<std::int64_t>(view.timeMs),
            view.car.position.x,
            view.car.position.y,
            view.car.position.z,
            view.car.rotationX,
            view.car.rotationY,
            view.car.rotationZ,
            view.car.rotationW,
            view.accelerate,
            view.brake,
            view.steering};
}

struct TimelineSamplingRuntime {
    forevervalidator::experimental::PhysicsSandbox sandbox;
    forevervalidator::experimental::PhysicsSandboxState initialState;
    std::uint64_t finalTickCount = 0u;
};

TimelineSamplingRuntime CreateTimelineSamplingRuntime(
        const SearchRequest &request,
        const forevervalidator::AssetBytes &replay,
        const forevervalidator::ReplayIdentity &identity) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    AssetSource source = Require(
            OpenInstalledPackDirectory(request.packDirectory),
            "opening pack directory for timeline sampling");
    PhysicsSandboxOptions options;
    options.backend = ToForeverValidatorBackend(request.backend);
    options.tickDurationMs = kSearchTickDurationMs;
    PhysicsSandbox sandbox = Require(
            CreatePhysicsSandbox(std::move(source), options),
            "creating timeline-sampling sandbox");
    const PhysicsSandboxStateView state = Require(
            sandbox.LoadReplay({replay.data(), replay.size()}, identity),
            "loading replay for timeline sampling");
    if (state.durationMs % kSearchTickDurationMs != 0u) {
        throw std::runtime_error(
                "replay duration is not aligned to the search tick duration");
    }
    PhysicsSandboxState initialState = Require(
            sandbox.CaptureState(),
            "capturing timeline-sampling initial state");
    return {
            std::move(sandbox),
            std::move(initialState),
            state.durationMs / kSearchTickDurationMs};
}

std::vector<SearchTimelineFrame> SampleTimeline(
        TimelineSamplingRuntime &runtime,
        const std::vector<
                forevervalidator::experimental::PhysicsSandboxInputEvent>
                &inputs,
        const SearchRunControl *control,
        bool reportProgress) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    CheckCancellation(control);
    PhysicsSandboxStateView state = Require(
            runtime.sandbox.RestoreState(runtime.initialState),
            "restoring timeline-sampling initial state");
    Require(runtime.sandbox.ReplaceInputs(inputs),
            "replacing inputs for timeline sampling");
    std::vector<SearchTimelineFrame> frames;
    if (runtime.finalTickCount >= frames.max_size()) {
        throw std::length_error("timeline contains too many ticks");
    }
    frames.reserve(
            static_cast<std::size_t>(runtime.finalTickCount) + 1u);
    frames.push_back(ToTimelineFrame(state));
    if (reportProgress) {
        ReportProgress(control,
                       SearchProgressStage::FinalSampling,
                       0u,
                       runtime.finalTickCount);
    }

    for (std::uint64_t tick = 1u;
         tick <= runtime.finalTickCount;
         ++tick) {
        CheckCancellation(control);
        state = Require(runtime.sandbox.AdvanceTicks(1u),
                        "sampling run timeline");
        frames.push_back(ToTimelineFrame(state));
        if (reportProgress &&
            (tick == runtime.finalTickCount || tick % 128u == 0u)) {
            ReportProgress(control,
                           SearchProgressStage::FinalSampling,
                           tick,
                           runtime.finalTickCount);
        }
    }
    return frames;
}

std::vector<SearchTimelineFrame> SampleBestTimeline(
        const SearchRequest &request,
        const forevervalidator::AssetBytes &replay,
        const forevervalidator::ReplayIdentity &identity,
        const std::vector<
                forevervalidator::experimental::PhysicsSandboxInputEvent>
                &inputs,
        const SearchRunControl *control) {
    ReportProgress(
            control, SearchProgressStage::FinalSamplingSetup, 0u, 0u);
    TimelineSamplingRuntime runtime =
            CreateTimelineSamplingRuntime(request, replay, identity);
    return SampleTimeline(runtime, inputs, control, true);
}

struct CachedSearchSandbox {
    std::mutex lock;
    forevervalidator::AssetBytes replay;
    std::optional<
            forevervalidator::experimental::PhysicsSandbox>
            sandbox;
    std::optional<
            forevervalidator::experimental::PhysicsSandboxState>
            initialState;
    std::vector<
            forevervalidator::experimental::PhysicsSandboxInputEvent>
            initialInputs;
};

std::shared_ptr<CachedSearchSandbox> CachedSandboxFor(
        const SearchRequest &request) {
    using Key = std::tuple<std::string, std::string, PhysicsBackend>;
    static std::mutex cacheLock;
    static std::map<Key, std::shared_ptr<CachedSearchSandbox>> cache;
    const Key key{
            request.packDirectory,
            request.replayPath,
            request.backend};
    std::lock_guard<std::mutex> guard(cacheLock);
    auto &entry = cache[key];
    if (!entry) {
        entry = std::make_shared<CachedSearchSandbox>();
    }
    return entry;
}

SearchResult RunLoadedSearch(
        const SearchRequest &request,
        const forevervalidator::AssetBytes &replay,
        const forevervalidator::ReplayIdentity &identity,
        forevervalidator::experimental::PhysicsSandbox &sandbox,
        const SearchAlgorithmRegistration &searchRegistration,
        const EvaluationTargetRegistration &evaluationRegistration,
        const SearchRunControl *control) {
    using namespace forevervalidator::experimental;

    std::unique_ptr<SearchAlgorithm> search =
            searchRegistration.create(
                    request.searchAlgorithm.settings,
                    kSearchTickDurationMs);
    std::vector<std::unique_ptr<InputMutator>> modifierPasses;
    modifierPasses.reserve(request.modifiers.size());
    for (const OptionConfiguration &modifier : request.modifiers) {
        const ModifierRegistration *const registration =
                FindModifier(modifier.id);
        modifierPasses.push_back(registration->create(
                modifier.settings, kSearchTickDurationMs));
    }
    const CompositeInputMutator mutator(std::move(modifierPasses));
    std::unique_ptr<IterationEvaluator> evaluator =
            evaluationRegistration.create(
                    request.evaluationTarget.settings,
                    kSearchTickDurationMs);
    std::vector<PhysicsSandboxCudaModifier> cudaModifiers;
    std::optional<PhysicsSandboxCudaEvaluator> cudaEvaluator;
    ReportProgress(control, SearchProgressStage::PreparingSearch, 0u, 0u);
#if FOREVERVALIDATOR_HAS_CUDA
    if (request.backend == PhysicsBackend::Cuda) {
        if (request.searchAlgorithm.id != kBasicBruteForceSearchId) {
            throw std::invalid_argument(
                    "CUDA does not support search algorithm: " +
                    request.searchAlgorithm.id);
        }
        cudaModifiers = BuildCudaModifiers(
                request.modifiers, kSearchTickDurationMs);
        cudaEvaluator = BuildCudaEvaluator(
                {evaluationRegistration.id,
                 request.evaluationTarget.settings},
                kSearchTickDurationMs);
    }
#endif
    const SearchRunControl *executionControl = control;
    SearchRunControl instrumentedControl;
    std::unique_ptr<TimelineSamplingRuntime> improvementSampler;
    std::uint64_t sampledImprovementCount = 0u;
    if (control != nullptr && control->liveChanged) {
        instrumentedControl = *control;
        const auto downstreamLiveChanged = control->liveChanged;
        instrumentedControl.liveChanged =
                [&, downstreamLiveChanged](
                        const SearchLiveUpdate &live) {
                    const bool improved =
                            live.winnerSource ==
                                    SearchWinnerSource::Mutation &&
                            live.mutationImprovementCount >
                                    sampledImprovementCount;
                    if (!improved) {
                        downstreamLiveChanged(live);
                        return;
                    }
                    if (!improvementSampler) {
                        improvementSampler =
                                std::make_unique<TimelineSamplingRuntime>(
                                        CreateTimelineSamplingRuntime(
                                                request,
                                                replay,
                                                identity));
                    }
                    SearchLiveUpdate enriched = live;
                    enriched.bestTimeline = SampleTimeline(
                            *improvementSampler,
                            live.bestInputs,
                            control,
                            false);
                    downstreamLiveChanged(enriched);
                    sampledImprovementCount =
                            live.mutationImprovementCount;
                };
        executionControl = &instrumentedControl;
    }

    SearchResult result = search->Run({
            sandbox,
            kSearchTickDurationMs,
            mutator,
            *evaluator,
            executionControl,
            request.parallelSampleCount,
            request.calibrateCudaParallelSampleCount,
            cudaModifiers.empty() ? nullptr : &cudaModifiers,
            cudaEvaluator ? &*cudaEvaluator : nullptr});
    CheckCancellation(control);
    if (control == nullptr || control->sampleBestTimeline) {
        result.bestTimeline = SampleBestTimeline(
                request, replay, identity, result.bestInputs, control);
    }
    return result;
}

std::vector<forevervalidator::experimental::PhysicsSandboxInputEvent>
BuildBaselineOrThrow(
        const SearchRequest &request,
        const std::vector<
                forevervalidator::experimental::PhysicsSandboxInputEvent>
                &replayInputs,
        std::int64_t replayDurationMs) {
    InputScriptBaselineResult baseline = BuildInputScriptBaseline(
            replayInputs,
            request.baseInputCommands,
            replayDurationMs,
            kSearchTickDurationMs);
    if (!baseline) {
        throw std::invalid_argument(*baseline.error);
    }
    ConvertKeyboardSteeringToAnalog(baseline.events);
    return std::move(baseline.events);
}

}  // namespace

SearchResult RunSearch(const SearchRequest &request,
                       const SearchRunControl *control) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    const SearchAlgorithmRegistration *const searchRegistration =
            FindSearchAlgorithm(request.searchAlgorithm.id);
    const EvaluationTargetRegistration *const evaluationRegistration =
            FindEvaluationTarget(request.evaluationTarget.id);
    if (searchRegistration == nullptr) {
        throw std::invalid_argument("unknown search algorithm: " +
                                    request.searchAlgorithm.id);
    }
    if (request.modifiers.empty()) {
        throw std::invalid_argument(
                "modifier pipeline must contain at least one pass");
    }
    if (evaluationRegistration == nullptr) {
        throw std::invalid_argument("unknown evaluation target: " +
                                    request.evaluationTarget.id);
    }
    if (const auto error = searchRegistration->validateSettings(
                request.searchAlgorithm.settings, kSearchTickDurationMs)) {
        throw std::invalid_argument(*error);
    }
    if (const auto error = evaluationRegistration->validateSettings(
                request.evaluationTarget.settings, kSearchTickDurationMs)) {
        throw std::invalid_argument(*error);
    }
    for (const OptionConfiguration &modifier : request.modifiers) {
        const ModifierRegistration *const registration =
                FindModifier(modifier.id);
        if (registration == nullptr) {
            throw std::invalid_argument("unknown modifier: " + modifier.id);
        }
        if (const auto error = registration->validateSettings(
                    modifier.settings, kSearchTickDurationMs)) {
            throw std::invalid_argument(*error);
        }
    }

    CheckCancellation(control);
    const ReplayIdentity identity{request.replayPath};
    PhysicsSandboxOptions options;
    options.backend = ToForeverValidatorBackend(request.backend);
    options.tickDurationMs = kSearchTickDurationMs;
    if (control != nullptr && control->reuseLoadedSandbox) {
        const std::shared_ptr<CachedSearchSandbox> cached =
                CachedSandboxFor(request);
        std::lock_guard<std::mutex> guard(cached->lock);
        CheckCancellation(control);
        if (!cached->sandbox) {
            ReportProgress(
                    control,
                    SearchProgressStage::OpeningPacksDirectory,
                    0u,
                    0u);
            AssetSource source = Require(
                    OpenInstalledPackDirectory(request.packDirectory),
                    "opening cached pack directory");
            ReportProgress(
                    control, SearchProgressStage::ReadingReplay, 0u, 0u);
            cached->replay = Require(
                    ReadReplayFileUtf8(request.replayPath, identity),
                    "reading cached replay");
            ReportProgress(
                    control,
                    SearchProgressStage::CreatingSimulation,
                    0u,
                    0u);
            cached->sandbox.emplace(Require(
                    CreatePhysicsSandbox(std::move(source), options),
                    "creating cached sandbox"));
            ReportProgress(
                    control, SearchProgressStage::LoadingReplay, 0u, 0u);
            Require(
                    cached->sandbox->LoadReplay(
                            {cached->replay.data(),
                             cached->replay.size()},
                            identity),
                    "loading replay into cached sandbox");
            cached->initialState = Require(
                    cached->sandbox->CaptureState(),
                    "capturing cached initial state");
            cached->initialInputs = Require(
                    cached->sandbox->ReadInputs(),
                    "reading cached initial inputs");
        } else {
            ReportProgress(
                    control,
                    SearchProgressStage::RestoringSimulation,
                    0u,
                    0u);
            Require(
                    cached->sandbox->RestoreState(
                            *cached->initialState),
                    "restoring cached initial state");
            Require(
                    cached->sandbox->ReplaceInputs(
                            cached->initialInputs),
                    "restoring cached initial inputs");
        }
        const PhysicsSandboxStateView initialView = Require(
                cached->sandbox->ReadState(),
                "reading cached initial state");
        ReportProgress(
                control,
                SearchProgressStage::ApplyingBaselineInputs,
                0u,
                0u);
        Require(
                cached->sandbox->ReplaceInputs(BuildBaselineOrThrow(
                        request,
                        cached->initialInputs,
                        static_cast<std::int64_t>(
                                initialView.durationMs))),
                "applying base input script");
        CheckCancellation(control);
        return RunLoadedSearch(
                request,
                cached->replay,
                identity,
                *cached->sandbox,
                *searchRegistration,
                *evaluationRegistration,
                control);
    }

    ReportProgress(
            control, SearchProgressStage::OpeningPacksDirectory, 0u, 0u);
    AssetSource source = Require(
            OpenInstalledPackDirectory(request.packDirectory),
            "opening pack directory");
    CheckCancellation(control);
    ReportProgress(control, SearchProgressStage::ReadingReplay, 0u, 0u);
    AssetBytes replay = Require(
            ReadReplayFileUtf8(request.replayPath, identity),
            "reading replay");
    CheckCancellation(control);
    ReportProgress(
            control, SearchProgressStage::CreatingSimulation, 0u, 0u);
    PhysicsSandbox sandbox = Require(
            CreatePhysicsSandbox(std::move(source), options),
            "creating sandbox");
    CheckCancellation(control);
    ReportProgress(control, SearchProgressStage::LoadingReplay, 0u, 0u);
    const PhysicsSandboxStateView initialView = Require(
            sandbox.LoadReplay({replay.data(), replay.size()}, identity),
            "loading replay");
    const std::vector<PhysicsSandboxInputEvent> replayInputs = Require(
            sandbox.ReadInputs(), "reading replay inputs");
    ReportProgress(
            control,
            SearchProgressStage::ApplyingBaselineInputs,
            0u,
            0u);
    Require(
            sandbox.ReplaceInputs(BuildBaselineOrThrow(
                    request,
                    replayInputs,
                    static_cast<std::int64_t>(initialView.durationMs))),
            "applying base input script");
    CheckCancellation(control);
    return RunLoadedSearch(
            request,
            replay,
            identity,
            sandbox,
            *searchRegistration,
            *evaluationRegistration,
            control);
}

}  // namespace forevertas
