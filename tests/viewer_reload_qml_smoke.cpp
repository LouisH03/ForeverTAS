#include "app/search_controller.h"
#include "viewer/race_timeline_item.h"
#include "viewer/race_viewer_controller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <iostream>

namespace {

bool CarModelsAreReady(QObject *root,
                       const forevertas::viewer::RaceViewerController &viewer) {
    const QList<QObject *> models = root->findChildren<QObject *>(
            QStringLiteral("runCarFilledModel"));
    const int expected = static_cast<int>(
            viewer.ellipsoidCount() * viewer.runCount());
    if (expected <= 0 || models.size() != expected) return false;
    for (const QObject *model : models) {
        const QVariant geometry = model->property("geometry");
        if (!geometry.isValid() || geometry.isNull()) return false;
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: forevertas-viewer-reload-qml <Packs> <replay>\n";
        return 2;
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForeverTASTests"));
    QCoreApplication::setApplicationName(
            QStringLiteral("ViewerReloadQmlSmoke"));
    QStandardPaths::setTestModeEnabled(true);

    forevertas::app::SearchController controller;
    forevertas::viewer::RaceViewerController viewer;
    forevertas::viewer::RegisterRaceViewerQmlTypes();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({
            {QStringLiteral("controller"),
             QVariant::fromValue(static_cast<QObject *>(&controller))},
            {QStringLiteral("viewer"),
             QVariant::fromValue(static_cast<QObject *>(&viewer))}});
    const QUrl mainQml = QUrl::fromLocalFile(
            QStringLiteral(FOREVERTAS_SOURCE_DIR "/qml/Main.qml"));
    engine.load(mainQml);
    if (engine.rootObjects().isEmpty()) {
        std::cerr << "Main.qml did not load\n";
        return 1;
    }
    QObject *const root = engine.rootObjects().front();

    int completedLoads = 0;
    bool finished = false;
    int exitCode = 1;
    QObject::connect(
            &viewer,
            &forevertas::viewer::RaceViewerController::stateChanged,
            &application,
            [&]() {
                if (finished || viewer.loading() || !viewer.loaded()) return;
                ++completedLoads;
                const int currentLoad = completedLoads;
                QTimer::singleShot(200, &application, [&, currentLoad]() {
                    if (finished || currentLoad != completedLoads) return;
                    if (!CarModelsAreReady(root, viewer)) {
                        finished = true;
                        std::cerr << "car delegates were not recreated after load "
                                  << currentLoad << '\n';
                        application.quit();
                        return;
                    }
                    if (currentLoad == 1) {
                        viewer.loadReplay(QString::fromLocal8Bit(argv[1]),
                                          QString::fromLocal8Bit(argv[2]));
                        return;
                    }
                    finished = true;
                    exitCode = 0;
                    application.quit();
                });
            });

    QTimer::singleShot(60000, &application, [&]() {
        if (finished) return;
        finished = true;
        std::cerr << "QML consecutive replay load timed out\n";
        application.quit();
    });
    viewer.loadReplay(QString::fromLocal8Bit(argv[1]),
                      QString::fromLocal8Bit(argv[2]));
    application.exec();
    return exitCode;
}
