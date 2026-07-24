#include "viewer/race_viewer_controller.h"

#include <QGuiApplication>
#include <QTimer>

#include <iostream>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: forevertas-viewer-reload <Packs> <replay>\n";
        return 2;
    }

    QGuiApplication application(argc, argv);
    forevertas::viewer::RaceViewerController viewer;
    int completedLoads = 0;
    bool sawSecondLoadingState = false;
    bool finished = false;
    int exitCode = 1;

    QObject::connect(
            &viewer,
            &forevertas::viewer::RaceViewerController::stateChanged,
            &application,
            [&]() {
                if (finished) return;
                if (completedLoads == 1 && viewer.loading()) {
                    sawSecondLoadingState = true;
                }
                if (viewer.loading() || !viewer.loaded()) return;

                const bool sceneValid = viewer.ellipsoidCount() > 0 &&
                        viewer.runCount() == 1 && viewer.tickCount() > 0;
                if (!sceneValid) {
                    finished = true;
                    std::cerr << "reloaded viewer scene is incomplete\n";
                    application.quit();
                    return;
                }

                ++completedLoads;
                if (completedLoads == 1) {
                    viewer.loadReplay(QString::fromLocal8Bit(argv[1]),
                                      QString::fromLocal8Bit(argv[2]));
                    return;
                }
                if (completedLoads == 2) {
                    finished = true;
                    exitCode = sawSecondLoadingState ? 0 : 1;
                    if (exitCode != 0) {
                        std::cerr << "second replay load never entered loading state\n";
                    }
                    application.quit();
                }
            });

    QTimer::singleShot(60000, &application, [&]() {
        if (finished) return;
        finished = true;
        std::cerr << "consecutive replay load timed out\n";
        application.quit();
    });
    viewer.loadReplay(QString::fromLocal8Bit(argv[1]),
                      QString::fromLocal8Bit(argv[2]));
    application.exec();
    return exitCode;
}
