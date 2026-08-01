#ifndef FOREVERTAS_APP_INPUT_PREVIEW_BINDING_H
#define FOREVERTAS_APP_INPUT_PREVIEW_BINDING_H

#include <QMetaObject>

namespace forevertas::app {

class SearchController;

}  // namespace forevertas::app

namespace forevertas::viewer {

class RaceViewerController;

}  // namespace forevertas::viewer

namespace forevertas::app {

QMetaObject::Connection BindInputPreview(
        SearchController &controller,
        viewer::RaceViewerController &viewer);

}  // namespace forevertas::app

#endif
