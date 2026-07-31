#include "app/search_controller.h"
#include "viewer/race_timeline_item.h"
#include "viewer/race_viewer_controller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <iostream>

namespace {

bool PreviewSceneIsVisible(
        QObject *root,
        const forevertas::viewer::RaceViewerController &viewer,
        int loadNumber) {
    const QList<QObject *> roots = root->findChildren<QObject *>(
            QStringLiteral("runCarRoot"));
    const QList<QObject *> nodes = root->findChildren<QObject *>(
            QStringLiteral("runCarEllipsoidNode"));
    const QList<QObject *> models = root->findChildren<QObject *>(
            QStringLiteral("runCarFilledModel"));
    const QList<QObject *> visualModels = root->findChildren<QObject *>(
            QStringLiteral("trackVisualModel"));
    const bool okay = viewer.loaded() && viewer.runCount() == 1 &&
            viewer.tickCount() > 1 && viewer.durationMs() > 0 &&
            viewer.selectedRunId() == QStringLiteral("preview") &&
            viewer.trajectoryCount() == 1 &&
            viewer.ellipsoidCount() > 0 &&
            viewer.visualBatchCount() > 0 &&
            roots.size() == 1 &&
            nodes.size() == viewer.ellipsoidCount() &&
            models.size() == viewer.ellipsoidCount() &&
            visualModels.size() == viewer.visualBatchCount();

    if (!okay) {
        std::cerr << "preview scene failed after button load " << loadNumber
                  << " roots=" << roots.size()
                  << " nodes=" << nodes.size()
                  << " models=" << models.size()
                  << " visualModels=" << visualModels.size()
                  << " visualBatches=" << viewer.visualBatchCount() << '\n';
    }
    return okay;
}

bool ClickLoadButton(QObject *button) {
    return button != nullptr && button->property("enabled").toBool() &&
            QMetaObject::invokeMethod(button, "clicked", Qt::DirectConnection);
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: forevertas-viewer-reload-qml "
                     "<Packs> <first> <second>\n";
        return 2;
    }
    if (QFileInfo(QString::fromLocal8Bit(argv[2])).canonicalFilePath() ==
        QFileInfo(QString::fromLocal8Bit(argv[3])).canonicalFilePath()) {
        std::cerr << "reload regression requires two distinct replay files\n";
        return 2;
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForeverTASTests"));
    QCoreApplication::setApplicationName(
            QStringLiteral("ViewerReloadQmlSmoke"));
    QStandardPaths::setTestModeEnabled(true);

    const QString packs = QString::fromLocal8Bit(argv[1]);
    const QString replays[]{
            QString::fromLocal8Bit(argv[2]),
            QString::fromLocal8Bit(argv[3]),
            QString::fromLocal8Bit(argv[2])};
    forevertas::app::SearchController controller;
    controller.setPacksDirectory(packs);
    controller.setReplayPath(replays[0]);
    const QString protectedDraft = QStringLiteral("0.00 press up");
    controller.setBaseInputScript(protectedDraft);
    forevertas::viewer::RaceViewerController viewer;
    forevertas::viewer::RegisterRaceViewerQmlTypes();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({
            {QStringLiteral("controller"),
             QVariant::fromValue(static_cast<QObject *>(&controller))},
            {QStringLiteral("viewer"),
             QVariant::fromValue(static_cast<QObject *>(&viewer))}});
    engine.load(QUrl::fromLocalFile(
            QStringLiteral(FOREVERTAS_SOURCE_DIR "/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        std::cerr << "Main.qml did not load\n";
        return 1;
    }
    QObject *const root = engine.rootObjects().front();
    QObject *const loadButton = root->findChild<QObject *>(
            QStringLiteral("loadMapButton"));
    QObject *const extractButton = root->findChild<QObject *>(
            QStringLiteral("extractReplayInputsButton"));
    QObject *const replaceDialog = root->findChild<QObject *>(
            QStringLiteral("replaceBaseInputScriptDialog"));
    if (!ClickLoadButton(extractButton)) {
        std::cerr << "Extract inputs button click failed\n";
        return 1;
    }
    QCoreApplication::processEvents();
    const bool confirmationProtectedDraft =
            replaceDialog != nullptr &&
            replaceDialog->property("visible").toBool() &&
            !controller.extractingReplayInputs() &&
            controller.baseInputScript() == protectedDraft &&
            QMetaObject::invokeMethod(
                    replaceDialog, "reject", Qt::DirectConnection);
    QCoreApplication::processEvents();
    if (!confirmationProtectedDraft ||
        controller.baseInputScript() != protectedDraft) {
        std::cerr << "non-empty input script was not protected by confirmation\n";
        return 1;
    }

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
                        const bool preserved = viewer.loaded() &&
                                viewer.ellipsoidCount() > 0 &&
                                viewer.visualBatchCount() > 0;
                        preservedSceneDuringReload &= preserved;
                        if (!preserved) {
                            std::cerr
                                    << "button reload changed the scene early: "
                                    << "loaded=" << viewer.loaded()
                                    << ", ellipsoids="
                                    << viewer.ellipsoidCount()
                                    << ", runs=" << viewer.runCount() << '\n';
                        }
                    }
                    return;
                }
                if (!loadInProgress || !viewer.loaded() ||
                    viewer.runCount() == 0) {
                    return;
                }
                loadInProgress = false;
                ++completedLoads;
                const int currentLoad = completedLoads;
                QTimer::singleShot(500, &application, [&, currentLoad]() {
                    if (finished || currentLoad != completedLoads) return;
                    if (!PreviewSceneIsVisible(root, viewer, currentLoad)) {
                        finished = true;
                        application.quit();
                        return;
                    }
                    if (currentLoad < 3) {
                        controller.setReplayPath(replays[currentLoad]);
                        QCoreApplication::processEvents();
                        if (!ClickLoadButton(loadButton)) {
                            finished = true;
                            std::cerr << "Load map button click "
                                      << currentLoad + 1 << " failed\n";
                            application.quit();
                        }
                        return;
                    }
                    finished = true;
                    exitCode = preservedSceneDuringReload ? 0 : 1;
                    if (exitCode != 0) {
                        std::cerr << "the visible scene was torn down during "
                                     "a button reload\n";
                    }
                    application.quit();
                });
            });
    if (!ClickLoadButton(loadButton)) {
        std::cerr << "first Load map button click failed\n";
        return 1;
    }

    QTimer::singleShot(180000, &application, [&]() {
        if (finished) return;
        finished = true;
        std::cerr << "QML distinct replay reload timed out\n";
        application.quit();
    });
    application.exec();
    return exitCode;
}
