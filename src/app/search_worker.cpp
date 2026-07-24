#include "app/search_worker.h"

#include "mutations/input_event_formatter.h"

#include <chrono>
#include <exception>
#include <utility>

namespace forevertas::app {
namespace {

QString FormatResult(const SearchResult &result) {
    const auto &state = result.bestState;
    const double elapsedMs =
            std::chrono::duration<double, std::milli>(result.elapsed).count();

    QString winner =
            result.winnerSource == SearchWinnerSource::Baseline
            ? QStringLiteral("Baseline")
            : QStringLiteral("Mutation");
    if (result.winningAttempt) {
        winner += QStringLiteral(" #%1")
                          .arg(static_cast<qulonglong>(
                                  *result.winningAttempt + 1u));
    }

    return QStringLiteral(
                   "Winner: %1\n"
                   "%2\n"
                   "State tick: %3  State time: %4 ms\n"
                   "Attempts: %5/%6  Skipped: %7\n"
                   "Improvements: %8  Elapsed: %9 ms")
            .arg(winner)
            .arg(QString::fromStdString(result.bestEvaluationDescription))
            .arg(static_cast<qulonglong>(state.tick))
            .arg(static_cast<qulonglong>(state.timeMs))
            .arg(static_cast<qulonglong>(result.executedAttempts))
            .arg(static_cast<qulonglong>(result.requestedAttempts))
            .arg(static_cast<qulonglong>(result.skippedAttempts))
            .arg(static_cast<qulonglong>(
                    result.mutationImprovementCount))
            .arg(elapsedMs, 0, 'f', 1);
}

}  // namespace

SearchWorker::SearchWorker(
        SearchRequest request,
        std::shared_ptr<std::atomic_bool> cancellationRequested)
    : request_(std::move(request)),
      cancellationRequested_(std::move(cancellationRequested)) {}

void SearchWorker::run() {
    emit stageChanged(
            QStringLiteral("Loading replay and Packs data..."), true);

    SearchRunControl control;
    control.cancellationRequested = [flag = cancellationRequested_]() {
        return flag->load(std::memory_order_relaxed);
    };
    control.progressChanged = [this](const SearchProgress &progress) {
        if (progress.stage == SearchProgressStage::Baseline) {
            emit stageChanged(
                    QStringLiteral("Evaluating baseline..."), true);
            return;
        }

        if (progress.stage == SearchProgressStage::FinalSampling) {
            const double value = progress.requestedAttempts == 0u
                    ? 1.0
                    : static_cast<double>(progress.completedAttempts) /
                              static_cast<double>(progress.requestedAttempts);
            emit progressChanged(
                    value,
                    QStringLiteral("Sampling best run: %1 of %2 ticks")
                            .arg(static_cast<qulonglong>(
                                    progress.completedAttempts))
                            .arg(static_cast<qulonglong>(
                                    progress.requestedAttempts)));
            return;
        }

        const double value = progress.requestedAttempts == 0u
                ? 0.0
                : static_cast<double>(progress.completedAttempts) /
                          static_cast<double>(progress.requestedAttempts);
        emit progressChanged(
                value,
                QStringLiteral("Trying mutations: %1 of %2")
                        .arg(static_cast<qulonglong>(
                                progress.completedAttempts))
                        .arg(static_cast<qulonglong>(
                                progress.requestedAttempts)));
    };

    try {
        SearchResult result = RunSearch(request_, &control);
        auto completion = std::make_shared<SearchCompletion>();
        completion->summary = FormatResult(result);
        completion->inputsText = QString::fromStdString(
                FormatTmInterfaceInputs(result.bestInputs));
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
