#include "app/search_worker.h"

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

QString FormattedDuration(std::chrono::steady_clock::duration duration) {
    return QString::fromStdString(FormatHumanDuration(duration));
}

QString IterationsPerSecond(const SearchLiveUpdate &live) {
    const double seconds =
            std::chrono::duration<double>(live.elapsed).count();
    const double rate = seconds <= 0.0
            ? 0.0
            : static_cast<double>(live.iterations) / seconds;
    return QString::number(static_cast<qlonglong>(std::llround(rate)));
}

QString RoundedElapsedDuration(
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
    return FormattedDuration(age) + QStringLiteral(" ago");
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

}  // namespace

SearchWorker::SearchWorker(
        SearchRequest request,
        std::shared_ptr<std::atomic_bool> stopRequested,
        std::shared_ptr<std::atomic_bool> cancellationRequested)
    : request_(std::move(request)),
      stopRequested_(std::move(stopRequested)),
      cancellationRequested_(std::move(cancellationRequested)) {}

void SearchWorker::run() {
    emit stageChanged(
            QStringLiteral("Loading replay and Packs data..."), true);

    SearchRunControl control;
    control.stopRequested = [flag = stopRequested_]() {
        return flag->load(std::memory_order_relaxed);
    };
    control.cancellationRequested = [flag = cancellationRequested_]() {
        return flag->load(std::memory_order_relaxed);
    };
    control.progressChanged = [this](const SearchProgress &progress) {
        if (progress.stage == SearchProgressStage::Baseline) {
            emit stageChanged(
                    QStringLiteral("Evaluating baseline..."), true);
            return;
        }

        if (progress.stage == SearchProgressStage::Mutations) {
            emit stageChanged(QStringLiteral("Searching..."), true);
            return;
        }

        if (progress.stage == SearchProgressStage::FinalSampling) {
            const double value = progress.totalWork == 0u
                    ? 1.0
                    : static_cast<double>(progress.completedWork) /
                              static_cast<double>(progress.totalWork);
            emit progressChanged(
                    value,
                    QStringLiteral("Sampling best run: %1 of %2 ticks")
                            .arg(static_cast<qulonglong>(
                                    progress.completedWork))
                            .arg(static_cast<qulonglong>(
                                    progress.totalWork)));
        }
    };
    control.liveChanged = [this,
                           latestInputsText = QString(),
                           latestSource = SearchWinnerSource::Baseline,
                           latestIteration =
                                   std::optional<std::uint64_t>{}](
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
                IterationsPerSecond(live),
                RoundedElapsedDuration(live.elapsed));
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
                QString::fromStdString(request_.packDirectory);
        completion->replayPath = QString::fromStdString(request_.replayPath);
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
