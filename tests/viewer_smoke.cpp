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

void DragTimeline(RaceTimelineItem &timeline,
                  Qt::MouseButton button,
                  const QPointF &start,
                  const QPointF &end) {
    QMouseEvent press(
            QEvent::MouseButtonPress,
            start,
            start,
            button,
            button,
            Qt::NoModifier);
    QCoreApplication::sendEvent(&timeline, &press);

    QMouseEvent move(
            QEvent::MouseMove,
            end,
            end,
            Qt::NoButton,
            button,
            Qt::NoModifier);
    QCoreApplication::sendEvent(&timeline, &move);

    QMouseEvent release(
            QEvent::MouseButtonRelease,
            end,
            end,
            button,
            Qt::NoButton,
            Qt::NoModifier);
    QCoreApplication::sendEvent(&timeline, &release);
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
                    const qint64 dragStartTick = viewer.currentTick();
                    DragTimeline(
                            timeline,
                            Qt::LeftButton,
                            QPointF(126.0, 300.0),
                            QPointF(126.0, 330.0));
                    const bool naturalScrubDirection =
                            viewer.currentTick() < dragStartTick;

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
                            rightDragZoomsIn && timeLabelUnambiguous;

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
                                                << "viewer controls failed: sceneValid="
                                                << sceneValid
                                                << ", leftSteeringTick="
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
                                                << ", rightDragZoomsIn="
                                                << rightDragZoomsIn
                                                << ", timeLabelUnambiguous="
                                                << timeLabelUnambiguous
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
