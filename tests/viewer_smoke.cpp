#include "viewer/race_timeline_item.h"
#include "viewer/race_viewer_controller.h"

#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

using forevertas::viewer::RaceTimelineItem;
using forevertas::viewer::RaceViewerController;

qint64 FindActivityTick(const RaceViewerController &viewer, char channel) {
    for (qint64 tick = 0; tick < viewer.tickCount(); ++tick) {
        const auto sample = viewer.inputSample(tick);
        const bool active = channel == 'l'
                ? sample.steering < -0.01f
                : channel == 'r'
                ? sample.steering > 0.01f
                : channel == 'a'
                ? sample.accelerate > 0.0f
                : sample.brake > 0.0f;
        if (active) {
            return tick;
        }
    }
    return -1;
}

bool TimelinePaintsColor(RaceTimelineItem &timeline,
                         RaceViewerController &viewer,
                         qint64 tick,
                         const QColor &color) {
    if (tick < 0) {
        return false;
    }
    viewer.setCurrentTick(tick);
    QImage image(252, 600, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    timeline.paint(&painter);
    painter.end();
    const QRgb expected = color.rgba();
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(
                image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (line[x] == expected) {
                return true;
            }
        }
    }
    return false;
}

int TimelineColorRow(RaceTimelineItem &timeline,
                     const QColor &color,
                     int minimumY,
                     int maximumY) {
    QImage image(252, 600, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    timeline.paint(&painter);
    painter.end();
    const QRgb expected = color.rgba();
    const int firstY = std::clamp(minimumY, 0, image.height() - 1);
    const int lastY = std::clamp(maximumY, firstY, image.height() - 1);
    for (int y = firstY; y <= lastY; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(
                image.constScanLine(y));
        if (line[60] == expected) {
            return y;
        }
    }
    return -1;
}

int TimelineGridRowCount(RaceTimelineItem &timeline,
                         int minimumY,
                         int maximumY) {
    QImage image(252, 600, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    timeline.paint(&painter);
    painter.end();
    const QRgb regular = QColor(QStringLiteral("#080a09")).rgba();
    const QRgb tenth = QColor(QStringLiteral("#0d100e")).rgba();
    const QRgb second = QColor(QStringLiteral("#171d19")).rgba();
    const int firstY = std::clamp(minimumY, 0, image.height() - 1);
    const int lastY = std::clamp(maximumY, firstY, image.height() - 1);
    int rows = 0;
    for (int y = firstY; y <= lastY; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(
                image.constScanLine(y));
        if (line[60] == regular || line[60] == tenth ||
            line[60] == second) {
            ++rows;
        }
    }
    return rows;
}

void SendTimelineMouseEvent(RaceTimelineItem &timeline,
                            QEvent::Type type,
                            Qt::MouseButton button,
                            Qt::MouseButtons buttons,
                            const QPointF &position) {
    QMouseEvent event(
            type,
            position,
            position,
            button,
            buttons,
            Qt::NoModifier);
    QCoreApplication::sendEvent(&timeline, &event);
}

void DragTimeline(RaceTimelineItem &timeline,
                  Qt::MouseButton button,
                  const QPointF &start,
                  const QPointF &end) {
    SendTimelineMouseEvent(
            timeline, QEvent::MouseButtonPress, button, button, start);
    SendTimelineMouseEvent(
            timeline, QEvent::MouseMove, Qt::NoButton, button, end);
    SendTimelineMouseEvent(
            timeline, QEvent::MouseButtonRelease, button, Qt::NoButton, end);
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: forevertas-viewer-smoke <Packs> <replay>\n";
        return 2;
    }

    QGuiApplication application(argc, argv);
    RaceViewerController viewer;
    int exitCode = 1;
    bool completed = false;
    bool verificationStarted = false;
    QObject::connect(
            &viewer,
            &forevertas::viewer::RaceViewerController::stateChanged,
            &application,
            [&]() {
                if (completed || verificationStarted) {
                    return;
                }
                if (viewer.loading()) {
                    return;
                }
                if (viewer.loaded()) {
                    verificationStarted = true;
                    std::cout << viewer.triangleCount() << " triangles, "
                              << viewer.ellipsoidCount() << " ellipsoids, "
                              << viewer.durationMs() << " ms, "
                              << viewer.tickCount() << " ticks\n";

                    RaceTimelineItem timeline;
                    timeline.setWidth(252);
                    timeline.setHeight(600);
                    timeline.setViewer(&viewer);
                    const qint64 leftSteeringTick =
                            FindActivityTick(viewer, 'l');
                    const qint64 rightSteeringTick =
                            FindActivityTick(viewer, 'r');
                    const qint64 accelerationTick =
                            FindActivityTick(viewer, 'a');
                    const qint64 brakeTick = FindActivityTick(viewer, 'b');
                    const bool leftSteeringPainted = TimelinePaintsColor(
                            timeline,
                            viewer,
                            leftSteeringTick,
                            QColor(QStringLiteral("#4f9ddd")));
                    const bool rightSteeringPainted = TimelinePaintsColor(
                            timeline,
                            viewer,
                            rightSteeringTick,
                            QColor(QStringLiteral("#4f9ddd")));
                    const bool accelerationPainted = TimelinePaintsColor(
                                    timeline,
                                    viewer,
                                    accelerationTick,
                                    QColor(QStringLiteral("#3dbd73")));
                    const bool brakePainted = TimelinePaintsColor(
                                    timeline,
                                    viewer,
                                    brakeTick,
                                    QColor(QStringLiteral("#df5555")));
                    const bool timelineInputs = leftSteeringPainted &&
                            rightSteeringPainted &&
                            accelerationPainted && brakePainted;

                    viewer.setCurrentTick(
                            std::min<qint64>(500, viewer.tickCount() - 1));
                    timeline.setPixelsPerTick(3.0);
                    const qint64 dragStartTimeMs = viewer.timeMs();
                    SendTimelineMouseEvent(
                            timeline,
                            QEvent::MouseButtonPress,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            QPointF(126.0, 70.0));
                    const bool leftPressDoesNotSnap =
                            viewer.timeMs() == dragStartTimeMs;
                    SendTimelineMouseEvent(
                            timeline,
                            QEvent::MouseMove,
                            Qt::NoButton,
                            Qt::LeftButton,
                            QPointF(126.0, 100.0));
                    SendTimelineMouseEvent(
                            timeline,
                            QEvent::MouseButtonRelease,
                            Qt::LeftButton,
                            Qt::NoButton,
                            QPointF(126.0, 100.0));
                    const bool naturalScrubDirection =
                            viewer.timeMs() < dragStartTimeMs;

                    timeline.setPixelsPerTick(4.0);
                    viewer.setTimeMs(5010);
                    const int wholeTickLineY = TimelineColorRow(
                            timeline,
                            QColor(QStringLiteral("#171d19")),
                            280,
                            320);
                    viewer.setTimeMs(5015);
                    const int halfTickLineY = TimelineColorRow(
                            timeline,
                            QColor(QStringLiteral("#171d19")),
                            280,
                            320);
                    timeline.setPixelsPerTick(8.0);
                    const int zoomedHalfTickLineY = TimelineColorRow(
                            timeline,
                            QColor(QStringLiteral("#171d19")),
                            270,
                            320);
                    const bool gridTracksScrollAndZoom =
                            wholeTickLineY == 296 &&
                            halfTickLineY == 294 &&
                            zoomedHalfTickLineY == 288;

                    timeline.setPixelsPerTick(3.0);
                    viewer.setTimeMs(5020);
                    SendTimelineMouseEvent(
                            timeline,
                            QEvent::MouseButtonPress,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            QPointF(126.0, 70.0));
                    SendTimelineMouseEvent(
                            timeline,
                            QEvent::MouseMove,
                            Qt::NoButton,
                            Qt::LeftButton,
                            QPointF(126.0, 71.0));
                    const bool gridVisibleWhileDragging =
                            TimelineGridRowCount(timeline, 301, 310) > 0;
                    SendTimelineMouseEvent(
                            timeline,
                            QEvent::MouseButtonRelease,
                            Qt::LeftButton,
                            Qt::NoButton,
                            QPointF(126.0, 71.0));
                    const bool gridVisibleAfterDragging =
                            TimelineGridRowCount(timeline, 301, 310) > 0;
                    timeline.setPixelsPerTick(1.0);
                    const bool gridVisibleAtMinimumZoom =
                            TimelineGridRowCount(timeline, 301, 310) > 0;
                    const int minimumZoomGridRows =
                            TimelineGridRowCount(timeline, 250, 349);
                    const bool minimumZoomGridStaysSparse =
                            minimumZoomGridRows >= 25 &&
                            minimumZoomGridRows <= 45;
                    const bool gridAlwaysVisible =
                            gridVisibleWhileDragging &&
                            gridVisibleAfterDragging &&
                            gridVisibleAtMinimumZoom &&
                            minimumZoomGridStaysSparse;

                    timeline.setPixelsPerTick(3.0);
                    DragTimeline(
                            timeline,
                            Qt::RightButton,
                            QPointF(126.0, 300.0),
                            QPointF(126.0, 240.0));
                    const bool rightDragZoomsIn =
                            timeline.pixelsPerTick() > 3.0;

                    viewer.setCurrentTick(100);
                    const bool timeLabelUnambiguous =
                            viewer.timeText().startsWith(
                                    QStringLiteral("00:01.000 / "));
                    const bool sceneValid = viewer.triangleCount() > 0 &&
                            viewer.ellipsoidCount() > 0 &&
                            viewer.durationMs() > 0 &&
                            viewer.tickCount() ==
                                    viewer.durationMs() /
                                                    viewer.tickDurationMs() +
                                            1 &&
                            timelineInputs && naturalScrubDirection &&
                            leftPressDoesNotSnap && gridTracksScrollAndZoom &&
                            gridAlwaysVisible &&
                            rightDragZoomsIn && timeLabelUnambiguous;
                    if (!sceneValid) {
                        std::cerr
                                << "viewer scene checks failed: leftSteeringTick="
                                << leftSteeringTick
                                << ", rightSteeringTick="
                                << rightSteeringTick
                                << ", accelerationTick="
                                << accelerationTick
                                << ", brakeTick="
                                << brakeTick
                                << ", leftSteeringPainted="
                                << leftSteeringPainted
                                << ", rightSteeringPainted="
                                << rightSteeringPainted
                                << ", accelerationPainted="
                                << accelerationPainted
                                << ", brakePainted="
                                << brakePainted
                                << ", naturalScrubDirection="
                                << naturalScrubDirection
                                << ", leftPressDoesNotSnap="
                                << leftPressDoesNotSnap
                                << ", wholeTickLineY="
                                << wholeTickLineY
                                << ", halfTickLineY="
                                << halfTickLineY
                                << ", zoomedHalfTickLineY="
                                << zoomedHalfTickLineY
                                << ", gridTracksScrollAndZoom="
                                << gridTracksScrollAndZoom
                                << ", gridVisibleWhileDragging="
                                << gridVisibleWhileDragging
                                << ", gridVisibleAfterDragging="
                                << gridVisibleAfterDragging
                                << ", gridVisibleAtMinimumZoom="
                                << gridVisibleAtMinimumZoom
                                << ", minimumZoomGridRows="
                                << minimumZoomGridRows
                                << ", minimumZoomGridStaysSparse="
                                << minimumZoomGridStaysSparse
                                << ", rightDragZoomsIn="
                                << rightDragZoomsIn
                                << ", timeLabelUnambiguous="
                                << timeLabelUnambiguous << '\n';
                    }

                    viewer.jumpToStart();
                    viewer.play();
                    QTimer::singleShot(90, &application, [&, sceneValid]() {
                        const bool playbackAdvanced = viewer.playing() &&
                                viewer.currentTick() >= 4;
                        viewer.pause();
                        viewer.jumpToEnd();
                        const bool endPaused = !viewer.playing() &&
                                viewer.timeMs() == viewer.durationMs();
                        viewer.play();
                        QTimer::singleShot(
                                70,
                                &application,
                                [&, sceneValid, playbackAdvanced,
                                 endPaused]() {
                                    const bool restartedFromEnd =
                                            viewer.playing() &&
                                            viewer.currentTick() > 0 &&
                                            viewer.currentTick() <
                                                    viewer.tickCount() - 1;
                                    viewer.jumpToStart();
                                    const bool startPaused =
                                            !viewer.playing() &&
                                            viewer.currentTick() == 0;
                                    completed = true;
                                    exitCode = sceneValid &&
                                                    playbackAdvanced &&
                                                    endPaused &&
                                                    restartedFromEnd &&
                                                    startPaused
                                            ? 0
                                            : 1;
                                    if (exitCode != 0) {
                                        std::cerr
                                                << "viewer playback failed: sceneValid="
                                                << sceneValid
                                                << ", playbackAdvanced="
                                                << playbackAdvanced
                                                << ", endPaused="
                                                << endPaused
                                                << ", restartedFromEnd="
                                                << restartedFromEnd
                                                << ", startPaused="
                                                << startPaused << '\n';
                                    }
                                    application.quit();
                                });
                    });
                    return;
                }
                if (viewer.statusText() != QStringLiteral("No replay loaded")) {
                    completed = true;
                    std::cerr << viewer.statusText().toStdString() << '\n';
                    application.quit();
                }
            });
    QTimer::singleShot(170000, &application, [&]() {
        if (completed) {
            return;
        }
        completed = true;
        std::cerr << "viewer loading timed out\n";
        application.quit();
    });
    viewer.loadReplay(QString::fromLocal8Bit(argv[1]),
                      QString::fromLocal8Bit(argv[2]));
    application.exec();
    return exitCode;
}
