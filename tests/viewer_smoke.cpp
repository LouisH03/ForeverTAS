#include "viewer/race_viewer_controller.h"

#include <QGuiApplication>
#include <QTimer>

#include <iostream>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: forevertas-viewer-smoke <Packs> <replay>\n";
        return 2;
    }

    QGuiApplication application(argc, argv);
    forevertas::viewer::RaceViewerController viewer;
    int exitCode = 1;
    bool completed = false;
    QObject::connect(
            &viewer,
            &forevertas::viewer::RaceViewerController::stateChanged,
            &application,
            [&]() {
                if (completed) {
                    return;
                }
                if (viewer.loading()) {
                    return;
                }
                if (viewer.loaded()) {
                    completed = true;
                    std::cout << viewer.triangleCount() << " triangles, "
                              << viewer.ellipsoidCount() << " ellipsoids, "
                              << viewer.durationMs() << " ms\n";
                    exitCode = viewer.triangleCount() > 0 &&
                                       viewer.ellipsoidCount() > 0 &&
                                       viewer.durationMs() > 0
                            ? 0
                            : 1;
                    application.quit();
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
