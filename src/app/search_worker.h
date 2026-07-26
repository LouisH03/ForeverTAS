#ifndef FOREVERTAS_APP_SEARCH_WORKER_H
#define FOREVERTAS_APP_SEARCH_WORKER_H

#include "app/search_completion.h"
#include "searches/search_runner.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

namespace forevertas::app {

class SearchWorker final : public QObject {
    Q_OBJECT

public:
    SearchWorker(SearchRequest request,
                 std::shared_ptr<std::atomic_bool> stopRequested,
                 std::shared_ptr<std::atomic_bool> cancellationRequested);

public slots:
    void run();

signals:
    void stageChanged(const QString &status, bool indeterminate);
    void progressChanged(double value, const QString &status);
    void metricsChanged(const QString &iterationCountText,
                        const QString &throughputText,
                        const QString &elapsedText);
    void cudaBatchSizeChanged(std::uint32_t batchSize);
    void bestChanged(const QString &summary, const QString &inputsText);
    void succeeded(forevertas::app::SearchCompletionPtr completion);
    void cancelled();
    void failed(const QString &message);
    void finished();

private:
    SearchRequest request_;
    std::shared_ptr<std::atomic_bool> stopRequested_;
    std::shared_ptr<std::atomic_bool> cancellationRequested_;
};

}  // namespace forevertas::app

#endif
