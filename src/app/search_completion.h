#ifndef FOREVERTAS_APP_SEARCH_COMPLETION_H
#define FOREVERTAS_APP_SEARCH_COMPLETION_H

#include "searches/search_algorithm.h"

#include <QMetaType>
#include <QString>

#include <memory>
#include <vector>

namespace forevertas::app {

struct SearchCompletion {
    QString summary;
    QString inputsText;
    QString packsDirectory;
    QString replayPath;
    std::vector<SandboxInputEvent> bestInputs;
    std::vector<SearchTimelineFrame> bestTimeline;
};

using SearchCompletionPtr = std::shared_ptr<const SearchCompletion>;

}  // namespace forevertas::app

Q_DECLARE_METATYPE(forevertas::app::SearchCompletionPtr)

#endif
