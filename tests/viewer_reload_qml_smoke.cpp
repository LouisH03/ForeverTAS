#include "app/search_controller.h"
#include "viewer/race_timeline_item.h"
#include "viewer/race_viewer_controller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <cmath>
#include <iostream>

namespace {

bool Finite(const QVector3D &value) {
    return std::isfinite(value.x()) && std::isfinite(value.y()) &&
            std::isfinite(value.z());
}

bool Close(const QVector3D &left,
           const QVector3D &right,
           float tolerance = 0.001f) {
    return (left - right).length() <= tolerance;
}

int CountOrangeCarPixels(const QImage &image, const QRect &area) {
    const QRect clipped = area.intersected(image.rect());
    int count = 0;
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.red() >= 170 && color.green() >= 35 &&
                color.green() <= 145 && color.blue() <= 100 &&
                color.red() > color.green() + 70) {
                ++count;
            }
        }
    }
    return count;
}

bool CanRenderQuick3D(const QQuickWindow &window) {
    const QSGRendererInterface *const renderer = window.rendererInterface();
    return renderer != nullptr && QSGRendererInterface::isApiRhiBased(
            renderer->graphicsApi());
}

bool CarModelsAreVisible(
        QObject *root,
        const forevertas::viewer::RaceViewerController &viewer,
        int loadNumber) {
    const QList<QObject *> roots = root->findChildren<QObject *>(
            QStringLiteral("runCarRoot"));
    const QList<QObject *> nodes = root->findChildren<QObject *>(
            QStringLiteral("runCarEllipsoidNode"));
    const QList<QObject *> models = root->findChildren<QObject *>(
            QStringLiteral("runCarFilledModel"));
    const QVariantList expectedEllipsoids = viewer.carEllipsoids();
    const int expected = static_cast<int>(
            viewer.ellipsoidCount() * viewer.runCount());
    int activeNodeCount = 0;
    int activeModelCount = 0;
    bool okay = expected > 0 && roots.size() == viewer.runCount();

    for (const QObject *carRoot : roots) {
        const QVector3D position =
                carRoot->property("position").value<QVector3D>();
        okay &= carRoot->property("visible").toBool() && Finite(position) &&
                Close(position, viewer.carPosition(), 0.01f);
    }
    for (const QObject *node : nodes) {
        if (!node->property("ellipsoidActive").toBool()) continue;
        ++activeNodeCount;
        const int index = node->property("ellipsoidIndex").toInt();
        const QVector3D position =
                node->property("position").value<QVector3D>();
        const QVector3D scale = node->property("scale").value<QVector3D>();
        okay &= index >= 0 && index < expectedEllipsoids.size() &&
                node->property("visible").toBool() && Finite(position) &&
                Finite(scale) && scale.x() > 0.0f && scale.y() > 0.0f &&
                scale.z() > 0.0f;
        if (index >= 0 && index < expectedEllipsoids.size()) {
            const QVariantMap expectedItem =
                    expectedEllipsoids[index].toMap();
            okay &= Close(position,
                          expectedItem.value(QStringLiteral("position"))
                                  .value<QVector3D>()) &&
                    Close(scale,
                          expectedItem.value(QStringLiteral("radii"))
                                  .value<QVector3D>());
        }
    }
    for (const QObject *model : models) {
        const QObject *const parent = model->parent();
        if (parent == nullptr ||
            !parent->property("ellipsoidActive").toBool()) {
            continue;
        }
        ++activeModelCount;
        const QVariant geometry = model->property("geometry");
        okay &= model->property("visible").toBool() &&
                model->property("opacity").toReal() > 0.0 &&
                geometry.isValid() && !geometry.isNull();
    }
    okay &= activeNodeCount == expected && activeModelCount == expected;

    auto *const window = qobject_cast<QQuickWindow *>(root);
    auto *const viewport = qobject_cast<QQuickItem *>(
            root->findChild<QObject *>(QStringLiteral("raceViewport")));
    int orangePixels = -1;
    if (window == nullptr || viewport == nullptr) {
        okay = false;
    } else if (CanRenderQuick3D(*window)) {
        const QImage frame = window->grabWindow();
        const QPointF center = viewport->mapToScene(
                QPointF(viewport->width() * 0.5, viewport->height() * 0.5));
        const QRect carArea(static_cast<int>(center.x()) - 120,
                            static_cast<int>(center.y()) - 120,
                            240,
                            240);
        orangePixels = CountOrangeCarPixels(frame, carArea);
        if (orangePixels <= 10) {
            frame.save(QStringLiteral("/tmp/forevertas-reload-failure-%1.png")
                               .arg(loadNumber));
            okay = false;
        }
    }

    if (!okay) {
        std::cerr << "car visibility failed after button load " << loadNumber
                  << ": expected=" << expected
                  << " roots=" << roots.size()
                  << " activeNodes=" << activeNodeCount
                  << "/" << nodes.size()
                  << " activeModels=" << activeModelCount
                  << "/" << models.size()
                  << " orangePixels=" << orangePixels
                  << " controllerPosition="
                  << viewer.carPosition().x() << ','
                  << viewer.carPosition().y() << ','
                  << viewer.carPosition().z() << '\n';
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
            QStringLiteral("loadRaceViewerButton"));

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
                ++completedLoads;
                const int currentLoad = completedLoads;
                QTimer::singleShot(500, &application, [&, currentLoad]() {
                    if (finished || currentLoad != completedLoads) return;
                    if (!CarModelsAreVisible(root, viewer, currentLoad)) {
                        finished = true;
                        application.quit();
                        return;
                    }
                    if (currentLoad < 3) {
                        controller.setReplayPath(replays[currentLoad]);
                        QCoreApplication::processEvents();
                        if (!ClickLoadButton(loadButton)) {
                            finished = true;
                            std::cerr << "Load Race Viewer button click "
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
        std::cerr << "first Load Race Viewer button click failed\n";
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
