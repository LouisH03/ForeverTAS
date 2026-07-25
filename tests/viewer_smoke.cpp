#include "viewer/race_timeline_item.h"
#include "viewer/race_viewer_controller.h"

#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
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

QImage RenderTimeline(RaceTimelineItem &timeline) {
    QImage image(252, 600, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    timeline.paint(&painter);
    painter.end();
    return image;
}

bool PixelIs(const QImage &image, int x, int y, const QColor &color) {
    return image.pixelColor(x, y) == color;
}

bool PixelIsAnyRulerMark(const QImage &image, int x, int y) {
    const QColor pixel = image.pixelColor(x, y);
    return pixel == QColor(QStringLiteral("#9aa69e")) ||
            pixel == QColor(QStringLiteral("#59635d")) ||
            pixel == QColor(QStringLiteral("#343b37"));
}

int RulerMarkLengthNear(const QImage &image, int expectedY) {
    const QColor rulerBackground(QStringLiteral("#0c100e"));
    int longest = 0;
    for (int y = expectedY - 1; y <= expectedY + 1; ++y) {
        if (y < 0 || y >= image.height()) {
            continue;
        }
        int leftmost = 50;
        for (int x = 35; x < 50; ++x) {
            if (image.pixelColor(x, y) != rulerBackground) {
                leftmost = x;
                break;
            }
        }
        longest = std::max(longest, 50 - leftmost);
    }
    return longest;
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
                              << viewer.visualTriangleCount()
                              << " visual triangles, "
                              << viewer.visualMeshCount() << " visual meshes, "
                              << viewer.materialCount() << " materials, "
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

                    viewer.setTimeMs(5000);
                    timeline.setPixelsPerTick(1.0);
                    const QImage baseScaleImage = RenderTimeline(timeline);
                    const bool baseScaleReadable =
                            PixelIs(baseScaleImage,
                                    49,
                                    400,
                                    QColor(QStringLiteral("#9aa69e"))) &&
                            PixelIs(baseScaleImage,
                                    49,
                                    310,
                                    QColor(QStringLiteral("#59635d"))) &&
                            !PixelIsAnyRulerMark(baseScaleImage, 49, 301) &&
                            !PixelIs(baseScaleImage,
                                     60,
                                     310,
                                     QColor(QStringLiteral("#59635d")));

                    timeline.setPixelsPerTick(3.0);
                    const QImage mediumScaleImage = RenderTimeline(timeline);
                    const bool mediumScaleReadable =
                            PixelIs(mediumScaleImage,
                                    49,
                                    450,
                                    QColor(QStringLiteral("#9aa69e"))) &&
                            PixelIs(mediumScaleImage,
                                    49,
                                    330,
                                    QColor(QStringLiteral("#59635d"))) &&
                            !PixelIsAnyRulerMark(mediumScaleImage, 49, 303);

                    timeline.setPixelsPerTick(12.0);
                    const QImage fineScaleImage = RenderTimeline(timeline);
                    const bool fineScaleReadable =
                            PixelIs(fineScaleImage,
                                    49,
                                    312,
                                    QColor(QStringLiteral("#59635d"))) &&
                            PixelIs(fineScaleImage,
                                    49,
                                    420,
                                    QColor(QStringLiteral("#9aa69e"))) &&
                            !PixelIs(fineScaleImage,
                                     60,
                                     312,
                                     QColor(QStringLiteral("#59635d")));
                    const bool dynamicRulerScale = baseScaleReadable &&
                            mediumScaleReadable && fineScaleReadable;

                    timeline.setPixelsPerTick(5.0);
                    const int fineLengthAt5 = RulerMarkLengthNear(
                            RenderTimeline(timeline), 305);
                    timeline.setPixelsPerTick(6.0);
                    const int fineLengthAt6 = RulerMarkLengthNear(
                            RenderTimeline(timeline), 306);
                    timeline.setPixelsPerTick(7.0);
                    const int fineLengthAt7 = RulerMarkLengthNear(
                            RenderTimeline(timeline), 307);
                    timeline.setPixelsPerTick(9.5);
                    const int fineLengthAt95 = RulerMarkLengthNear(
                            RenderTimeline(timeline), 309);
                    const int fineLengthAt12 = RulerMarkLengthNear(
                            fineScaleImage, 312);
                    const bool fineMarksGrowSmoothly =
                            fineLengthAt5 == 0 &&
                            fineLengthAt6 > fineLengthAt5 &&
                            fineLengthAt7 > fineLengthAt6 &&
                            fineLengthAt95 > fineLengthAt7 &&
                            fineLengthAt12 > fineLengthAt95;

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
                                    QStringLiteral("00:00:01 / "));
                    QSet<QString> visibleMaterialClasses;
                    for (const QVariant &entry :
                         viewer.visualInstances()) {
                        const QVariantMap instance = entry.toMap();
                        if (instance
                                            .value(
                                                    QStringLiteral(
                                                            "sourceVisible"))
                                            .toBool() &&
                            instance
                                            .value(
                                                    QStringLiteral(
                                                            "lodLevel"))
                                            .toLongLong() == 0) {
                            const qint64 bindingIndex = instance
                                    .value(QStringLiteral(
                                            "materialBindingIndex"))
                                    .toLongLong();
                            if (bindingIndex >= 0 &&
                                bindingIndex <
                                        viewer.visualMaterials().size()) {
                                visibleMaterialClasses.insert(
                                        viewer.visualMaterials()
                                                .at(bindingIndex)
                                                .toMap()
                                                .value(QStringLiteral(
                                                        "materialClass"))
                                                .toString());
                            }
                        }
                    }
                    const bool sceneValid = viewer.triangleCount() > 0 &&
                            viewer.visualTriangleCount() > 0 &&
                            viewer.visualMeshCount() > 0 &&
                            viewer.materialCount() > 0 &&
                            !viewer.visualMaterials().isEmpty() &&
                            viewer.visualMaterials().size() <
                                    viewer.visualInstances().size() &&
                            !viewer.visualInstances().isEmpty() &&
                            viewer.visualInstances().size() >
                                    viewer.visualMeshCount() &&
                            viewer.diagnosticCount() > 0 &&
                            visibleMaterialClasses.size() >= 3 &&
                            viewer.ellipsoidCount() > 0 &&
                            viewer.durationMs() > 0 &&
                            viewer.tickCount() ==
                                    viewer.durationMs() /
                                                    viewer.tickDurationMs() +
                                            1 &&
                            timelineInputs && naturalScrubDirection &&
                            leftPressDoesNotSnap && dynamicRulerScale &&
                            fineMarksGrowSmoothly &&
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
                                << ", baseScaleReadable="
                                << baseScaleReadable
                                << ", mediumScaleReadable="
                                << mediumScaleReadable
                                << ", fineScaleReadable="
                                << fineScaleReadable
                                << ", fineLengthAt5="
                                << fineLengthAt5
                                << ", fineLengthAt6="
                                << fineLengthAt6
                                << ", fineLengthAt7="
                                << fineLengthAt7
                                << ", fineLengthAt95="
                                << fineLengthAt95
                                << ", fineLengthAt12="
                                << fineLengthAt12
                                << ", rightDragZoomsIn="
                                << rightDragZoomsIn
                                << ", timeLabelUnambiguous="
                                << timeLabelUnambiguous
                                << ", visualTriangles="
                                << viewer.visualTriangleCount()
                                << ", visualMeshes="
                                << viewer.visualMeshCount()
                                << ", materials="
                                << viewer.materialCount()
                                << ", visualInstances="
                                << viewer.visualInstances().size()
                                << ", diagnostics="
                                << viewer.diagnosticCount()
                                << ", visibleMaterialClasses="
                                << visibleMaterialClasses.size() << '\n';
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
