#include "app/input_preview_binding.h"

#include "app/search_controller.h"
#include "viewer/race_viewer_controller.h"

namespace forevertas::app {

QMetaObject::Connection BindInputPreview(
        SearchController &controller,
        viewer::RaceViewerController &viewer) {
    viewer.setPreviewInputScript(controller.baseInputScript());
    return QObject::connect(
            &controller,
            &SearchController::baseInputScriptChanged,
            &viewer,
            [&controller, &viewer]() {
                viewer.setPreviewInputScript(controller.baseInputScript());
            });
}

}  // namespace forevertas::app
