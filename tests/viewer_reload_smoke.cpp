#include "viewer/race_viewer_controller.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>

#include <cmath>
#include <iostream>

namespace {

bool Finite(const QVector3D &value) {
    return std::isfinite(value.x()) && std::isfinite(value.y()) &&
            std::isfinite(value.z());
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: forevertas-viewer-reload <Packs> <first> <second>\n";
        return 2;
    }
    if (QFileInfo(QString::fromLocal8Bit(argv[2])).canonicalFilePath() ==
        QFileInfo(QString::fromLocal8Bit(argv[3])).canonicalFilePath()) {
        std::cerr << "reload regression requires two distinct replay files\n";
        return 2;
    }

    QGuiApplication application(argc, argv);
    forevertas::viewer::RaceViewerController viewer;
    const QString packs = QString::fromLocal8Bit(argv[1]);
    const QString replays[]{
            QString::fromLocal8Bit(argv[2]),
            QString::fromLocal8Bit(argv[3]),
            QString::fromLocal8Bit(argv[2])};
    int completedLoads = 0;
    bool loadInProgress = false;
    bool preservedSceneDuringReload = true;
    bool finished = false;
    int exitCode = 1;

    QObject::connect(
            &viewer,
            &forevertas::viewer::RaceViewerController::stateChanged,
            &application,
            [&]() {
                if (finished) return;
                if (viewer.loading()) {
                    loadInProgress = true;
                    if (completedLoads > 0) {
                        preservedSceneDuringReload &= viewer.loaded() &&
                                viewer.ellipsoidCount() > 0 &&
                                viewer.runCount() > 0;
                    }
                    return;
                }
                if (!loadInProgress || !viewer.loaded()) return;
                loadInProgress = false;

                const bool sceneValid = viewer.ellipsoidCount() > 0 &&
                        viewer.runCount() == 1 && viewer.tickCount() > 0 &&
                        Finite(viewer.carPosition()) &&
                        !viewer.carRotation().isNull();
                if (!sceneValid) {
                    finished = true;
                    std::cerr << "viewer scene is incomplete after load "
                              << completedLoads + 1 << '\n';
                    application.quit();
                    return;
                }

                ++completedLoads;
                if (completedLoads < 3) {
                    viewer.loadReplay(packs, replays[completedLoads]);
                    return;
                }
                finished = true;
                exitCode = preservedSceneDuringReload ? 0 : 1;
                if (exitCode != 0) {
                    std::cerr << "an existing scene was torn down during reload\n";
                }
                application.quit();
            });

    QTimer::singleShot(180000, &application, [&]() {
        if (finished) return;
        finished = true;
        std::cerr << "distinct replay reload timed out\n";
        application.quit();
    });
    viewer.loadReplay(packs, replays[0]);
    application.exec();
    return exitCode;
}
