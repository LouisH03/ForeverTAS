#include "app/input_preview_binding.h"

#include "app/search_controller.h"
#include "viewer/race_viewer_controller.h"

namespace forevertas::app {

QMetaObject::Connection BindInputPreview(
        SearchController &controller,
        viewer::RaceViewerController &viewer) {
    viewer.setPreviewInputScript(controller.baseInputScript());
    bool horizonOk = false;
    const qint64 horizon =
            controller.simulationHorizonMs().toLongLong(&horizonOk);
    if (horizonOk) viewer.setSimulationHorizonMs(horizon);
    QObject::connect(
            &controller,
            &SearchController::simulationHorizonMsChanged,
            &viewer,
            [&controller, &viewer]() {
                bool ok = false;
                const qint64 value =
                        controller.simulationHorizonMs().toLongLong(&ok);
                if (ok) viewer.setSimulationHorizonMs(value);
            });
    return QObject::connect(
            &controller,
            &SearchController::baseInputScriptChanged,
            &viewer,
            [&controller, &viewer]() {
                viewer.setPreviewInputScript(controller.baseInputScript());
            });
}

}  // namespace forevertas::app
