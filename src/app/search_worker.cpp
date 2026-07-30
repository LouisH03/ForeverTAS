#include "app/search_worker.h"

#include "app/rolling_throughput.h"
#include "mutations/input_event_formatter.h"
#include "time_format.h"

#include <chrono>
#include <cmath>
#include <exception>
#include <utility>

namespace forevertas::app {
namespace {

QString IterationLabel(
        SearchWinnerSource source,
        const std::optional<std::uint64_t> &iterationIndex) {
    if (source == SearchWinnerSource::Baseline) {
        return QStringLiteral("Baseline");
    }
    return iterationIndex
            ? QStringLiteral("Iteration #%1")
                      .arg(static_cast<qulonglong>(*iterationIndex + 1u))
            : QStringLiteral("Mutation");
}

QString IterationsPerSecond(double rate) {
    return QString::number(static_cast<qlonglong>(std::llround(rate)));
}

QString RoundedDuration(
        std::chrono::steady_clock::duration duration) {
    return QString::fromStdString(FormatHumanDuration(
            std::chrono::round<std::chrono::seconds>(duration)));
}

QString LastImprovementText(const SearchLiveUpdate &live) {
    if (!live.lastImprovementElapsed) {
        return QStringLiteral("none");
    }
    const auto age = live.elapsed > *live.lastImprovementElapsed
            ? live.elapsed - *live.lastImprovementElapsed
            : std::chrono::steady_clock::duration::zero();
    return RoundedDuration(age) + QStringLiteral(" ago");
}

QString FormatLive(const SearchLiveUpdate &live, const QString &heading) {
    return QStringLiteral(
                   "%1: %2\n"
                   "%3\n"
                   "Improvements: %4\n"
                   "Last improvement: %5")
            .arg(heading)
            .arg(IterationLabel(live.winnerSource,
                                live.winningIterationIndex))
            .arg(QString::fromStdString(live.bestEvaluationDescription))
            .arg(static_cast<qulonglong>(
                    live.mutationImprovementCount))
            .arg(LastImprovementText(live));
}

SearchLiveUpdate ToLiveUpdate(const SearchResult &result) {
    return {
            result.winnerSource,
            result.winningIterationIndex,
            result.winningMutationCount,
            result.bestScore,
            result.bestEvaluationTimeMs,
            result.bestEvaluationDescription,
            result.bestState,
            result.bestInputs,
            result.iterations,
            result.evaluatorCalls,
            result.mutationImprovementCount,
            result.totalMutationCount,
            result.elapsed,
            result.lastImprovementElapsed};
}

QString FormatResult(const SearchResult &result) {
    return FormatLive(ToLiveUpdate(result), QStringLiteral("Best"));
}

QString FilePathFromUtf8(const std::string &path) {
    return QString::fromUtf8(
            path.data(), static_cast<qsizetype>(path.size()));
}

}  // namespace

QString SearchStageStatus(SearchProgressStage stage,
                          std::string_view backendId) {
    const bool cuda = backendId == "cuda";
    switch (stage) {
    case SearchProgressStage::OpeningPacksDirectory:
        return QStringLiteral("Opening Packs directory...");
    case SearchProgressStage::ReadingReplay:
        return QStringLiteral("Reading replay file...");
    case SearchProgressStage::CreatingSimulation:
        if (cuda) {
            return QStringLiteral(
                    "Initializing CUDA simulation; waiting for GPU "
                    "availability...");
        }
        if (backendId == "optimized-cpu") {
            return QStringLiteral("Initializing optimized CPU simulation...");
        }
        return QStringLiteral("Initializing reference simulation...");
    case SearchProgressStage::LoadingReplay:
        if (cuda) {
            return QStringLiteral(
                    "Loading replay onto the GPU; waiting for GPU "
                    "availability...");
        }
        return QStringLiteral("Loading replay into the simulation...");
    case SearchProgressStage::RestoringSimulation:
        if (cuda) {
            return QStringLiteral(
                    "Restoring the prepared CUDA simulation; waiting for GPU "
                    "availability...");
        }
        return QStringLiteral("Restoring the prepared simulation...");
    case SearchProgressStage::ApplyingBaselineInputs:
        if (cuda) {
            return QStringLiteral(
                    "Applying baseline inputs to CUDA; waiting for GPU "
                    "availability...");
        }
        return QStringLiteral("Applying the baseline input sequence...");
    case SearchProgressStage::PreparingSearch:
        if (cuda) {
            return QStringLiteral(
                    "Preparing CUDA search components; waiting for GPU "
                    "availability...");
        }
        return QStringLiteral("Preparing search components...");
    case SearchProgressStage::Baseline:
        return cuda
                ? QStringLiteral(
                          "Evaluating CUDA baseline; waiting for GPU "
                          "availability...")
                : QStringLiteral("Evaluating baseline...");
    case SearchProgressStage::Calibration:
        return QStringLiteral(
                "Calibrating CUDA throughput; waiting for GPU "
                "availability...");
    case SearchProgressStage::Mutations:
        return cuda
                ? QStringLiteral(
                          "Searching on CUDA; waiting for GPU availability "
                          "as needed...")
                : QStringLiteral("Searching...");
    case SearchProgressStage::FinalSamplingSetup:
        return cuda
                ? QStringLiteral(
                          "Preparing final CUDA sampling; waiting for GPU "
                          "availability...")
                : QStringLiteral("Preparing final best-run sampling...");
    case SearchProgressStage::FinalSampling:
        return QStringLiteral("Sampling best run...");
    }
    return QStringLiteral("Preparing search...");
}

bool TryBeginSearchIteration(
        const std::shared_ptr<std::atomic<SearchIterationPhase>> &phase) {
    SearchIterationPhase expected = SearchIterationPhase::Pending;
    if (phase->compare_exchange_strong(
                expected,
                SearchIterationPhase::Started,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
        return true;
    }
    return expected == SearchIterationPhase::Started;
}

bool TryCancelBeforeSearchIteration(
        const std::shared_ptr<std::atomic<SearchIterationPhase>> &phase) {
    SearchIterationPhase expected = SearchIterationPhase::Pending;
    return phase->compare_exchange_strong(
            expected,
            SearchIterationPhase::Cancelled,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
}

SearchWorker::SearchWorker(
        SearchRequest request,
        std::shared_ptr<std::atomic_bool> stopRequested,
        std::shared_ptr<std::atomic_bool> cancellationRequested,
        std::shared_ptr<std::atomic<SearchIterationPhase>> iterationPhase)
    : request_(std::move(request)),
      stopRequested_(std::move(stopRequested)),
      cancellationRequested_(std::move(cancellationRequested)),
      iterationPhase_(std::move(iterationPhase)) {}

void SearchWorker::run() {
    emit stageChanged(QStringLiteral("Preparing search..."), true);

    SearchRunControl control;
    control.stopRequested = [flag = stopRequested_]() {
        return flag->load(std::memory_order_relaxed);
    };
    control.cancellationRequested = [flag = cancellationRequested_]() {
        return flag->load(std::memory_order_relaxed);
    };
    control.beginIteration = [phase = iterationPhase_]() {
        return TryBeginSearchIteration(phase);
    };
    control.progressChanged = [this](const SearchProgress &progress) {
        if (progress.stage == SearchProgressStage::FinalSampling) {
            const double value = progress.totalWork == 0u
                    ? 1.0
                    : static_cast<double>(progress.completedWork) /
                              static_cast<double>(progress.totalWork);
            const bool cuda =
                    PhysicsBackendId(request_.backend) == "cuda";
            const QString status = cuda
                    ? QStringLiteral(
                              "Sampling best run on CUDA: %1 of %2 ticks")
                    : QStringLiteral("Sampling best run: %1 of %2 ticks");
            emit progressChanged(
                    value,
                    status
                            .arg(static_cast<qulonglong>(
                                    progress.completedWork))
                            .arg(static_cast<qulonglong>(
                                    progress.totalWork)));
            return;
        }
        emit stageChanged(
                SearchStageStatus(
                        progress.stage, PhysicsBackendId(request_.backend)),
                true);
    };
    control.cudaBatchSizeChanged = [this](std::uint32_t batchSize) {
        emit cudaBatchSizeChanged(batchSize);
    };
    control.liveChanged = [this,
                           latestInputsText = QString(),
                           latestSource = SearchWinnerSource::Baseline,
                           latestIteration =
                                   std::optional<std::uint64_t>{},
                           throughput = RollingThroughput()](
                                  const SearchLiveUpdate &live) mutable {
        if (latestInputsText.isEmpty() ||
            latestSource != live.winnerSource ||
            latestIteration != live.winningIterationIndex) {
            latestInputsText = QString::fromStdString(
                    FormatInputScript(live.bestInputs));
            latestSource = live.winnerSource;
            latestIteration = live.winningIterationIndex;
        }
        emit metricsChanged(
                QString::number(static_cast<qulonglong>(live.iterations)),
                IterationsPerSecond(
                        throughput.Observe(live.iterations, live.elapsed)),
                RoundedDuration(live.elapsed));
        emit bestChanged(
                FormatLive(live, QStringLiteral("Current best")),
                latestInputsText);
    };

    try {
        SearchResult result = RunSearch(request_, &control);
        auto completion = std::make_shared<SearchCompletion>();
        completion->summary = FormatResult(result);
        completion->inputsText = QString::fromStdString(
                FormatInputScript(result.bestInputs));
        completion->packsDirectory =
                FilePathFromUtf8(request_.packDirectory);
        completion->replayPath = FilePathFromUtf8(request_.replayPath);
        const std::string_view backendId = PhysicsBackendId(request_.backend);
        completion->simulationBackendId = QString::fromLatin1(
                backendId.data(), static_cast<qsizetype>(backendId.size()));
        completion->bestInputs = std::move(result.bestInputs);
        completion->bestTimeline = std::move(result.bestTimeline);
        emit succeeded(std::move(completion));
    } catch (const SearchCancelled &) {
        emit cancelled();
    } catch (const std::exception &error) {
        emit failed(QString::fromUtf8(error.what()));
    } catch (...) {
        emit failed(QStringLiteral("Unexpected search failure"));
    }
    emit finished();
}

}  // namespace forevertas::app
