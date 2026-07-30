#include "viewer/whiteboard_canvas_item.h"
#include "viewer/whiteboard_model.h"
#include "viewer/race_timeline_item.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>

namespace {

using forevertas::viewer::WhiteboardCanvasItem;
using forevertas::viewer::WhiteboardModel;

int OpaquePixelCount(const QImage &image) {
    int result = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0) {
                ++result;
            }
        }
    }
    return result;
}

QImage Render(const QVariantMap &drawing) {
    QImage image(400, 240, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    WhiteboardCanvasItem canvas;
    canvas.setWidth(image.width());
    canvas.setHeight(image.height());
    canvas.setDrawing(drawing);
    QPainter painter(&image);
    canvas.paint(&painter);
    painter.end();
    return image;
}

bool Near(double lhs, double rhs, double tolerance = 0.0001) {
    return std::abs(lhs - rhs) <= tolerance;
}

}  // namespace

int main(int argc, char **argv) {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(
            QStringLiteral("ForeverTAS Tests"));
    QCoreApplication::setApplicationName(
            QStringLiteral("Whiteboard Tests"));
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();

    int failures = 0;
    const auto expect = [&failures](bool condition, const char *message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    };

    WhiteboardModel model;
    expect(!model.active(), "whiteboard starts inactive");
    expect(model.count() == 0, "whiteboard starts empty");
    expect(model.tool() == QStringLiteral("select"),
           "select is the initial tool");
    expect(model.maximumCount() == 512, "item limit is exposed");
    expect(!model.beginItem(0.1, 0.1),
           "drawing is rejected while inactive");
    expect(model.addText(0.1, 0.1, QStringLiteral("note")) == -1,
           "text is rejected while inactive");

    model.setTool(QStringLiteral("unknown"));
    expect(model.tool() == QStringLiteral("select"),
           "unknown tools are rejected");
    model.setColor(QColor());
    expect(model.color().isValid(), "invalid colors are rejected");
    model.setSize(std::numeric_limits<double>::quiet_NaN());
    expect(Near(model.size(), 4.0), "non-finite sizes are rejected");
    model.setSize(-100.0);
    expect(Near(model.size(), 1.0), "size is clamped at the lower bound");
    model.setSize(100.0);
    expect(Near(model.size(), 24.0), "size is clamped at the upper bound");
    model.setSize(6.0);
    model.setColor(QColor(QStringLiteral("#42d3c6")));

    model.setActive(true);
    model.setTool(QStringLiteral("pen"));
    expect(model.beginItem(-1.0, 2.0),
           "pen accepts and clamps a finite starting point");
    expect(model.drawing(), "draft state is exposed");
    expect(model.updateItem(0.25, 0.35), "pen records a second point");
    expect(!model.updateItem(0.25001, 0.35001),
           "pen suppresses near-duplicate points");
    expect(model.updateItem(0.7, 0.65), "pen records subsequent points");
    expect(model.finishItem(), "pen gesture is committed");
    expect(!model.drawing() && model.count() == 1,
           "committed pen leaves drawing state");
    QVariantMap pen = model.items().front().toMap();
    expect(pen.value(QStringLiteral("type")).toString() ==
                   QStringLiteral("pen"),
           "pen item retains its vector type");
    expect(pen.value(QStringLiteral("points")).toList().size() == 3,
           "pen item retains vector points");
    expect(pen.value(QStringLiteral("color")).value<QColor>() ==
                   QColor(QStringLiteral("#42d3c6")),
           "pen item retains its chosen color");
    expect(Near(pen.value(QStringLiteral("strokeWidth")).toDouble(), 6.0),
           "pen item retains its chosen size");

    model.setTool(QStringLiteral("line"));
    expect(model.beginItem(0.1, 0.1), "line starts");
    expect(model.updateItem(0.9, 0.9), "line endpoint updates");
    expect(model.finishItem(), "line commits");
    expect(model.count() == 2, "line adds an independent item");

    model.setTool(QStringLiteral("rectangle"));
    expect(model.beginItem(0.8, 0.7), "rectangle starts");
    expect(model.updateItem(0.4, 0.2),
           "rectangle supports reverse-direction drawing");
    expect(model.finishItem(), "rectangle commits");
    const QVariantMap rectangle = model.items().back().toMap();
    expect(Near(rectangle.value(QStringLiteral("x")).toDouble(), 0.4) &&
                   Near(rectangle.value(QStringLiteral("y")).toDouble(), 0.2),
           "rectangle bounds are normalized");

    model.setTool(QStringLiteral("ellipse"));
    expect(model.beginItem(0.2, 0.3), "ellipse starts");
    expect(model.updateItem(0.6, 0.8), "ellipse bounds update");
    expect(model.finishItem(), "ellipse commits");
    expect(model.count() == 4, "all basic shapes are item based");

    expect(model.beginItem(0.4, 0.4), "tiny shape draft starts");
    expect(!model.finishItem(), "tiny shape draft is discarded");
    expect(model.count() == 4, "discarded draft leaves no placeholder");
    expect(model.beginItem(0.1, 0.1), "cancelable draft starts");
    model.cancelItem();
    expect(!model.drawing() && model.count() == 4,
           "cancel removes its draft");

    model.setTool(QStringLiteral("text"));
    expect(model.addText(0.95, 0.98, QStringLiteral("  Vector note  ")) == 4,
           "text is added as a distinct item");
    QVariantMap text = model.items().back().toMap();
    expect(text.value(QStringLiteral("type")).toString() ==
                   QStringLiteral("text") &&
                   text.value(QStringLiteral("text")).toString() ==
                           QStringLiteral("Vector note"),
           "text is normalized and remains editable");
    expect(text.value(QStringLiteral("x")).toDouble() +
                           text.value(QStringLiteral("width")).toDouble() <=
                   1.0001,
           "text is kept within the viewport");
    expect(model.setText(4, QStringLiteral("Edited annotation")),
           "text can be edited");
    expect(!model.setText(4, QStringLiteral("   ")),
           "empty edits are rejected");
    expect(!model.setText(0, QStringLiteral("not text")),
           "non-text items cannot be edited as text");

    model.setTool(QStringLiteral("select"));
    expect(model.selectItem(1), "line can be selected");
    const QVariantMap lineBeforeMove = model.selectedItem();
    expect(model.moveSelected(10.0, -10.0),
           "movement clamps instead of leaving the viewport");
    const QVariantMap lineAfterMove = model.selectedItem();
    expect(lineAfterMove.value(QStringLiteral("x")).toDouble() >= 0.0 &&
                   lineAfterMove.value(QStringLiteral("y")).toDouble() >=
                           0.0 &&
                   lineAfterMove.value(QStringLiteral("x")).toDouble() +
                                   lineAfterMove
                                           .value(QStringLiteral("width"))
                                           .toDouble() <=
                           1.0001,
           "moved items stay inside the viewport");
    expect(lineAfterMove.value(QStringLiteral("x")).toDouble() !=
                           lineBeforeMove.value(QStringLiteral("x")).toDouble(),
           "movement changes item bounds");
    const double oldWidth =
            lineAfterMove.value(QStringLiteral("width")).toDouble();
    expect(model.resizeSelected(-oldWidth * 0.5, 0.05),
           "selected items can be resized");
    const QVariantMap lineAfterResize = model.selectedItem();
    expect(lineAfterResize.value(QStringLiteral("width")).toDouble() <
                           oldWidth &&
                   lineAfterResize.value(QStringLiteral("height")).toDouble() >
                           0.0,
           "resize updates selected item dimensions");

    model.selectItem(1);
    model.moveSelected(
            0.1 - model.selectedItem().value(QStringLiteral("x")).toDouble(),
            0.1 - model.selectedItem().value(QStringLiteral("y")).toDouble());
    const QVariantMap lineBeforeErase = model.selectedItem();
    const QVariantList linePoints =
            lineBeforeErase.value(QStringLiteral("points")).toList();
    const QPointF first = linePoints.front().toPointF();
    const QPointF last = linePoints.back().toPointF();
    const QPointF midpoint = (first + last) * 0.5;
    const int opaqueBefore = OpaquePixelCount(Render(lineBeforeErase));
    expect(!model.eraseSelected(0.99, 0.01, 0.01),
           "eraser rejects points outside the selected item");
    expect(model.eraseSelected(midpoint.x(), midpoint.y(), 0.05),
           "eraser records a selective cut on the selected item");
    expect(!model.eraseSelected(midpoint.x(), midpoint.y(), 0.05),
           "eraser suppresses redundant adjacent samples");
    const QVariantMap lineAfterErase = model.selectedItem();
    const int opaqueAfter = OpaquePixelCount(Render(lineAfterErase));
    expect(lineAfterErase.value(QStringLiteral("erasures"))
                           .toList()
                           .size() == 1,
           "erasures are retained on the selected item only");
    expect(opaqueBefore > 0 && opaqueAfter < opaqueBefore,
           "vector rendering applies transparent pixel erasure");
    expect(model.items().front()
                           .toMap()
                           .value(QStringLiteral("erasures"))
                           .toList()
                           .isEmpty(),
           "erasing one selected item leaves other items unchanged");

    const QImage textImage = Render(model.items().back().toMap());
    expect(OpaquePixelCount(textImage) > 0,
           "text renders as scalable vector-backed glyphs");

    const int beforeRemove = model.count();
    expect(model.removeSelected(), "selected item can be deleted");
    expect(model.count() == beforeRemove - 1,
           "delete removes exactly the selected item");
    model.clearSelection();
    expect(model.selectedIndex() == -1, "selection can be cleared");
    expect(!model.removeSelected(), "delete rejects an empty selection");

    const int retainedCount = model.count();
    model.setActive(false);
    expect(model.count() == retainedCount,
           "switching to normal 3D view retains drawings");
    expect(!model.moveSelected(0.1, 0.1) &&
                   !model.eraseSelected(0.1, 0.1, 0.02),
           "inactive whiteboard rejects editing operations");
    model.setActive(true);
    expect(model.count() == retainedCount,
           "switching back restores all drawing items");

    WhiteboardModel capacityModel;
    capacityModel.setActive(true);
    capacityModel.setTool(QStringLiteral("text"));
    for (int index = 0; index < capacityModel.maximumCount(); ++index) {
        expect(capacityModel.addText(
                       0.1,
                       0.1,
                       QStringLiteral("Item %1").arg(index)) == index,
               "items can be added through the documented capacity");
    }
    expect(capacityModel.addText(
                   0.1, 0.1, QStringLiteral("One too many")) == -1 &&
                   capacityModel.count() ==
                           capacityModel.maximumCount(),
           "the item limit rejects overflow without corrupting drawings");

    QSettings().clear();
    WhiteboardModel repositoryModel;
    repositoryModel.setMapKey(
            QStringLiteral("/maps/Stadium.Replay.Gbx"));
    repositoryModel.setActive(true);
    repositoryModel.setTool(QStringLiteral("line"));
    expect(repositoryModel.beginItem(0.1, 0.2) &&
                   repositoryModel.updateItem(0.8, 0.7) &&
                   repositoryModel.finishItem(),
           "persistent board source drawing is created");
    const QVariantMap firstCapture{
            {QStringLiteral("targetX"), 12.0},
            {QStringLiteral("targetY"), 3.0},
            {QStringLiteral("targetZ"), -4.0},
            {QStringLiteral("yaw"), 35.0},
            {QStringLiteral("pitch"), -20.0},
            {QStringLiteral("distance"), 38.0},
            {QStringLiteral("planeX"), 7.0},
            {QStringLiteral("planeY"), 4.0},
            {QStringLiteral("planeZ"), 8.0},
            {QStringLiteral("planeWidth"), 12.0},
            {QStringLiteral("planeHeight"), 7.0}};
    expect(repositoryModel.captureCurrentBoard(
                   QStringLiteral("Entry line"), firstCapture) == 0 &&
                   repositoryModel.count() == 0 &&
                   repositoryModel.boardCount() == 1,
           "placing a drawing captures its plane and clears the draft");
    QVariantMap firstBoard =
            repositoryModel.boards().front().toMap();
    expect(firstBoard.value(QStringLiteral("name")).toString() ==
                           QStringLiteral("Entry line") &&
                   firstBoard.value(QStringLiteral("isCurrentMap"))
                           .toBool() &&
                   Near(firstBoard.value(
                                   QStringLiteral("targetX"))
                                .toDouble(),
                        12.0) &&
                   Near(firstBoard.value(
                                   QStringLiteral("planeWidth"))
                                .toDouble(),
                        12.0) &&
                   firstBoard.value(QStringLiteral("items"))
                                   .toList()
                                   .size() == 1,
           "placed drawing retains vector items and its saved viewpoint");

    repositoryModel.setTool(QStringLiteral("text"));
    expect(repositoryModel.addText(
                   0.2, 0.3, QStringLiteral("Brake here")) == 0,
           "a second persistent drawing can be authored");
    QVariantMap secondCapture = firstCapture;
    secondCapture[QStringLiteral("yaw")] = -42.0;
    secondCapture[QStringLiteral("pitch")] = 15.0;
    secondCapture[QStringLiteral("planeX")] = -9.0;
    expect(repositoryModel.captureCurrentBoard(
                   QStringLiteral("Exit note"), secondCapture) == 1 &&
                   repositoryModel.boardCount() == 2,
           "multiple drawings retain independent planes");
    expect(repositoryModel.selectBoard(0) &&
                   repositoryModel.selectedBoardIndex() == 0,
           "a current-map drawing can be selected for viewpoint restore");
    expect(repositoryModel.setBoardVisible(0, false) &&
                   !repositoryModel.boards()
                            .front()
                            .toMap()
                            .value(QStringLiteral("visible"))
                            .toBool() &&
                   repositoryModel.visibleBoards().size() == 1,
           "visibility hides a plane without removing its list entry");

    WhiteboardModel restoredRepository;
    restoredRepository.setMapKey(
            QStringLiteral("/maps/Stadium.Replay.Gbx"));
    expect(restoredRepository.boardCount() == 2 &&
                   !restoredRepository.boards()
                            .front()
                            .toMap()
                            .value(QStringLiteral("visible"))
                            .toBool(),
           "drawings and visibility persist across model sessions");
    restoredRepository.setMapKey(
            QStringLiteral("/maps/Other.Replay.Gbx"));
    expect(!restoredRepository.boards()
                    .front()
                    .toMap()
                    .value(QStringLiteral("isCurrentMap"))
                    .toBool() &&
                   restoredRepository.visibleBoards().isEmpty() &&
                   !restoredRepository.selectBoard(0),
           "other-map drawings remain listed but cannot alter this view");
    restoredRepository.setMapKey(
            QStringLiteral("/maps/Stadium.Replay.Gbx"));

    QTemporaryDir setDirectory;
    expect(setDirectory.isValid(),
           "temporary export directory is available");
    const QString setPath = setDirectory.filePath(
            QStringLiteral("Stadium notes.json"));
    expect(restoredRepository.exportBoardSet(
                   QUrl::fromLocalFile(setPath)) &&
                   QFileInfo(setPath).size() > 0,
           "a named set exports to the selected local file");

    QSettings().clear();
    WhiteboardModel importedRepository;
    importedRepository.setMapKey(
            QStringLiteral("/maps/Stadium.Replay.Gbx"));
    expect(importedRepository.importBoardSet(
                   QUrl::fromLocalFile(setPath)) &&
                   importedRepository.boardCount() == 2 &&
                   importedRepository.boards()
                                   .front()
                                   .toMap()
                                   .value(QStringLiteral("items"))
                                   .toList()
                                   .size() == 1,
           "an arbitrary exported set imports with complete drawings");
    const int beforeInvalidImport =
            importedRepository.boardCount();
    const QString invalidPath = setDirectory.filePath(
            QStringLiteral("invalid.json"));
    QFile invalidFile(invalidPath);
    expect(invalidFile.open(QIODevice::WriteOnly) &&
                   invalidFile.write("{\"boards\":[42]}") > 0,
           "invalid import fixture is written");
    invalidFile.close();
    expect(!importedRepository.importBoardSet(
                   QUrl::fromLocalFile(invalidPath)) &&
                   importedRepository.boardCount() ==
                           beforeInvalidImport,
           "invalid imports fail atomically without changing the list");
    QFile exportedSet(setPath);
    expect(exportedSet.open(QIODevice::ReadOnly),
           "exported set can be reopened for corruption testing");
    QByteArray invalidVisibility = exportedSet.readAll();
    exportedSet.close();
    invalidVisibility.replace(
            "\"visible\": false", "\"visible\": \"yes\"");
    const QString invalidVisibilityPath = setDirectory.filePath(
            QStringLiteral("invalid-visibility.json"));
    QFile invalidVisibilityFile(invalidVisibilityPath);
    expect(invalidVisibilityFile.open(QIODevice::WriteOnly) &&
                   invalidVisibilityFile.write(invalidVisibility) ==
                           invalidVisibility.size(),
           "type-confused import fixture is written");
    invalidVisibilityFile.close();
    expect(!importedRepository.importBoardSet(
                   QUrl::fromLocalFile(invalidVisibilityPath)) &&
                   importedRepository.boardCount() ==
                           beforeInvalidImport,
           "schema type errors are rejected atomically");

    QByteArray mapNeutralSet = invalidVisibility;
    mapNeutralSet.replace(
            "\"visible\": \"yes\"", "\"visible\": false");
    mapNeutralSet.replace(
            "/maps/Stadium.Replay.Gbx", "");
    const QString mapNeutralPath = setDirectory.filePath(
            QStringLiteral("map-neutral.json"));
    QFile mapNeutralFile(mapNeutralPath);
    expect(mapNeutralFile.open(QIODevice::WriteOnly) &&
                   mapNeutralFile.write(mapNeutralSet) ==
                           mapNeutralSet.size(),
           "map-neutral import fixture is written");
    mapNeutralFile.close();
    QSettings().clear();
    WhiteboardModel neutralRepository;
    expect(!neutralRepository.importBoardSet(
                   QUrl::fromLocalFile(mapNeutralPath)) &&
                   neutralRepository.boardCount() == 0,
           "map-neutral imports require a loaded map");
    neutralRepository.setMapKey(
            QStringLiteral("/maps/Stadium.Replay.Gbx"));
    expect(neutralRepository.importBoardSet(
                   QUrl::fromLocalFile(mapNeutralPath)) &&
                   neutralRepository.boardCount() == 2 &&
                   neutralRepository.visibleBoards().size() == 1 &&
                   neutralRepository.boards()
                                   .front()
                                   .toMap()
                                   .value(QStringLiteral("isCurrentMap"))
                                   .toBool(),
           "map-neutral imports attach atomically to the loaded map");

    expect(importedRepository.removeBoard(0) &&
                   importedRepository.boardCount() == 1,
           "drawings can be removed individually from the list");

    forevertas::viewer::RegisterRaceViewerQmlTypes();
    QQmlEngine engine;
    QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(
                    QStringLiteral(FOREVERTAS_SOURCE_DIR)
                    + QStringLiteral("/qml/WhiteboardOverlay.qml")));
    if (component.isError()) {
        for (const QQmlError &error : component.errors()) {
            std::cerr << error.toString().toStdString() << '\n';
        }
    }
    std::unique_ptr<QObject> overlay(component.createWithInitialProperties({
            {QStringLiteral("model"),
             QVariant::fromValue(static_cast<QObject *>(&model))},
            {QStringLiteral("available"), true}}));
    expect(overlay != nullptr, "whiteboard overlay QML instantiates");
    if (overlay != nullptr) {
        overlay->setProperty("width", 1000.0);
        overlay->setProperty("height", 700.0);
        QCoreApplication::processEvents();
        auto *const board = qobject_cast<QQuickItem *>(
                overlay->findChild<QObject *>(
                        QStringLiteral("whiteboardBoardArea")));
        auto *const toolbar = qobject_cast<QQuickItem *>(
                overlay->findChild<QObject *>(
                        QStringLiteral("whiteboardToolbar")));
        QObject *const toggle = overlay->findChild<QObject *>(
                QStringLiteral("whiteboardModeToggle"));
        auto *const toggleItem = qobject_cast<QQuickItem *>(toggle);
        QObject *const input = overlay->findChild<QObject *>(
                QStringLiteral("whiteboardDrawingInput"));
        QObject *const sizeSlider = overlay->findChild<QObject *>(
                QStringLiteral("whiteboardSizeSlider"));
        QObject *const customColor = overlay->findChild<QObject *>(
                QStringLiteral("whiteboardCustomColor"));
        QObject *const textEditor = overlay->findChild<QObject *>(
                QStringLiteral("whiteboardTextEditor"));
        auto *const deleteButton = qobject_cast<QQuickItem *>(
                overlay->findChild<QObject *>(
                        QStringLiteral("whiteboardDeleteButton")));
        QObject *const toolRepeater = overlay->findChild<QObject *>(
                QStringLiteral("whiteboardToolRepeater"));
        QObject *const drawingRepeater = overlay->findChild<QObject *>(
                QStringLiteral("whiteboardDrawingRepeater"));
        QObject *const placeButton = overlay->findChild<QObject *>(
                QStringLiteral("whiteboardPlaceButton"));
        QObject *const drawingList = overlay->findChild<QObject *>(
                QStringLiteral("whiteboardDrawingList"));
        QObject *const importButton = overlay->findChild<QObject *>(
                QStringLiteral("whiteboardImportButton"));
        QObject *const exportButton = overlay->findChild<QObject *>(
                QStringLiteral("whiteboardExportButton"));
        auto *const toolbarContent = qobject_cast<QQuickItem *>(
                overlay->findChild<QObject *>(
                        QStringLiteral("whiteboardToolbarContent")));
        expect(board != nullptr && Near(board->y(), 52.0) &&
                       Near(board->height(), 648.0),
               "drawing surface overlays the viewer below its header");
        expect(toolbar != nullptr && toolbar->width() <= 972.0 &&
                       toolbar->height() == 84.0,
               "active toolbar fits a desktop viewport");
        expect(toggle != nullptr &&
                       toggle->property("enabled").toBool() &&
                       toggle->property("checked").toBool(),
               "available mode toggle reflects active state");
        expect(input != nullptr && input->property("enabled").toBool(),
               "active whiteboard captures drawing input");
        expect(sizeSlider != nullptr &&
                       sizeSlider->property("visible").toBool() &&
                       customColor != nullptr &&
                       textEditor != nullptr,
               "size, arbitrary color, and text controls are connected");
        if (sizeSlider != nullptr) {
            sizeSlider->setProperty("value", 9.0);
            QCoreApplication::processEvents();
            expect(Near(model.size(), 9.0),
                   "keyboard and accessibility slider changes update size");
        }
        expect(toolRepeater != nullptr &&
                       toolRepeater->property("count").toInt() == 7,
               "every whiteboard tool has a mode control");
        expect(placeButton != nullptr && drawingList != nullptr &&
                       importButton != nullptr &&
                       exportButton != nullptr,
               "placement, list, import, and export controls are present");
        expect(drawingRepeater != nullptr &&
                       drawingRepeater->property("count").toInt() ==
                               retainedCount,
               "each item is represented by an independent canvas delegate");

        overlay->setProperty("width", 520.0);
        overlay->setProperty("height", 390.0);
        QCoreApplication::processEvents();
        const QPointF deletePosition =
                deleteButton != nullptr && toolbar != nullptr
                ? deleteButton->mapToItem(toolbar, QPointF())
                : QPointF(-1.0, -1.0);
        const bool compactLayout =
                toolbar != nullptr && toolbar->width() <= 492.0 &&
                Near(toolbar->height(), 84.0) &&
                board != nullptr && Near(board->width(), 520.0) &&
                Near(board->height(), 338.0) &&
                deleteButton != nullptr &&
                deletePosition.x() + deleteButton->width() <=
                        toolbar->width() - 7.0 &&
                deletePosition.y() + deleteButton->height() <=
                        toolbar->height() - 3.0;
        if (!compactLayout && toolbar != nullptr &&
            board != nullptr && deleteButton != nullptr) {
            std::cerr << "compact layout: toolbar="
                      << toolbar->width() << 'x' << toolbar->height()
                      << ", flow="
                      << (toolbarContent != nullptr
                                  ? toolbarContent->width()
                                  : -1.0)
                      << 'x'
                      << (toolbarContent != nullptr
                                  ? toolbarContent->height()
                                  : -1.0)
                      << ", toggle="
                      << (toggleItem != nullptr ? toggleItem->x() : -1.0)
                      << ','
                      << (toggleItem != nullptr ? toggleItem->y() : -1.0)
                      << ' '
                      << (toggleItem != nullptr ? toggleItem->width() : -1.0)
                      << 'x'
                      << (toggleItem != nullptr ? toggleItem->height() : -1.0)
                      << ", board=" << board->width() << 'x'
                      << board->height() << ", delete="
                      << deletePosition.x() << ',' << deletePosition.y()
                      << ' ' << deleteButton->width() << 'x'
                      << deleteButton->height() << '\n';
        }
        expect(compactLayout,
               "whiteboard remains bounded at a compact window size");

        textEditor->setProperty("visible", true);
        model.setActive(false);
        QCoreApplication::processEvents();
        expect(toolbar != nullptr && Near(toolbar->width(), 198.0) &&
                       input != nullptr &&
                       !input->property("enabled").toBool() &&
                       toggle != nullptr &&
                       !toggle->property("checked").toBool() &&
                       !textEditor->property("visible").toBool(),
               "normal 3D mode collapses controls and releases input");
        expect(drawingRepeater != nullptr &&
                       drawingRepeater->property("count").toInt() ==
                               retainedCount &&
                       model.count() == retainedCount,
               "drawings remain visible when normal 3D interaction resumes");

        overlay->setProperty("available", false);
        QCoreApplication::processEvents();
        expect(toggle != nullptr &&
                       !toggle->property("enabled").toBool(),
               "empty viewer disables whiteboard activation");
    }

    QQmlComponent planesComponent(
            &engine,
            QUrl::fromLocalFile(
                    QStringLiteral(FOREVERTAS_SOURCE_DIR)
                    + QStringLiteral("/qml/WhiteboardPlanes.qml")));
    if (planesComponent.isError()) {
        for (const QQmlError &error : planesComponent.errors()) {
            std::cerr << error.toString().toStdString() << '\n';
        }
    }
    std::unique_ptr<QObject> planes(
            planesComponent.createWithInitialProperties({
                    {QStringLiteral("model"),
                     QVariant::fromValue(static_cast<QObject *>(
                             &importedRepository))},
                    {QStringLiteral("cameraTarget"),
                     QVariant::fromValue(QVector3D())},
                    {QStringLiteral("orbitYaw"), 35.0},
                    {QStringLiteral("orbitPitch"), -20.0},
                    {QStringLiteral("orbitDistance"), 38.0}}));
    expect(planes != nullptr,
           "persistent whiteboard plane view instantiates");
    if (planes != nullptr) {
        QCoreApplication::processEvents();
        QObject *const planeRepeater = planes->findChild<QObject *>(
                QStringLiteral("whiteboardPlaneRepeater"));
        expect(planeRepeater != nullptr &&
                       planeRepeater->property("count").toInt() ==
                               importedRepository.boardCount(),
               "every listed drawing has an independent 3D plane delegate");
    }

    QSettings().clear();
    if (failures == 0) {
        std::cout << "whiteboard model and vector rendering tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
