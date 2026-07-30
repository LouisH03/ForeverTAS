#include "app/search_controller.h"
#include "viewer/race_timeline_item.h"
#include "viewer/race_viewer_controller.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDevice>
#include <QMouseEvent>
#include <QPalette>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QSettings>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVector3D>
#include <QWheelEvent>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#ifndef FOREVERTAS_SOURCE_DIR
#error "FOREVERTAS_SOURCE_DIR must be defined"
#endif

namespace {

forevertas::SandboxInputEvent SwitchInput(
        std::int32_t timeMs,
        forevertas::SandboxInputAction action,
        bool pressed) {
    using forevervalidator::experimental::PhysicsSandboxInputValueKind;
    using forevervalidator::experimental::PhysicsSandboxSwitchState;
    forevertas::SandboxInputEvent event;
    event.timeMs = timeMs;
    event.action = action;
    event.value.kind = PhysicsSandboxInputValueKind::Switch;
    event.value.switchState = pressed
            ? PhysicsSandboxSwitchState::Pressed
            : PhysicsSandboxSwitchState::Released;
    return event;
}

bool ModelsHaveState(const QList<QObject *> &models,
                     int expectedCount,
                     bool visible) {
    if (models.size() != expectedCount) {
        return false;
    }
    for (const QObject *model : models) {
        const QVariant geometry = model->property("geometry");
        if (model->property("visible").toBool() != visible ||
            !geometry.isValid() || geometry.isNull()) {
            return false;
        }
    }
    return true;
}

int VisibleModelCount(const QList<QObject *> &models) {
    return static_cast<int>(std::count_if(
            models.cbegin(),
            models.cend(),
            [](const QObject *model) {
                return model->property("visible").toBool();
            }));
}

bool ModelsHaveGeometry(const QList<QObject *> &models,
                        int expectedCount) {
    return models.size() == expectedCount &&
            std::all_of(
                    models.cbegin(),
                    models.cend(),
                    [](const QObject *model) {
                        const QVariant geometry =
                                model->property("geometry");
                        return geometry.isValid() && !geometry.isNull();
                    });
}

bool VisualMaterialsAreBoundAndShared(
        const QList<QObject *> &models,
        const QList<QObject *> &materials,
        const QList<QObject *> &baseTextures,
        const QList<QObject *> &normalTextures,
        const forevertas::viewer::RaceViewerController &viewer) {
    if (materials.size() != viewer.visualMaterials().size() ||
        baseTextures.size() != materials.size() ||
        normalTextures.size() != materials.size() ||
        materials.isEmpty() || materials.size() >= models.size()) {
        return false;
    }

    QSet<QObject *> baseTextureObjects(baseTextures.cbegin(),
                                       baseTextures.cend());
    QSet<QObject *> normalTextureObjects(normalTextures.cbegin(),
                                         normalTextures.cend());
    for (const QObject *texture : baseTextures) {
        const QUrl source = texture->property("source").toUrl();
        if (source.scheme() != QStringLiteral("qrc") ||
            !source.path().startsWith(QStringLiteral("/materials/"))) {
            return false;
        }
    }
    for (const QObject *texture : normalTextures) {
        const QUrl source = texture->property("source").toUrl();
        if (source.scheme() != QStringLiteral("qrc") ||
            !source.path().startsWith(QStringLiteral("/materials/"))) {
            return false;
        }
    }

    for (const QObject *material : materials) {
        QObject *const baseMap =
                material->property("baseColorMap").value<QObject *>();
        QObject *const normalMap =
                material->property("normalMap").value<QObject *>();
        if (!baseTextureObjects.contains(baseMap) ||
            !normalTextureObjects.contains(normalMap)) {
            return false;
        }
    }

    QSet<QObject *> usedMaterials;
    bool repeatedBinding = false;
    for (const QObject *model : models) {
        const int binding = model->property("materialBindingIndex").toInt();
        QObject *const material =
                model->property("sharedMaterial").value<QObject *>();
        if (binding < 0 || binding >= materials.size() ||
            material != materials.at(binding)) {
            return false;
        }
        repeatedBinding |= usedMaterials.contains(material);
        usedMaterials.insert(material);
    }
    return repeatedBinding && usedMaterials.size() < models.size();
}

bool FilledModelsHaveBakedRunPalettes(
        const QList<QObject *> &models,
        const QList<QObject *> &materials,
        int expectedCount) {
    if (models.size() != expectedCount ||
        materials.size() != expectedCount) {
        return false;
    }
    QSet<QObject *> geometries;
    for (const QObject *model : models) {
        const QVariant geometry = model->property("geometry");
        if (!geometry.canConvert<QObject *>()) return false;
        QObject *const object = geometry.value<QObject *>();
        if (object == nullptr) return false;
        geometries.insert(object);
    }
    for (const QObject *material : materials) {
        if (!material->property("vertexColorsEnabled").toBool() ||
            material->property("diffuseColor").value<QColor>() !=
                    QColor(Qt::white)) {
            return false;
        }
    }
    return !geometries.isEmpty();
}

bool ContainsStandardSlider(QObject *root) {
    const QList<QObject *> objects = root->findChildren<QObject *>();
    for (const QObject *object : objects) {
        if (QByteArray(object->metaObject()->className()).contains("Slider")) {
            return true;
        }
    }
    return false;
}

bool ContainsText(QObject *root, const QString &needle) {
    const QList<QObject *> objects = root->findChildren<QObject *>();
    for (const QObject *object : objects) {
        const QVariant text = object->property("text");
        if (text.isValid() && text.toString().contains(needle)) {
            return true;
        }
    }
    return false;
}

bool IsCenteredIcon(QQuickItem *item, qreal expectedSize) {
    if (item == nullptr || item->parentItem() == nullptr) {
        return false;
    }
    const QQuickItem *const parent = item->parentItem();
    constexpr qreal tolerance = 0.1;
    return std::abs(item->width() - expectedSize) < tolerance &&
            std::abs(item->height() - expectedSize) < tolerance &&
            std::abs(item->x() + item->width() * 0.5 -
                     parent->width() * 0.5) < tolerance &&
            std::abs(item->y() + item->height() * 0.5 -
                     parent->height() * 0.5) < tolerance;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: forevertas-viewer-qml-smoke <Packs> <replay>\n";
        return 2;
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForeverTASTests"));
    QCoreApplication::setApplicationName(
            QStringLiteral("ViewerQmlSmoke"));
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();

    forevertas::app::SearchController controller;
    forevertas::viewer::RaceViewerController viewer;
    forevertas::viewer::RegisterRaceViewerQmlTypes();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({
            {QStringLiteral("controller"),
             QVariant::fromValue(static_cast<QObject *>(&controller))},
            {QStringLiteral("viewer"),
             QVariant::fromValue(static_cast<QObject *>(&viewer))}});

    int exitCode = 1;
    bool completed = false;
    bool verificationScheduled = false;
    bool editorStructure = false;
    QObject::connect(
            &engine,
            &QQmlApplicationEngine::objectCreationFailed,
            &application,
            [&]() {
                completed = true;
                application.quit();
            },
            Qt::QueuedConnection);

    const QUrl mainQml = QUrl::fromLocalFile(
            QStringLiteral(FOREVERTAS_SOURCE_DIR "/qml/Main.qml"));
    engine.load(mainQml);
    if (engine.rootObjects().isEmpty()) {
        std::cerr << "failed to create Main.qml\n";
        return 1;
    }
    QObject *const root = engine.rootObjects().front();
    QObject *const settingsPanel =
            root->findChild<QObject *>(QStringLiteral("settingsPanel"));
    QObject *const darkModeToggle =
            root->findChild<QObject *>(QStringLiteral("darkModeToggle"));
    QObject *const whiteboardImportDialog =
            root->findChild<QObject *>(
                    QStringLiteral("whiteboardImportDialog"));
    QObject *const whiteboardExportDialog =
            root->findChild<QObject *>(
                    QStringLiteral("whiteboardExportDialog"));
    QObject *const whiteboardImageExportDialog =
            root->findChild<QObject *>(
                    QStringLiteral("whiteboardImageExportDialog"));
    const auto usesApplicationFileDialog =
            [](const QObject *dialog) {
                return dialog != nullptr &&
                       (dialog->property("options").toInt() &
                        static_cast<int>(
                                QFileDialog::DontUseNativeDialog)) != 0;
            };
    QObject *const initialMainMapLight =
            root->findChild<QObject *>(QStringLiteral("mainMapLight"));
    QObject *const initialFillMapLight =
            root->findChild<QObject *>(QStringLiteral("fillMapLight"));
    QObject *const initialMapEnvironment =
            root->findChild<QObject *>(QStringLiteral("mapEnvironment"));
    const QColor lightWindowColor = root->property("color").value<QColor>();
    const QColor lightPanelColor =
            settingsPanel != nullptr
            ? settingsPanel->property("color").value<QColor>()
            : QColor();
    const QColor mainLightColor =
            initialMainMapLight != nullptr
            ? initialMainMapLight->property("color").value<QColor>()
            : QColor();
    const QColor fillLightColor =
            initialFillMapLight != nullptr
            ? initialFillMapLight->property("color").value<QColor>()
            : QColor();
    const QColor environmentColor =
            initialMapEnvironment != nullptr
            ? initialMapEnvironment->property("clearColor").value<QColor>()
            : QColor();
    const QColor lightWidgetWindowColor =
            application.palette().color(QPalette::Window);
    controller.setDarkMode(true);
    QCoreApplication::processEvents();
    const bool darkThemeValid =
            controller.darkMode() && darkModeToggle != nullptr &&
            darkModeToggle->property("checked").toBool() &&
            usesApplicationFileDialog(whiteboardImportDialog) &&
            usesApplicationFileDialog(whiteboardExportDialog) &&
            usesApplicationFileDialog(whiteboardImageExportDialog) &&
            root->property("color").value<QColor>() != lightWindowColor &&
            settingsPanel != nullptr &&
            settingsPanel->property("color").value<QColor>() !=
                    lightPanelColor &&
            application.palette().color(QPalette::Window) !=
                    lightWidgetWindowColor &&
            application.palette().color(QPalette::Text) ==
                    QColor(QStringLiteral("#f0f3ef")) &&
            application.palette().color(
                    QPalette::Disabled, QPalette::Text) ==
                    QColor(QStringLiteral("#737b74")) &&
            initialMainMapLight != nullptr &&
            initialMainMapLight->property("color").value<QColor>() ==
                    mainLightColor &&
            initialFillMapLight != nullptr &&
            initialFillMapLight->property("color").value<QColor>() ==
                    fillLightColor &&
            initialMapEnvironment != nullptr &&
            initialMapEnvironment->property("clearColor").value<QColor>() ==
                    environmentColor;
    controller.setDarkMode(false);
    QCoreApplication::processEvents();
    const bool lightThemeRestored =
            darkModeToggle != nullptr &&
            !darkModeToggle->property("checked").toBool() &&
            settingsPanel != nullptr &&
            root->property("color").value<QColor>() == lightWindowColor &&
            settingsPanel->property("color").value<QColor>() ==
                    lightPanelColor &&
            application.palette().color(QPalette::Window) ==
                    lightWidgetWindowColor;
    if (!darkThemeValid || !lightThemeRestored) {
        std::cerr << "light/dark theme switching changed scene rendering or "
                     "failed to update the complete UI shell"
                  << " (toggle=" << (darkModeToggle != nullptr)
                  << ", settings=" << (settingsPanel != nullptr)
                  << ", light-window="
                  << lightWindowColor.name().toStdString()
                  << ", restored-window="
                  << root->property("color")
                             .value<QColor>()
                             .name()
                             .toStdString()
                  << ", light-panel="
                  << lightPanelColor.name().toStdString()
                  << ", restored-panel="
                  << (settingsPanel != nullptr
                              ? settingsPanel->property("color")
                                        .value<QColor>()
                                        .name()
                                        .toStdString()
                              : std::string("<missing>"))
                  << ", scene=" << (initialMainMapLight != nullptr)
                  << "/" << (initialFillMapLight != nullptr)
                  << "/" << (initialMapEnvironment != nullptr) << ")\n";
        return 1;
    }

    auto *const initialGlobalScript =
            qobject_cast<QQuickItem *>(
                    root->findChild<QObject *>(
                            QStringLiteral("baseInputScriptSection")));
    auto *const initialToolTabs =
            qobject_cast<QQuickItem *>(
                    root->findChild<QObject *>(
                            QStringLiteral("toolTabs")));
    auto *const initialBruteforceContent =
            qobject_cast<QQuickItem *>(
                    root->findChild<QObject *>(
                            QStringLiteral("bruteforceTabContent")));
    auto *const initialDebuggerContent =
            qobject_cast<QQuickItem *>(
                    root->findChild<QObject *>(
                            QStringLiteral("simulationDebuggerPanel")));
    bool globalScriptVisibleAcrossTabs =
            initialGlobalScript != nullptr &&
            initialToolTabs != nullptr &&
            initialBruteforceContent != nullptr &&
            initialDebuggerContent != nullptr;
    if (globalScriptVisibleAcrossTabs) {
        initialToolTabs->setProperty("currentIndex", 1);
        QCoreApplication::processEvents();
        globalScriptVisibleAcrossTabs &=
                initialGlobalScript->isVisible() &&
                !initialBruteforceContent->isVisible() &&
                initialDebuggerContent->isVisible();
        initialToolTabs->setProperty("currentIndex", 0);
        QCoreApplication::processEvents();
        globalScriptVisibleAcrossTabs &=
                initialGlobalScript->isVisible() &&
                initialBruteforceContent->isVisible() &&
                !initialDebuggerContent->isVisible();
    }

    QObject::connect(
            &viewer,
            &forevertas::viewer::RaceViewerController::stateChanged,
            &application,
            [&]() {
                if (completed || verificationScheduled || viewer.loading() ||
                    !viewer.loaded()) {
                    return;
                }
                verificationScheduled = true;
                QTimer::singleShot(500, &application, [&]() {
                    if (completed) {
                        return;
                    }
                    QObject *const filled = root->findChild<QObject *>(
                            QStringLiteral("trackFilledModel"));
                    QObject *const wire = root->findChild<QObject *>(
                            QStringLiteral("trackWireModel"));
                    auto *const renderModeSelector =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(
                                            QStringLiteral(
                                                    "renderModeSelector")));
                    QObject *const gpuRayTracingView =
                            root->findChild<QObject *>(
                                    QStringLiteral("gpuRayTracingView"));
                    QObject *const rasterMapView =
                            root->findChild<QObject *>(
                                    QStringLiteral("rasterMapView"));
                    QObject *const viewCamera = root->findChild<QObject *>(
                            QStringLiteral("viewCamera"));
                    QObject *const mapEnvironment =
                            root->findChild<QObject *>(
                                    QStringLiteral("mapEnvironment"));
                    QObject *const daySkyTexture =
                            root->findChild<QObject *>(
                                    QStringLiteral("daySkyTexture"));
                    QObject *const mainMapLight = root->findChild<QObject *>(
                            QStringLiteral("mainMapLight"));
                    QObject *const fillMapLight = root->findChild<QObject *>(
                            QStringLiteral("fillMapLight"));
                    auto *const timeline = root->findChild<
                            forevertas::viewer::RaceTimelineItem *>(
                            QStringLiteral("raceTimeline"));
                    auto *const timelinePanel = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("timelinePanel")));
                    auto *const viewport = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("raceViewport")));
                    auto *const raceViewerHeader = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("raceViewerHeader")));
                    auto *const headerControlsRow =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(
                                            QStringLiteral(
                                                    "headerControlsRow")));
                    auto *const raceViewerTitleBlock =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(
                                            QStringLiteral(
                                                    "raceViewerTitleBlock")));
                    auto *const runSelector = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("runSelector")));
                    auto *const resetViewButton =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(
                                            QStringLiteral(
                                                    "resetViewButton")));
                    auto *const playbackDock = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("playbackDock")));
                    QObject *const playPause = root->findChild<QObject *>(
                            QStringLiteral("playPauseButton"));
                    auto *const playIcon = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("playTransportIcon")));
                    auto *const pauseIcon = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("pauseTransportIcon")));
                    QObject *const jumpStart = root->findChild<QObject *>(
                            QStringLiteral("jumpStartButton"));
                    auto *const jumpStartIcon = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("jumpStartTransportIcon")));
                    QObject *const jumpEnd = root->findChild<QObject *>(
                            QStringLiteral("jumpEndButton"));
                    auto *const jumpEndIcon = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("jumpEndTransportIcon")));
                    QObject *const manualDriveButton =
                            root->findChild<QObject *>(
                                    QStringLiteral("manualDriveButton"));
                    auto *const takeOverOnInputCheckBox =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(
                                            QStringLiteral(
                                                    "takeOverOnInputCheckBox")));
                    auto *const manualDriveStatus =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(
                                            QStringLiteral(
                                                    "manualDriveStatus")));
                    auto *const manualInputFocus =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(
                                            QStringLiteral(
                                                    "manualInputFocus")));
                    QObject *const stepBackward = root->findChild<QObject *>(
                            QStringLiteral("stepBackwardShortcut"));
                    QObject *const stepForward = root->findChild<QObject *>(
                            QStringLiteral("stepForwardShortcut"));
                    QObject *const autoPacksSuggestion =
                            root->findChild<QObject *>(
                                    QStringLiteral("autoPacksSuggestion"));
                    QObject *const autoPacksSuggestionText =
                            root->findChild<QObject *>(QStringLiteral(
                                    "autoPacksSuggestionText"));
                    QObject *const applyAutoPacks = root->findChild<QObject *>(
                            QStringLiteral("applyAutoPacksButton"));
                    QObject *const searchAlgorithmCombo =
                            root->findChild<QObject *>(
                                    QStringLiteral("searchAlgorithmCombo"));
                    QObject *const simulationBackendCombo =
                            root->findChild<QObject *>(
                                    QStringLiteral("simulationBackendCombo"));
                    auto *const cpuWorkerSettings =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(QStringLiteral(
                                            "cpuWorkerSettings")));
                    QObject *const cpuWorkerCountField =
                            root->findChild<QObject *>(QStringLiteral(
                                    "cpuWorkerCountField"));
#if FOREVERVALIDATOR_HAS_CUDA
                    auto *const cudaParallelSampleSettings =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(QStringLiteral(
                                            "cudaParallelSampleSettings")));
                    QObject *const cudaParallelSampleCountField =
                            root->findChild<QObject *>(QStringLiteral(
                                    "cudaParallelSampleCountField"));
                    auto *const cudaCalibrationCheckBox =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(QStringLiteral(
                                            "cudaCalibrationCheckBox")));
#endif
                    QObject *const settingsScroll = root->findChild<QObject *>(
                            QStringLiteral("settingsScroll"));
                    QObject *const settingsWheelRedirector =
                            root->property("settingsWheelRedirectorObject")
                                    .value<QObject *>();
                    auto *const toolTabs = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("toolTabs")));
                    auto *const bruteforceTabContent =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(
                                            QStringLiteral(
                                                    "bruteforceTabContent")));
                    auto *const simulationDebuggerPanel =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(
                                            QStringLiteral(
                                                    "simulationDebuggerPanel")));
                    QObject *const simulationSourceTree =
                            root->findChild<QObject *>(
                                    QStringLiteral("simulationSourceTree"));
                    QObject *const simulationCodeViewer =
                            root->findChild<QObject *>(
                                    QStringLiteral("simulationCodeViewer"));
                    QObject *const simulationVariables =
                            root->findChild<QObject *>(
                                    QStringLiteral("simulationVariables"));
                    QObject *const restartLiveSimulationButton =
                            root->findChild<QObject *>(QStringLiteral(
                                    "restartLiveSimulationButton"));
                    QObject *const resetLiveEditsButton =
                            root->findChild<QObject *>(
                                    QStringLiteral("resetLiveEditsButton"));
                    auto *const evaluationSection = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("evaluationSection")));
                    auto *const modifierSection = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("modifierSection")));
                    auto *const searchSection = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("searchSection")));
                    QObject *const evaluationTargetSelector =
                            root->findChild<QObject *>(
                                    QStringLiteral("evaluationTargetSelector"));
                    QObject *const modifierComposition =
                            root->findChild<QObject *>(
                                    QStringLiteral("modifierComposition"));
                    QObject *const addModifierCombo =
                            root->findChild<QObject *>(
                                    QStringLiteral("addModifierCombo"));
                    QObject *const addModifierButton =
                            root->findChild<QObject *>(
                                    QStringLiteral("addModifierButton"));
                    QObject *const evaluationTargetCombo =
                            root->findChild<QObject *>(
                                    QStringLiteral("evaluationTargetCombo"));
                    QObject *const basicBruteForceSettings =
                            root->findChild<QObject *>(QStringLiteral(
                                    "basicBruteForceSearchSettings"));
                    QObject *const autoPromoteBestSwitch =
                            root->findChild<QObject *>(QStringLiteral(
                                    "autoPromoteBestSwitch"));
                    QObject *const velocitySettings =
                            root->findChild<QObject *>(QStringLiteral(
                                    "velocityEvaluationSettings"));
                    auto *const velocityModeCombo =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(QStringLiteral(
                                            "velocityModeCombo")));
                    QObject *const velocityModeComboContent =
                            root->findChild<QObject *>(QStringLiteral(
                                    "velocityModeComboContent"));
                    QObject *const bestInputsScrollView =
                            root->findChild<QObject *>(QStringLiteral(
                                    "bestInputsScrollView"));
                    QObject *const bestInputsTextArea =
                            root->findChild<QObject *>(QStringLiteral(
                                    "bestInputsTextArea"));
                    QObject *const copyBestInputsButton =
                            root->findChild<QObject *>(QStringLiteral(
                                    "copyBestInputsButton"));
                    auto *const baseInputScriptSection =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(QStringLiteral(
                                            "baseInputScriptSection")));
                    auto *const packsDirectorySection =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(QStringLiteral(
                                            "packsDirectorySection")));
                    QObject *const baseInputScriptScrollView =
                            root->findChild<QObject *>(QStringLiteral(
                                    "baseInputScriptScrollView"));
                    QObject *const baseInputScriptTextArea =
                            root->findChild<QObject *>(QStringLiteral(
                                    "baseInputScriptTextArea"));
                    QObject *const baseInputScriptErrorLabel =
                            root->findChild<QObject *>(QStringLiteral(
                                    "baseInputScriptErrorLabel"));
                    QObject *const copyCurrentRaceInputsButton =
                            root->findChild<QObject *>(QStringLiteral(
                                    "copyCurrentRaceInputsButton"));
                    QObject *const loadMapButton =
                            root->findChild<QObject *>(
                                    QStringLiteral("loadMapButton"));
                    QObject *const extractReplayInputsButton =
                            root->findChild<QObject *>(QStringLiteral(
                                    "extractReplayInputsButton"));
                    QObject *const replaceBaseInputScriptDialog =
                            root->findChild<QObject *>(QStringLiteral(
                                    "replaceBaseInputScriptDialog"));
                    QObject *const startSearchButton =
                            root->findChild<QObject *>(QStringLiteral(
                                    "startSearchButton"));
                    QObject *const stopSearchButton =
                            root->findChild<QObject *>(QStringLiteral(
                                    "stopSearchButton"));
                    auto *const searchMetricsRow = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(QStringLiteral(
                                    "searchMetricsRow")));
                    auto *const iterationsMetricCard =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(QStringLiteral(
                                            "iterationsMetricCard")));
                    QObject *const iterationsMetricValue =
                            root->findChild<QObject *>(QStringLiteral(
                                    "iterationsMetricValue"));
                    auto *const throughputMetricCard =
                            qobject_cast<QQuickItem *>(
                                    root->findChild<QObject *>(QStringLiteral(
                                            "throughputMetricCard")));
                    QObject *const throughputMetricValue =
                            root->findChild<QObject *>(QStringLiteral(
                                    "throughputMetricValue"));
                    auto *const elapsedMetricCard = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(QStringLiteral(
                                    "elapsedMetricCard")));
                    QObject *const elapsedMetricValue =
                            root->findChild<QObject *>(QStringLiteral(
                                    "elapsedMetricValue"));
                    const bool keyboardStepping =
                            stepBackward != nullptr &&
                            stepForward != nullptr &&
                            stepBackward->property("enabled").toBool() &&
                            stepForward->property("enabled").toBool() &&
                            stepBackward->property("sequence").toString() ==
                                    QStringLiteral("Left") &&
                            stepForward->property("sequence").toString() ==
                                    QStringLiteral("Right");
                    const auto manualMapping =
                            [root](Qt::Key key) {
                                QVariant result;
                                const bool invoked =
                                        QMetaObject::invokeMethod(
                                                root,
                                                "manualControlForKey",
                                                Q_RETURN_ARG(
                                                        QVariant, result),
                                                Q_ARG(
                                                        QVariant,
                                                        QVariant::fromValue(
                                                                static_cast<
                                                                        int>(
                                                                        key))));
                                return invoked
                                        ? result.toString()
                                        : QStringLiteral("<invoke failed>");
                            };
                    const auto sendMouseClick =
                            [root](QQuickItem *item) {
                                auto *const quickWindow =
                                        qobject_cast<QQuickWindow *>(root);
                                if (quickWindow == nullptr ||
                                    item == nullptr) {
                                    return false;
                                }
                                const QPointF position = item->mapToScene(
                                        QPointF(item->width() * 0.5,
                                                item->height() * 0.5));
                                const QPointF global =
                                        quickWindow->mapToGlobal(
                                                position.toPoint());
                                QMouseEvent press(
                                        QEvent::MouseButtonPress,
                                        position,
                                        position,
                                        global,
                                        Qt::LeftButton,
                                        Qt::LeftButton,
                                        Qt::NoModifier);
                                QCoreApplication::sendEvent(
                                        quickWindow, &press);
                                QMouseEvent release(
                                        QEvent::MouseButtonRelease,
                                        position,
                                        position,
                                        global,
                                        Qt::LeftButton,
                                        Qt::NoButton,
                                        Qt::NoModifier);
                                QCoreApplication::sendEvent(
                                        quickWindow, &release);
                                QCoreApplication::processEvents();
                                return press.isAccepted() &&
                                        release.isAccepted();
                            };
                    auto *const manualDriveItem =
                            qobject_cast<QQuickItem *>(manualDriveButton);
                    bool takeoverControlValid =
                            takeOverOnInputCheckBox != nullptr &&
                            manualDriveItem != nullptr &&
                            playbackDock != nullptr &&
                            playbackDock->width() >= 429.0 &&
                            takeOverOnInputCheckBox->parentItem() ==
                                    manualDriveItem->parentItem() &&
                            takeOverOnInputCheckBox->x() >=
                                    manualDriveItem->x() +
                                            manualDriveItem->width() &&
                            takeOverOnInputCheckBox
                                            ->property("text")
                                            .toString() ==
                                    QStringLiteral("Take Over on Input") &&
                            takeOverOnInputCheckBox
                                    ->property("enabled")
                                    .toBool() &&
                            !takeOverOnInputCheckBox
                                     ->property("checked")
                                     .toBool() &&
                            !viewer.takeOverOnInput();
                    if (takeoverControlValid) {
                        takeoverControlValid &=
                                sendMouseClick(
                                        takeOverOnInputCheckBox);
                        takeoverControlValid &=
                                viewer.takeOverOnInput() &&
                                takeOverOnInputCheckBox
                                        ->property("checked")
                                        .toBool();
                        viewer.play();
                        QCoreApplication::processEvents();
                        takeoverControlValid &=
                                viewer.playing() &&
                                !stepBackward
                                         ->property("enabled")
                                         .toBool() &&
                                !stepForward
                                         ->property("enabled")
                                         .toBool();
                        takeoverControlValid &=
                                sendMouseClick(manualInputFocus) &&
                                manualInputFocus
                                        ->property("activeFocus")
                                        .toBool();
                        viewer.pause();
                        viewer.setTakeOverOnInput(false);
                        QCoreApplication::processEvents();
                        takeoverControlValid &=
                                !viewer.takeOverOnInput() &&
                                !takeOverOnInputCheckBox
                                         ->property("checked")
                                         .toBool() &&
                                stepBackward
                                        ->property("enabled")
                                        .toBool() &&
                                stepForward
                                        ->property("enabled")
                                        .toBool();
                    }
                    const bool manualDrivingUi =
                            playbackDock != nullptr &&
                            playbackDock->width() >= 285.0 &&
                            takeoverControlValid &&
                            manualDriveButton != nullptr &&
                            manualDriveButton->property("text").toString() ==
                                    QStringLiteral("Drive") &&
                            manualDriveButton
                                    ->property("enabled")
                                    .toBool() ==
                                    (viewer.loaded() &&
                                     !viewer.loading()) &&
                            manualDriveStatus != nullptr &&
                            !manualDriveStatus->isVisible() &&
                            manualInputFocus != nullptr &&
                            manualMapping(Qt::Key_Left) ==
                                    QStringLiteral("left") &&
                            manualMapping(Qt::Key_A) ==
                                    QStringLiteral("left") &&
                            manualMapping(Qt::Key_Q) ==
                                    QStringLiteral("left") &&
                            manualMapping(Qt::Key_Right) ==
                                    QStringLiteral("right") &&
                            manualMapping(Qt::Key_D) ==
                                    QStringLiteral("right") &&
                            manualMapping(Qt::Key_Up) ==
                                    QStringLiteral("accelerate") &&
                            manualMapping(Qt::Key_W) ==
                                    QStringLiteral("accelerate") &&
                            manualMapping(Qt::Key_Z) ==
                                    QStringLiteral("accelerate") &&
                            manualMapping(Qt::Key_Down) ==
                                    QStringLiteral("brake") &&
                            manualMapping(Qt::Key_S) ==
                                    QStringLiteral("brake") &&
                            manualMapping(Qt::Key_Escape).isEmpty();
                    const qreal originalWindowWidth =
                            root->property("width").toReal();
                    root->setProperty("width", 1240);
                    QCoreApplication::processEvents();
                    const bool compactViewerHeader =
                            raceViewerHeader != nullptr &&
                            headerControlsRow != nullptr &&
                            runSelector != nullptr &&
                            renderModeSelector != nullptr &&
                            resetViewButton != nullptr &&
                            raceViewerTitleBlock != nullptr &&
                            raceViewerTitleBlock->x() >= -0.1 &&
                            runSelector->x() >=
                                    raceViewerTitleBlock->x() +
                                            raceViewerTitleBlock->width() &&
                            renderModeSelector->x() >=
                                    runSelector->x() + runSelector->width() &&
                            resetViewButton->x() >=
                                    renderModeSelector->x() +
                                            renderModeSelector->width() &&
                            resetViewButton->x() +
                                            resetViewButton->width() <=
                                    headerControlsRow->width() + 0.1;
                    root->setProperty("width", originalWindowWidth);
                    QCoreApplication::processEvents();
                    const auto rowCenter =
                            [](const QQuickItem *item) {
                                return item != nullptr
                                        ? item->y() + item->height() * 0.5
                                        : -1.0;
                            };
                    const bool runSelectorValid =
                            raceViewerHeader != nullptr &&
                            headerControlsRow != nullptr &&
                            raceViewerTitleBlock != nullptr &&
                            runSelector != nullptr &&
                            renderModeSelector != nullptr &&
                            resetViewButton != nullptr &&
                            headerControlsRow->parentItem() ==
                                    raceViewerHeader &&
                            raceViewerTitleBlock->parentItem() ==
                                    headerControlsRow &&
                            runSelector->parentItem() ==
                                    headerControlsRow &&
                            renderModeSelector->parentItem() ==
                                    headerControlsRow &&
                            resetViewButton->parentItem() ==
                                    headerControlsRow &&
                            std::abs(rowCenter(raceViewerTitleBlock) -
                                     rowCenter(runSelector)) < 0.6 &&
                            std::abs(rowCenter(runSelector) -
                                     rowCenter(renderModeSelector)) < 0.6 &&
                            std::abs(rowCenter(renderModeSelector) -
                                     rowCenter(resetViewButton)) < 0.6 &&
                            renderModeSelector->width() >= 179.0 &&
                            runSelector->property("count").toInt() == 1 &&
                            runSelector->property("enabled").toBool();
                    bool globalBaseInputScriptPlacement =
                            globalScriptVisibleAcrossTabs &&
                            packsDirectorySection != nullptr &&
                            baseInputScriptSection != nullptr &&
                            toolTabs != nullptr &&
                            bruteforceTabContent != nullptr &&
                            simulationDebuggerPanel != nullptr &&
                            root->findChildren<QObject *>(
                                        QStringLiteral(
                                                "baseInputScriptSection"))
                                            .size() == 1 &&
                            packsDirectorySection->parentItem() ==
                                    baseInputScriptSection->parentItem() &&
                            baseInputScriptSection->parentItem() ==
                                    toolTabs->parentItem() &&
                            packsDirectorySection->y() +
                                            packsDirectorySection->height() <=
                                    baseInputScriptSection->y() &&
                            baseInputScriptSection->y() +
                                            baseInputScriptSection->height() <=
                                    toolTabs->y();
                    const bool baseInputScriptUiValid =
                            baseInputScriptSection != nullptr &&
                            baseInputScriptScrollView != nullptr &&
                            baseInputScriptTextArea != nullptr &&
                            baseInputScriptErrorLabel != nullptr &&
                            copyCurrentRaceInputsButton != nullptr &&
                            copyCurrentRaceInputsButton
                                     ->property("enabled").toBool() &&
                            root->findChild<QObject *>(
                                    QStringLiteral(
                                            "saveInputTrajectoryButton")) ==
                                    nullptr &&
                            root->findChild<QObject *>(
                                    QStringLiteral(
                                            "saveInputTrajectoryShortcut")) ==
                                    nullptr &&
                            !ContainsText(
                                    root,
                                    QStringLiteral("Save trajectory")) &&
                            loadMapButton != nullptr &&
                            extractReplayInputsButton != nullptr &&
                            replaceBaseInputScriptDialog != nullptr &&
                            !baseInputScriptTextArea
                                     ->property("readOnly")
                                     .toBool() &&
                            baseInputScriptTextArea
                                    ->property("enabled")
                                    .toBool() &&
                            baseInputScriptErrorLabel
                                    ->property("text")
                                    .toString()
                                    .isEmpty() &&
                            loadMapButton->property("text").toString() ==
                                    QStringLiteral("Load map") &&
                            extractReplayInputsButton
                                            ->property("text")
                                            .toString() ==
                                    QStringLiteral("Extract inputs to script");
                    const bool bestInputsUiValid =
                            bestInputsScrollView != nullptr &&
                            bestInputsTextArea != nullptr &&
                            copyBestInputsButton != nullptr &&
                            bestInputsTextArea->property("readOnly").toBool() &&
                            copyBestInputsButton->property("text").toString() ==
                                    QStringLiteral("Copy all");
                    const bool searchControlsValid =
                            startSearchButton != nullptr &&
                            stopSearchButton != nullptr &&
                            startSearchButton->property("text").toString() ==
                                    QStringLiteral("Start") &&
                            stopSearchButton->property("text").toString() ==
                                    QStringLiteral("Stop") &&
                            !stopSearchButton->property("enabled").toBool();
                    const bool searchMetricsUiValid =
                            searchMetricsRow != nullptr &&
                            iterationsMetricCard != nullptr &&
                            iterationsMetricValue != nullptr &&
                            throughputMetricCard != nullptr &&
                            throughputMetricValue != nullptr &&
                            elapsedMetricCard != nullptr &&
                            elapsedMetricValue != nullptr &&
                            !searchMetricsRow->isVisible() &&
                            std::abs(iterationsMetricCard->height() -
                                     throughputMetricCard->height()) < 0.1 &&
                            std::abs(throughputMetricCard->height() -
                                     elapsedMetricCard->height()) < 0.1 &&
                            iterationsMetricValue->property("text")
                                    .toString().isEmpty() &&
                            throughputMetricValue->property("text")
                                    .toString().isEmpty() &&
                            elapsedMetricValue->property("text")
                                    .toString().isEmpty();
                    const bool removedSectionDescriptions =
                            !ContainsText(
                                    root,
                                    QStringLiteral("Build an ordered pipeline")) &&
                            !ContainsText(
                                    root,
                                    QStringLiteral("Choose what makes one")) &&
                            !ContainsText(
                                    root,
                                    QStringLiteral("Choose how iterations")) &&
                            !ContainsText(
                                    root,
                                    QStringLiteral("Runs continuously until"));
                    const bool automaticPacksUi =
                            autoPacksSuggestion != nullptr &&
                            autoPacksSuggestionText != nullptr &&
                            autoPacksSuggestionText->property("text")
                                    .toString() ==
                                    QStringLiteral(
                                            "This location was found "
                                            "automatically and should work. "
                                            "Apply?") &&
                            applyAutoPacks != nullptr &&
                            applyAutoPacks->property("text").toString() ==
                                    QStringLiteral("Apply");
                    bool backendSelectorValid =
                            simulationBackendCombo != nullptr &&
                            simulationBackendCombo->property("count").toInt() ==
#if FOREVERVALIDATOR_HAS_CUDA
                                    4 &&
#else
                                    3 &&
#endif
                            simulationBackendCombo->property("currentValue")
                                            .toString() ==
                                    QStringLiteral("reference") &&
                            simulationBackendCombo->property("displayText")
                                            .toString() ==
                                    QStringLiteral("Reference");
                    if (backendSelectorValid) {
                        controller.setSimulationBackendId(
                                QStringLiteral("optimized-cpu"));
                        QCoreApplication::processEvents();
                        backendSelectorValid =
                                simulationBackendCombo
                                                ->property("currentValue")
                                                .toString() ==
                                        QStringLiteral("optimized-cpu") &&
                                simulationBackendCombo
                                                ->property("displayText")
                                                .toString() ==
                                        QStringLiteral("CPU Optimized") &&
                                ContainsText(
                                        root,
                                        QStringLiteral(
                                                "Faster runtime optimized for "
                                                "Stadium, may break "
                                                "compatibility in other "
                                                "environments"));
                        if (backendSelectorValid) {
                            controller.setSimulationBackendId(
                                    QStringLiteral(
                                            "multi-threaded-cpu"));
                            controller.setCpuWorkerCount(
                                    QStringLiteral("2"));
                            QCoreApplication::processEvents();
                            backendSelectorValid =
                                    simulationBackendCombo
                                                    ->property("currentValue")
                                                    .toString() ==
                                            QStringLiteral(
                                                    "multi-threaded-cpu") &&
                                    simulationBackendCombo
                                                    ->property("displayText")
                                                    .toString() ==
                                            QStringLiteral(
                                                    "CPU Multi-threaded") &&
                                    cpuWorkerSettings != nullptr &&
                                    cpuWorkerSettings->isVisible() &&
                                    cpuWorkerCountField != nullptr &&
                                    cpuWorkerCountField
                                                    ->property("text")
                                                    .toString() ==
                                            QStringLiteral("2") &&
                                    ContainsText(
                                            root,
                                            QStringLiteral(
                                                    "Runs independent "
                                                    "optimized CPU "
                                                    "simulations across "
                                                    "multiple worker "
                                                    "threads"));
                        }
#if FOREVERVALIDATOR_HAS_CUDA
                        if (backendSelectorValid) {
                            controller.setSimulationBackendId(
                                    QStringLiteral("cuda"));
                            QCoreApplication::processEvents();
                            backendSelectorValid =
                                    simulationBackendCombo
                                                    ->property("currentValue")
                                                    .toString() ==
                                            QStringLiteral("cuda") &&
                                    simulationBackendCombo
                                                    ->property("displayText")
                                                    .toString() ==
                                            QStringLiteral("CUDA") &&
                                    cudaParallelSampleSettings != nullptr &&
                                    cudaParallelSampleSettings->isVisible() &&
                                    cudaParallelSampleCountField != nullptr &&
                                    cudaParallelSampleCountField
                                                    ->property("text")
                                                    .toString() ==
                                            QStringLiteral("256") &&
                                    cudaCalibrationCheckBox != nullptr &&
                                    cudaCalibrationCheckBox->isVisible() &&
                                    cudaCalibrationCheckBox->y() >=
                                            cudaParallelSampleSettings->y() +
                                                    cudaParallelSampleSettings
                                                            ->height() &&
                                    !cudaCalibrationCheckBox
                                             ->property("checked")
                                             .toBool() &&
                                    ContainsText(
                                            root,
                                            QStringLiteral(
                                                    "Fastest runtime optimized "
                                                    "for Stadium, needs a "
                                                    "modern NVIDIA GPU and may "
                                                    "break compatibility in "
                                                    "other environments"));
                            controller.setCudaCalibrationEnabled(true);
                            controller.setCudaParallelSampleCount(
                                    QStringLiteral("512"));
                            QCoreApplication::processEvents();
                            backendSelectorValid &=
                                    cudaCalibrationCheckBox
                                            ->property("checked")
                                            .toBool() &&
                                    cudaParallelSampleCountField
                                                    ->property("text")
                                                    .toString() ==
                                            QStringLiteral("512");
                        }
#endif
                        controller.setSimulationBackendId(
                                QStringLiteral("reference"));
                        QCoreApplication::processEvents();
                    }
                    const bool algorithmSelectorsValid =
                            searchAlgorithmCombo != nullptr &&
                            modifierComposition != nullptr &&
                            addModifierCombo != nullptr &&
                            addModifierButton != nullptr &&
                            evaluationTargetCombo != nullptr &&
                            searchAlgorithmCombo->property("count").toInt() ==
                                    1 &&
                            modifierComposition
                                            ->property("firstPassOptionCount")
                                            .toInt() == 5 &&
                            addModifierCombo->property("count").toInt() == 5 &&
                            evaluationTargetCombo->property("count").toInt() ==
                                    7 &&
                            searchAlgorithmCombo->property("currentValue")
                                            .toString() ==
                                    QStringLiteral("basic-brute-force") &&
                            searchAlgorithmCombo->property("displayText")
                                            .toString() ==
                                    QStringLiteral("Basic bruteforce") &&
                            modifierComposition
                                            ->property("firstPassSelectedId")
                                            .toString() ==
                                    QStringLiteral("random-steering") &&
                            evaluationTargetCombo->property("currentValue")
                                            .toString() ==
                                    QStringLiteral("velocity") &&
                            basicBruteForceSettings != nullptr &&
                            autoPromoteBestSwitch != nullptr &&
                            !autoPromoteBestSwitch
                                     ->property("checked")
                                     .toBool() &&
                            modifierComposition
                                    ->property("firstPassSettingsLoaded")
                                    .toBool() &&
                            velocitySettings != nullptr;
                    controller.setSearchAlgorithmSetting(
                            QStringLiteral("autoPromoteBest"),
                            QStringLiteral("true"));
                    QCoreApplication::processEvents();
                    const bool autoPromoteBestValid =
                            autoPromoteBestSwitch != nullptr &&
                            autoPromoteBestSwitch
                                    ->property("checked")
                                    .toBool();
                    controller.setSearchAlgorithmSetting(
                            QStringLiteral("autoPromoteBest"),
                            QStringLiteral("false"));
                    QCoreApplication::processEvents();
                    const bool settingComboTextValid =
                            velocityModeCombo != nullptr &&
                            velocityModeComboContent != nullptr &&
                            velocityModeCombo->property("displayText")
                                            .toString() ==
                                    QStringLiteral("Total speed") &&
                            velocityModeComboContent->property("text")
                                            .toString() ==
                                    QStringLiteral("Total speed") &&
                            !velocityModeComboContent->property("truncated")
                                     .toBool() &&
                            velocityModeCombo->width() >= 160.0;
                    const bool configurationSectionsValid =
                            evaluationSection != nullptr &&
                            modifierSection != nullptr &&
                            searchSection != nullptr &&
                            evaluationSection->parentItem() ==
                                    modifierSection->parentItem() &&
                            modifierSection->parentItem() ==
                                    searchSection->parentItem() &&
                            evaluationSection->y() < modifierSection->y() &&
                            modifierSection->y() < searchSection->y() &&
                            evaluationSection->property("radius").toReal() >
                                    0.0 &&
                            modifierSection->property("radius").toReal() >
                                    0.0 &&
                            searchSection->property("radius").toReal() > 0.0;
                    const bool comboSlotsStyled =
                            simulationBackendCombo != nullptr &&
                            searchAlgorithmCombo != nullptr &&
                            evaluationTargetCombo != nullptr &&
                            addModifierCombo != nullptr &&
                            simulationBackendCombo->property("slotStyled")
                                    .toBool() &&
                            searchAlgorithmCombo->property("slotStyled")
                                    .toBool() &&
                            evaluationTargetCombo->property("slotStyled")
                                    .toBool() &&
                            addModifierCombo->property("slotStyled").toBool() &&
                            modifierComposition
                                    ->property("firstPassSlotStyled").toBool();
                    const bool modifierPassLayoutValid =
                            modifierComposition != nullptr &&
                            modifierComposition
                                    ->property("firstPassHeaderLayoutValid")
                                    .toBool();
                    const bool debuggerUiValid =
                            toolTabs != nullptr &&
                            toolTabs->property("count").toInt() == 2 &&
                            toolTabs->property("currentIndex").toInt() == 0 &&
                            bruteforceTabContent != nullptr &&
                            bruteforceTabContent->isVisible() &&
                            simulationDebuggerPanel != nullptr &&
                            !simulationDebuggerPanel->isVisible() &&
                            simulationDebuggerPanel->height() >= 650.0 &&
                            simulationSourceTree != nullptr &&
                            simulationCodeViewer != nullptr &&
                            simulationVariables != nullptr &&
                            restartLiveSimulationButton != nullptr &&
                            resetLiveEditsButton != nullptr;

                    bool wheelScrollingValid =
                            settingsScroll != nullptr &&
                            settingsWheelRedirector != nullptr &&
                            settingsWheelRedirector->property("blocking")
                                    .toBool();
                    if (wheelScrollingValid) {
                        QObject *const flickable =
                                settingsScroll->property("contentItem")
                                        .value<QObject *>();
                        QObject *const nestedFlickable =
                                bestInputsScrollView == nullptr
                                ? nullptr
                                : bestInputsScrollView
                                          ->property("contentItem")
                                          .value<QObject *>();
                        auto *const scrollItem =
                                qobject_cast<QQuickItem *>(settingsScroll);
                        auto *const redirectorItem =
                                qobject_cast<QQuickItem *>(
                                        settingsWheelRedirector);
                        auto *const comboItem =
                                qobject_cast<QQuickItem *>(
                                        evaluationTargetCombo);
                        auto *const scriptItem =
                                qobject_cast<QQuickItem *>(
                                        bestInputsScrollView);
                        auto *const quickWindow =
                                qobject_cast<QQuickWindow *>(root);
                        wheelScrollingValid &= flickable != nullptr &&
                                nestedFlickable != nullptr &&
                                scrollItem != nullptr &&
                                redirectorItem != nullptr &&
                                comboItem != nullptr &&
                                scriptItem != nullptr &&
                                quickWindow != nullptr;
                        const auto sendWheel = [quickWindow](
                                                       QQuickItem *item,
                                                       const QPointF &local,
                                                       int delta) {
                            const QPointF position = item->mapToScene(local);
                            const QPoint global = quickWindow->mapToGlobal(
                                    position.toPoint());
                            QWheelEvent event(position,
                                              QPointF(global),
                                              {},
                                              QPoint(0, delta),
                                              Qt::NoButton,
                                              Qt::NoModifier,
                                              Qt::ScrollUpdate,
                                              false);
                            QCoreApplication::sendEvent(quickWindow, &event);
                            QCoreApplication::processEvents();
                            return event.isAccepted();
                        };
                        if (wheelScrollingValid) {
                            const double comboContentHeight =
                                    flickable->property("contentHeight")
                                            .toDouble();
                            const double comboMaximum = std::max(
                                    0.0,
                                    comboContentHeight -
                                            scrollItem->height());
                            const QPointF panelTopLeft =
                                    redirectorItem->mapToScene(QPointF());
                            const QPointF comboBefore =
                                    comboItem->mapToScene(
                                            QPointF(
                                                    comboItem->width() * 0.5,
                                                    comboItem->height() * 0.5));
                            flickable->setProperty(
                                    "contentY",
                                    std::clamp(
                                            comboBefore.y() -
                                                    panelTopLeft.y() -
                                                    redirectorItem->height() *
                                                            0.5,
                                            0.0,
                                            comboMaximum));
                            QCoreApplication::processEvents();
                            const double beforeCombo =
                                    flickable->property("contentY").toDouble();
                            const bool comboAccepted = sendWheel(
                                    comboItem,
                                    QPointF(comboItem->width() * 0.5,
                                            comboItem->height() * 0.5),
                                    -120);
                            const double afterCombo =
                                    flickable->property("contentY").toDouble();
                            wheelScrollingValid &= comboAccepted &&
                                    afterCombo > beforeCombo;

                            controller.setModifierPassId(
                                    0,
                                    QStringLiteral(
                                            "existing-event-perturbation"));
                            QCoreApplication::processEvents();
                            QCoreApplication::processEvents();
                            QObject *const perturbationSettings =
                                    modifierComposition
                                            ->property("firstPassSettingsItem")
                                            .value<QObject *>();
                            auto *const absoluteMinimumSlider =
                                    perturbationSettings == nullptr
                                    ? nullptr
                                    : qobject_cast<QQuickItem *>(
                                              perturbationSettings
                                                      ->findChild<QObject *>(
                                                              QStringLiteral(
                                                                      "perturbationAbsoluteMinimumSlider")));
                            wheelScrollingValid &=
                                    absoluteMinimumSlider != nullptr;
                            if (wheelScrollingValid) {
                                const double sliderContentHeight =
                                        flickable->property("contentHeight")
                                                .toDouble();
                                const double sliderMaximum = std::max(
                                        0.0,
                                        sliderContentHeight -
                                                scrollItem->height());
                                const QPointF panelTopLeft =
                                        redirectorItem->mapToScene(QPointF());
                                const QPointF sliderBefore =
                                        absoluteMinimumSlider->mapToScene(
                                                QPointF(
                                                        absoluteMinimumSlider
                                                                        ->width() *
                                                                0.5,
                                                        absoluteMinimumSlider
                                                                        ->height() *
                                                                0.5));
                                const double desiredSceneY =
                                        panelTopLeft.y() +
                                        redirectorItem->height() * 0.5;
                                const double currentY =
                                        flickable->property("contentY")
                                                .toDouble();
                                flickable->setProperty(
                                        "contentY",
                                        std::clamp(
                                                currentY + sliderBefore.y() -
                                                        desiredSceneY,
                                                0.0,
                                                sliderMaximum));
                                QCoreApplication::processEvents();
                                const double beforeSlider =
                                        flickable->property("contentY")
                                                .toDouble();
                                const bool sliderAccepted = sendWheel(
                                        absoluteMinimumSlider,
                                        QPointF(
                                                absoluteMinimumSlider->width() *
                                                        0.5,
                                                absoluteMinimumSlider->height() *
                                                        0.5),
                                        -120);
                                const double afterSlider =
                                        flickable->property("contentY")
                                                .toDouble();
                                wheelScrollingValid &= sliderAccepted &&
                                        afterSlider > beforeSlider;
                            }
                            controller.setModifierPassId(
                                    0, QStringLiteral("random-steering"));
                            QCoreApplication::processEvents();
                            QCoreApplication::processEvents();

                            const double contentHeight =
                                    flickable->property("contentHeight")
                                            .toDouble();
                            const double maximum = std::max(
                                    0.0,
                                    contentHeight - scrollItem->height());
                            flickable->setProperty("contentY", maximum);
                            nestedFlickable->setProperty("contentY", 0.0);
                            QCoreApplication::processEvents();
                            const QPointF scriptLocal(
                                    scriptItem->width() * 0.5,
                                    std::min(10.0,
                                             scriptItem->height() * 0.5));
                            const QPointF scriptScene =
                                    scriptItem->mapToScene(scriptLocal);
                            const bool scriptInsidePanel =
                                    redirectorItem->contains(
                                            redirectorItem->mapFromScene(
                                                    scriptScene));
                            const double beforeOuter =
                                    flickable->property("contentY").toDouble();
                            const double beforeNested = nestedFlickable
                                    ->property("contentY").toDouble();
                            const bool scriptAccepted =
                                    sendWheel(scriptItem, scriptLocal, 120);
                            const double afterOuter =
                                    flickable->property("contentY").toDouble();
                            const double afterNested = nestedFlickable
                                    ->property("contentY").toDouble();
                            wheelScrollingValid &= scriptInsidePanel &&
                                    scriptAccepted &&
                                    afterOuter < beforeOuter &&
                                    afterNested == beforeNested;
                        }
                    }
                    bool everyOwnedPanelLoaded =
                            evaluationTargetSelector != nullptr &&
                            modifierComposition != nullptr;
                    const std::array<std::pair<const char *, const char *>, 7>
                            evaluationPanels{{
                                    {"velocity",
                                     "velocityEvaluationSettings"},
                                    {"stunt-points",
                                     "stuntPointsEvaluationSettings"},
                                    {"precise-finish-time",
                                     "preciseFinishTimeEvaluationSettings"},
                                    {"volume-entry-time",
                                     "volumeEntryEvaluationSettings"},
                                    {"custom-volume-entry-time",
                                     "volumeEntryEvaluationSettings"},
                                    {"point-target",
                                     "pointTargetEvaluationSettings"},
                                    {"pose-target",
                                     "poseTargetEvaluationSettings"}}};
                    for (const auto &[id, objectName] : evaluationPanels) {
                        controller.setEvaluationTargetId(
                                QString::fromLatin1(id));
                        QCoreApplication::processEvents();
                        everyOwnedPanelLoaded &=
                                evaluationTargetSelector != nullptr &&
                                evaluationTargetSelector
                                                ->property("settingsLoaded")
                                                .toBool() &&
                                evaluationTargetSelector
                                                ->property(
                                                        "settingsObjectName")
                                                .toString() ==
                                        QString::fromLatin1(objectName);
                    }
                    controller.setEvaluationTargetId(
                            QStringLiteral("pose-target"));
                    QCoreApplication::processEvents();
                    const qreal expandedEvaluationHeight =
                            evaluationSection == nullptr
                            ? 0.0
                            : evaluationSection->height();
                    const qreal expandedSelectorHeight =
                            evaluationTargetSelector == nullptr
                            ? 0.0
                            : evaluationTargetSelector
                                      ->property("height")
                                      .toReal();
                    const QPointer<QObject> expandedSettingsItem =
                            evaluationTargetSelector == nullptr
                            ? nullptr
                            : evaluationTargetSelector
                                      ->property("settingsItem")
                                      .value<QObject *>();
                    const QString expandedSettingsObjectName =
                            expandedSettingsItem == nullptr
                            ? QString()
                            : expandedSettingsItem->objectName();
                    controller.setEvaluationTargetId(
                            QStringLiteral("precise-finish-time"));
                    QCoreApplication::processEvents();
                    const qreal compactEvaluationHeight =
                            evaluationSection == nullptr
                            ? 0.0
                            : evaluationSection->height();
                    const qreal compactSelectorHeight =
                            evaluationTargetSelector == nullptr
                            ? 0.0
                            : evaluationTargetSelector
                                      ->property("height")
                                      .toReal();
                    QObject *const compactSettingsItem =
                            evaluationTargetSelector == nullptr
                            ? nullptr
                            : evaluationTargetSelector
                                      ->property("settingsItem")
                                      .value<QObject *>();
                    const bool targetLayoutUpdatesImmediately =
                            expandedSettingsItem != nullptr &&
                            expandedSettingsObjectName ==
                                    QStringLiteral(
                                            "poseTargetEvaluationSettings") &&
                            compactSettingsItem != nullptr &&
                            compactSettingsItem != expandedSettingsItem.data() &&
                            compactSettingsItem->objectName() ==
                                    QStringLiteral(
                                            "preciseFinishTimeEvaluationSettings") &&
                            expandedEvaluationHeight >
                                    compactEvaluationHeight + 250.0 &&
                            expandedSelectorHeight >
                                    compactSelectorHeight + 250.0 &&
                            compactSelectorHeight < 90.0;
                    controller.setEvaluationTargetId(
                            QStringLiteral("stunt-points"));
                    QCoreApplication::processEvents();
                    QObject *const stuntPointsTimeField =
                            root->findChild<QObject *>(
                                    QStringLiteral("stuntPointsTimeField"));
                    const bool stuntPointsFieldValid =
                            stuntPointsTimeField != nullptr &&
                            stuntPointsTimeField->property("text").toString() ==
                                    QStringLiteral("6000") &&
                            stuntPointsTimeField->property("minimum").toReal() ==
                                    0.0;
                    const std::array<std::pair<const char *, const char *>, 5>
                            modifierPanels{{
                                    {"random-steering",
                                     "randomSteeringMutationSettings"},
                                    {"existing-event-perturbation",
                                     "existingEventPerturbationSettings"},
                                    {"smooth-steering",
                                     "smoothSteeringSettings"},
                                    {"input-insertion",
                                     "inputInsertionSettings"},
                                    {"input-deletion",
                                     "inputDeletionSettings"}}};
                    for (const auto &[id, objectName] : modifierPanels) {
                        controller.setModifierPassId(
                                0, QString::fromLatin1(id));
                        QCoreApplication::processEvents();
                        everyOwnedPanelLoaded &=
                                modifierComposition != nullptr &&
                                modifierComposition
                                                ->property(
                                                        "firstPassSettingsLoaded")
                                                .toBool() &&
                                modifierComposition
                                                ->property(
                                                        "firstPassSettingsObjectName")
                                                .toString() ==
                                        QString::fromLatin1(objectName);
                    }

                    const auto activateCombo = [](QObject *combo, int index) {
                        return combo != nullptr && QMetaObject::invokeMethod(
                                combo,
                                "activated",
                                Qt::DirectConnection,
                                Q_ARG(int, index));
                    };
                    QObject *const firstPassForCombo =
                            modifierComposition
                                    ->property("firstRenderedPass")
                                    .value<QObject *>();
                    QObject *const modifierPassCombo =
                            firstPassForCombo == nullptr
                            ? nullptr
                            : firstPassForCombo->findChild<QObject *>(
                                      QStringLiteral("modifierPassCombo0"));

                    bool dropdownStateUpdates =
                            activateCombo(modifierPassCombo, 2);
                    QCoreApplication::processEvents();
                    QCoreApplication::processEvents();
                    dropdownStateUpdates &=
                            controller.modifierPasses()
                                            .front()
                                            .toMap()
                                            .value(QStringLiteral("id"))
                                            .toString() ==
                                    QStringLiteral("smooth-steering") &&
                            modifierComposition
                                            ->property(
                                                    "firstPassSettingsObjectName")
                                            .toString() ==
                                    QStringLiteral("smoothSteeringSettings");

                    dropdownStateUpdates &=
                            activateCombo(modifierPassCombo, 3);
                    QCoreApplication::processEvents();
                    QCoreApplication::processEvents();
                    QObject *const insertionSettings =
                            modifierComposition
                                    ->property("firstPassSettingsItem")
                                    .value<QObject *>();
                    QObject *const insertionModeCombo =
                            insertionSettings == nullptr
                            ? nullptr
                            : insertionSettings->findChild<QObject *>(
                                      QStringLiteral(
                                              "insertionSteeringModeCombo"));
                    QObject *const insertionMinimumSlider =
                            insertionSettings == nullptr
                            ? nullptr
                            : insertionSettings->findChild<QObject *>(
                                      QStringLiteral(
                                              "insertionAbsoluteMinimumSlider"));
                    QObject *const insertionMaximumSlider =
                            insertionSettings == nullptr
                            ? nullptr
                            : insertionSettings->findChild<QObject *>(
                                      QStringLiteral(
                                              "insertionAbsoluteMaximumSlider"));
                    dropdownStateUpdates &=
                            activateCombo(insertionModeCombo, 1);
                    QCoreApplication::processEvents();
                    const QVariantMap insertionPass =
                            controller.modifierPasses().front().toMap();
                    dropdownStateUpdates &=
                            insertionPass.value(QStringLiteral("settings"))
                                            .toMap()
                                            .value(QStringLiteral("steerMode"))
                                            .toString() ==
                                    QStringLiteral("absolute");

                    const bool insertionSlidersValid =
                            insertionMinimumSlider != nullptr &&
                            insertionMaximumSlider != nullptr &&
                            insertionMinimumSlider->property("from").toReal() ==
                                    -1.0 &&
                            insertionMinimumSlider->property("to").toReal() ==
                                    1.0 &&
                            insertionMaximumSlider->property("from").toReal() ==
                                    -1.0 &&
                            insertionMaximumSlider->property("to").toReal() ==
                                    1.0;

                    dropdownStateUpdates &=
                            activateCombo(evaluationTargetCombo, 5);
                    QCoreApplication::processEvents();
                    QCoreApplication::processEvents();
                    dropdownStateUpdates &=
                            controller.evaluationTargetId() ==
                                    QStringLiteral("point-target") &&
                            evaluationTargetSelector
                                            ->property("settingsObjectName")
                                            .toString() ==
                                    QStringLiteral(
                                            "pointTargetEvaluationSettings");

                    dropdownStateUpdates &=
                            activateCombo(evaluationTargetCombo, 6);
                    QCoreApplication::processEvents();
                    QCoreApplication::processEvents();
                    QObject *const poseEditor =
                            evaluationTargetSelector
                                    ->property("settingsItem")
                                    .value<QObject *>();
                    QObject *const rotationWeightSlider =
                            root->findChild<QObject *>(
                                    QStringLiteral("rotationWeightSlider"));
                    QObject *const poseSelector =
                            poseEditor == nullptr ? nullptr
                            : poseEditor->findChild<QObject *>(
                                      QStringLiteral("poseTargetSelector"));
                    QObject *const poseNameField =
                            poseEditor == nullptr ? nullptr
                            : poseEditor->findChild<QObject *>(
                                      QStringLiteral("poseTargetNameField"));
                    QObject *const posePositionSettings =
                            poseEditor == nullptr ? nullptr
                            : poseEditor->findChild<QObject *>(
                                      QStringLiteral(
                                              "poseTargetPositionSettings"));
                    QObject *const poseRotationSettings =
                            poseEditor == nullptr ? nullptr
                            : poseEditor->findChild<QObject *>(
                                      QStringLiteral(
                                              "poseTargetRotationSettings"));
                    const int placedPoseIndex =
                            controller.poseTargets()->addTarget(
                                    7.0,
                                    3.0,
                                    -4.0,
                                    QQuaternion::fromEulerAngles(
                                            10.0F, 20.0F, 30.0F));
                    QCoreApplication::processEvents();
                    const int poseModels =
                            root->findChildren<QObject *>(
                                        QStringLiteral(
                                                "poseTargetCarModel"))
                                    .size();
                    const double initialPoseX =
                            controller.poseTargets()
                                    ->selectedTarget()
                                    .value(QStringLiteral("x"))
                                    .toDouble();
                    const double initialPoseYaw =
                            controller.poseTargets()
                                    ->selectedTarget()
                                    .value(QStringLiteral("yawDegrees"))
                                    .toDouble();
                    QVariant beganPoseMove;
                    QVariant beganPoseRotation;
                    bool poseSliderValid =
                            rotationWeightSlider != nullptr &&
                            rotationWeightSlider->property("from").toReal() ==
                                    0.0 &&
                            rotationWeightSlider->property("to").toReal() ==
                                    100.0 &&
                            poseEditor != nullptr &&
                            poseSelector != nullptr &&
                            poseNameField != nullptr &&
                            posePositionSettings != nullptr &&
                            poseRotationSettings != nullptr &&
                            placedPoseIndex == 1 &&
                            poseSelector->property("count").toInt() == 2 &&
                            poseSelector->property("currentIndex").toInt() ==
                                    1 &&
                            poseModels >= 2 &&
                            QMetaObject::invokeMethod(
                                    viewport,
                                    "beginPoseInteraction",
                                    Q_RETURN_ARG(
                                            QVariant, beganPoseMove),
                                    Q_ARG(
                                            QVariant,
                                            QVariant(
                                                    QStringLiteral(
                                                            "pose-move"))),
                                    Q_ARG(
                                            QVariant,
                                            QVariant(
                                                    QStringLiteral("x"))),
                                    Q_ARG(QVariant, QVariant(100.0)),
                                    Q_ARG(QVariant, QVariant(100.0))) &&
                            beganPoseMove.toBool() &&
                            QMetaObject::invokeMethod(
                                    viewport,
                                    "updatePoseInteraction",
                                    Q_ARG(QVariant, QVariant(140.0)),
                                    Q_ARG(QVariant, QVariant(100.0))) &&
                            QMetaObject::invokeMethod(
                                    viewport, "endPoseInteraction") &&
                            QMetaObject::invokeMethod(
                                    viewport,
                                    "beginPoseInteraction",
                                    Q_RETURN_ARG(
                                            QVariant,
                                            beganPoseRotation),
                                    Q_ARG(
                                            QVariant,
                                            QVariant(
                                                    QStringLiteral(
                                                            "pose-rotate"))),
                                    Q_ARG(
                                            QVariant,
                                            QVariant(
                                                    QStringLiteral("yaw"))),
                                    Q_ARG(QVariant, QVariant(100.0)),
                                    Q_ARG(QVariant, QVariant(100.0))) &&
                            beganPoseRotation.toBool() &&
                            QMetaObject::invokeMethod(
                                    viewport,
                                    "updatePoseInteraction",
                                    Q_ARG(QVariant, QVariant(140.0)),
                                    Q_ARG(QVariant, QVariant(100.0))) &&
                            QMetaObject::invokeMethod(
                                    viewport, "endPoseInteraction");
                    controller.focusSelectedPoseTarget();
                    QCoreApplication::processEvents();
                    poseSliderValid &=
                            controller.poseTargets()
                                            ->selectedTarget()
                                            .value(QStringLiteral("x"))
                                            .toDouble() > initialPoseX &&
                            controller.poseTargets()
                                            ->selectedTarget()
                                            .value(
                                                    QStringLiteral(
                                                            "yawDegrees"))
                                            .toDouble() > initialPoseYaw &&
                            controller.evaluationTargetSettings()
                                            .value(QStringLiteral("x"))
                                            .toDouble() > initialPoseX &&
                            viewport->property("cuboidFocused").toBool() &&
                            viewport->property("cameraTarget")
                                            .value<QVector3D>() ==
                                    controller.poseTargets()
                                            ->selectedTarget()
                                            .value(
                                                    QStringLiteral(
                                                            "position"))
                                            .value<QVector3D>() &&
                            root->findChildren<QObject *>(
                                        QStringLiteral(
                                                "poseTargetMoveHandle"))
                                            .size() >= 6 &&
                            root->findChildren<QObject *>(
                                        QStringLiteral(
                                                "poseTargetRotationHandle"))
                                            .size() >= 6;

                    dropdownStateUpdates &=
                            activateCombo(evaluationTargetCombo, 3);
                    QCoreApplication::processEvents();
                    QCoreApplication::processEvents();
                    QObject *const cuboidEditor =
                            evaluationTargetSelector
                                    ->property("settingsItem")
                                    .value<QObject *>();
                    QObject *const cuboidSelector =
                            cuboidEditor == nullptr ? nullptr
                            : cuboidEditor->findChild<QObject *>(
                                    QStringLiteral("shapeTargetSelector"));
                    QObject *const addCuboidButton =
                            cuboidEditor == nullptr ? nullptr
                            : cuboidEditor->findChild<QObject *>(
                                    QStringLiteral("addShapeTargetButton"));
                    QObject *const duplicateCuboidButton =
                            cuboidEditor == nullptr ? nullptr
                            : cuboidEditor->findChild<QObject *>(
                                    QStringLiteral(
                                            "duplicateShapeTargetButton"));
                    QObject *const focusCuboidButton =
                            cuboidEditor == nullptr ? nullptr
                            : cuboidEditor->findChild<QObject *>(
                                    QStringLiteral(
                                            "focusShapeTargetButton"));
                    QObject *const removeCuboidButton =
                            cuboidEditor == nullptr ? nullptr
                            : cuboidEditor->findChild<QObject *>(
                                    QStringLiteral(
                                            "removeShapeTargetButton"));
                    QObject *const cuboidNameField =
                            cuboidEditor == nullptr ? nullptr
                            : cuboidEditor->findChild<QObject *>(
                                    QStringLiteral("shapeTargetNameField"));
                    const int placedIndex =
                            controller.cuboidTargets()->addTarget(
                                    14.0, 3.0, -2.0);
                    QCoreApplication::processEvents();
                    const int initialCuboidModels =
                            root->findChildren<QObject *>(
                                        QStringLiteral("cuboidTargetModel"))
                                    .size();
                    const double initialCuboidSize =
                            controller.cuboidTargets()
                                    ->selectedTarget()
                                    .value(QStringLiteral("sizeX"))
                                    .toDouble();
                    QVariant beganResize;
                    bool cuboidEditorValid =
                            cuboidEditor != nullptr &&
                            cuboidSelector != nullptr &&
                            addCuboidButton != nullptr &&
                            duplicateCuboidButton != nullptr &&
                            focusCuboidButton != nullptr &&
                            removeCuboidButton != nullptr &&
                            cuboidNameField != nullptr &&
                            placedIndex == 1 &&
                            cuboidSelector->property("count").toInt() == 3 &&
                            cuboidSelector->property("currentIndex").toInt() ==
                                    1 &&
                            initialCuboidModels >= 4 &&
                            removeCuboidButton->property("enabled").toBool();
                    controller.focusSelectedCuboid();
                    QCoreApplication::processEvents();
                    cuboidEditorValid &=
                            viewport != nullptr &&
                            viewport->property("cuboidFocused").toBool() &&
                            viewport->property("cameraTarget")
                                            .value<QVector3D>() ==
                                    QVector3D(14.0F, 3.0F, -2.0F) &&
                            QMetaObject::invokeMethod(
                                    viewport,
                                    "beginCuboidInteraction",
                                    Q_RETURN_ARG(QVariant, beganResize),
                                    Q_ARG(QVariant,
                                          QVariant(QStringLiteral("resize"))),
                                    Q_ARG(QVariant,
                                          QVariant(QStringLiteral("x"))),
                                    Q_ARG(QVariant, QVariant(100.0)),
                                    Q_ARG(QVariant, QVariant(100.0))) &&
                            beganResize.toBool() &&
                            QMetaObject::invokeMethod(
                                    viewport,
                                    "updateCuboidInteraction",
                                    Q_ARG(QVariant, QVariant(140.0)),
                                    Q_ARG(QVariant, QVariant(100.0))) &&
                            QMetaObject::invokeMethod(
                                    viewport, "endCuboidInteraction");
                    QCoreApplication::processEvents();
                    cuboidEditorValid &=
                            controller.cuboidTargets()
                                            ->selectedTarget()
                                            .value(QStringLiteral("sizeX"))
                                            .toDouble() >
                                    initialCuboidSize &&
                            controller.evaluationTargetSettings()
                                            .value(QStringLiteral("sizeX"))
                                            .toDouble() >
                                    initialCuboidSize;
                    if (!cuboidEditorValid) {
                        std::cerr
                                << "cuboid editor checks: objects="
                                << (cuboidEditor != nullptr) << "/"
                                << (cuboidSelector != nullptr) << "/"
                                << (addCuboidButton != nullptr) << "/"
                                << (duplicateCuboidButton != nullptr) << "/"
                                << (focusCuboidButton != nullptr) << "/"
                                << (removeCuboidButton != nullptr) << "/"
                                << (cuboidNameField != nullptr)
                                << ", placed=" << placedIndex
                                << ", combo="
                                << (cuboidSelector == nullptr
                                            ? -1
                                            : cuboidSelector
                                                      ->property("count")
                                                      .toInt())
                                << "/"
                                << (cuboidSelector == nullptr
                                            ? -1
                                            : cuboidSelector
                                                      ->property("currentIndex")
                                                      .toInt())
                                << ", models=" << initialCuboidModels
                                << ", focus="
                                << (viewport != nullptr &&
                                    viewport->property("cuboidFocused").toBool())
                                << ", begin=" << beganResize.toBool()
                                << ", size="
                                << controller.cuboidTargets()
                                           ->selectedTarget()
                                           .value(QStringLiteral("sizeX"))
                                           .toDouble()
                                << "/" << initialCuboidSize << '\n';
                    }
                    dropdownStateUpdates &= cuboidEditorValid;

                    dropdownStateUpdates &=
                            activateCombo(evaluationTargetCombo, 4);
                    QCoreApplication::processEvents();
                    QCoreApplication::processEvents();
                    QObject *const customEditor =
                            evaluationTargetSelector
                                    ->property("settingsItem")
                                    .value<QObject *>();
                    QObject *const customPlaneSetting =
                            customEditor == nullptr ? nullptr
                            : customEditor->findChild<QObject *>(
                                      QStringLiteral(
                                              "customVolumePlaneSetting"));
                    QObject *const customDepthField =
                            customEditor == nullptr ? nullptr
                            : customEditor->findChild<QObject *>(
                                      QStringLiteral(
                                              "customVolumeDepthField"));
                    QObject *const drawCustomVolumeButton =
                            customEditor == nullptr ? nullptr
                            : customEditor->findChild<QObject *>(
                                      QStringLiteral(
                                              "drawCustomVolumeButton"));
                    QObject *const cancelCustomDrawingButton =
                            customEditor == nullptr ? nullptr
                            : customEditor->findChild<QObject *>(
                                      QStringLiteral(
                                              "cancelCustomVolumeDrawingButton"));
                    const int initialCustomModels =
                            root->findChildren<QObject *>(
                                        QStringLiteral(
                                                "customVolumeTargetModel"))
                                    .size();
                    controller.customVolumeTargets()->setDepth(
                            0, QStringLiteral("6.5"));
                    controller.beginCustomVolumeDrawing();
                    QVariant projectedPlanePoint;
                    QVariant secondProjectedPlanePoint;
                    bool customVolumeEditorValid =
                            customEditor != nullptr &&
                            customPlaneSetting != nullptr &&
                            customDepthField != nullptr &&
                            drawCustomVolumeButton != nullptr &&
                            cancelCustomDrawingButton != nullptr &&
                            controller.customVolumeDrawing() &&
                            initialCustomModels >= 2 &&
                            QMetaObject::invokeMethod(
                                    viewport,
                                    "customPlanePoint",
                                    Q_RETURN_ARG(
                                            QVariant,
                                            projectedPlanePoint),
                                    Q_ARG(QVariant, QVariant(400.0)),
                                    Q_ARG(QVariant, QVariant(300.0))) &&
                            QMetaObject::invokeMethod(
                                    viewport,
                                    "customPlanePoint",
                                    Q_RETURN_ARG(
                                            QVariant,
                                            secondProjectedPlanePoint),
                                    Q_ARG(QVariant, QVariant(600.0)),
                                    Q_ARG(QVariant, QVariant(450.0))) &&
                            projectedPlanePoint.canConvert<QVector3D>() &&
                            secondProjectedPlanePoint.canConvert<QVector3D>() &&
                            projectedPlanePoint.value<QVector3D>()
                                            .distanceToPoint(
                                                    secondProjectedPlanePoint
                                                            .value<QVector3D>()) >
                                    0.01F &&
                            controller.customVolumeTargets()->addVertexWorld(
                                    -2.0, 0.0, -2.0) &&
                            controller.customVolumeTargets()->addVertexWorld(
                                    2.0, 0.0, -2.0) &&
                            controller.customVolumeTargets()->addVertexWorld(
                                    0.0, 0.0, 2.0);
                    controller.finishCustomVolumeDrawing();
                    controller.focusSelectedCustomVolume();
                    QCoreApplication::processEvents();
                    if (projectedPlanePoint.canConvert<QVector3D>() &&
                        secondProjectedPlanePoint.canConvert<QVector3D>() &&
                        projectedPlanePoint.value<QVector3D>()
                                        .distanceToPoint(
                                                secondProjectedPlanePoint
                                                        .value<QVector3D>()) <=
                                0.01F) {
                        const QVector3D first =
                                projectedPlanePoint.value<QVector3D>();
                        const QVector3D second =
                                secondProjectedPlanePoint.value<QVector3D>();
                        std::cerr
                                << "custom plane projection collapsed: "
                                << first.x() << "," << first.y() << ","
                                << first.z() << " / " << second.x() << ","
                                << second.y() << "," << second.z() << '\n';
                    }
                    customVolumeEditorValid &=
                            !controller.customVolumeDrawing() &&
                            controller.evaluationTargetSettings()
                                            .value(QStringLiteral("depth"))
                                            .toString() ==
                                    QStringLiteral("6.5") &&
                            controller.evaluationTargetSettings()
                                            .value(QStringLiteral("polygon"))
                                            .toString() ==
                                    QStringLiteral("-2,-2;2,-2;0,2") &&
                            viewport->property("cuboidFocused").toBool() &&
                            root->findChildren<QObject *>(
                                        QStringLiteral(
                                                "customVolumePlaneChoiceXZ"))
                                            .size() >= 2 &&
                            root->findChildren<QObject *>(
                                        QStringLiteral(
                                                "customVolumeDepthHandle"))
                                            .size() >= 2;
                    dropdownStateUpdates &= customVolumeEditorValid;

                    dropdownStateUpdates &=
                            activateCombo(evaluationTargetCombo, 0);
                    dropdownStateUpdates &=
                            activateCombo(modifierPassCombo, 0);
                    QCoreApplication::processEvents();
                    QCoreApplication::processEvents();
                    QObject *const minimumAlignmentSlider =
                            root->findChild<QObject *>(
                                    QStringLiteral("minimumAlignmentSlider"));
                    const bool velocitySliderValid =
                            minimumAlignmentSlider != nullptr &&
                            minimumAlignmentSlider->property("from").toReal() ==
                                    -100.0 &&
                            minimumAlignmentSlider->property("to").toReal() ==
                                    100.0;

                    controller.setModifierPassId(
                            0, QStringLiteral("random-steering"));
                    controller.setEvaluationTargetId(
                            QStringLiteral("velocity"));
                    QCoreApplication::processEvents();
                    QObject *const firstPassBefore =
                            modifierComposition
                                    ->property("firstRenderedPass")
                                    .value<QObject *>();
                    QObject *const firstPassSettings =
                            modifierComposition
                                    ->property("firstPassSettingsItem")
                                    .value<QObject *>();
                    QObject *const minimumTimeField = firstPassSettings == nullptr
                            ? nullptr
                            : firstPassSettings->findChild<QObject *>(
                                      QStringLiteral("minimumTimeField"));
                    const int rebuildCountBefore =
                            modifierComposition->property("rebuildCount").toInt();
                    const bool focusRequested = minimumTimeField != nullptr &&
                            QMetaObject::invokeMethod(
                                    minimumTimeField,
                                    "forceActiveFocus",
                                    Qt::DirectConnection);
                    QCoreApplication::processEvents();
                    const bool focusedBeforeUpdate =
                            minimumTimeField != nullptr &&
                            (minimumTimeField->property("activeFocus").toBool() ||
                             minimumTimeField->property("focus").toBool());
                    controller.setModifierPassSetting(
                            0,
                            QStringLiteral("minTimeMs"),
                            QStringLiteral("1010"));
                    QCoreApplication::processEvents();
                    QCoreApplication::processEvents();
                    QObject *const firstPassAfter =
                            modifierComposition
                                    ->property("firstRenderedPass")
                                    .value<QObject *>();
                    const QVariantMap updatedPass =
                            controller.modifierPasses().front().toMap();
                    const bool modifierFocusStable = focusRequested &&
                            focusedBeforeUpdate &&
                            firstPassBefore == firstPassAfter &&
                            modifierComposition->property("rebuildCount").toInt() ==
                                    rebuildCountBefore &&
                            minimumTimeField != nullptr &&
                            (minimumTimeField->property("activeFocus").toBool() ||
                             minimumTimeField->property("focus").toBool()) &&
                            updatedPass.value(QStringLiteral("settings"))
                                            .toMap()
                                            .value(QStringLiteral("minTimeMs"))
                                            .toString() ==
                                    QStringLiteral("1010");
                    const bool unboundedFieldsScrubbable =
                            minimumTimeField != nullptr &&
                            minimumTimeField->property("scrubbable").toBool();
                    controller.setModifierPassSetting(
                            0,
                            QStringLiteral("minTimeMs"),
                            QStringLiteral("1000"));
                    QCoreApplication::processEvents();
                    if (!algorithmSelectorsValid) {
                        const auto count = [](QObject *object) {
                            return object == nullptr
                                    ? -1
                                    : object->property("count").toInt();
                        };
                        const auto current = [](QObject *object) {
                            return object == nullptr
                                    ? QStringLiteral("<missing>")
                                    : object->property("currentValue")
                                              .toString();
                        };
                        std::cerr
                                << "algorithm structure failed: search="
                                << count(searchAlgorithmCombo) << "/"
                                << current(searchAlgorithmCombo).toStdString()
                                << ", modifierComposition="
                                << (modifierComposition != nullptr)
                                << "/"
                                << (modifierComposition == nullptr
                                            ? -1
                                            : modifierComposition
                                                      ->property("passCount")
                                                      .toInt())
                                << "/"
                                << (modifierComposition == nullptr
                                            ? -1
                                            : modifierComposition
                                                      ->property(
                                                              "passModelCount")
                                                      .toInt())
                                << "/"
                                << (modifierComposition == nullptr
                                            ? -1
                                            : modifierComposition
                                                      ->property(
                                                              "renderedPassCount")
                                                      .toInt())
                                << ", controllerPasses="
                                << controller.modifierPasses().size()
                                << ", firstPass="
                                << modifierComposition
                                           ->property("firstPassOptionCount")
                                           .toInt() << "/"
                                << modifierComposition
                                           ->property("firstPassSelectedId")
                                           .toString().toStdString()
                                << "/"
                                << modifierComposition
                                           ->property("firstPassSettingsLoaded")
                                           .toBool()
                                << ", addCombo=" << count(addModifierCombo)
                                << ", addButton="
                                << (addModifierButton != nullptr)
                                << ", evaluation="
                                << count(evaluationTargetCombo) << "/"
                                << current(evaluationTargetCombo).toStdString()
                                << ", basicSettings="
                                << (basicBruteForceSettings != nullptr)
                                << ", velocitySettings="
                                << (velocitySettings != nullptr) << '\n';
                    }
                    editorStructure = timeline != nullptr &&
                            timeline->viewer() == &viewer &&
                            timeline->isEnabled() &&
                            timelinePanel != nullptr && viewport != nullptr &&
                            timelinePanel->x() < viewport->x() &&
                            runSelectorValid &&
                            globalBaseInputScriptPlacement &&
                            baseInputScriptUiValid &&
                            bestInputsUiValid &&
                            searchControlsValid && searchMetricsUiValid &&
                            removedSectionDescriptions &&
                            automaticPacksUi && backendSelectorValid &&
                            algorithmSelectorsValid &&
                            autoPromoteBestValid &&
                            everyOwnedPanelLoaded && stuntPointsFieldValid &&
                            targetLayoutUpdatesImmediately &&
                            configurationSectionsValid &&
                            comboSlotsStyled && settingComboTextValid &&
                            modifierPassLayoutValid && debuggerUiValid &&
                            wheelScrollingValid &&
                            dropdownStateUpdates && insertionSlidersValid &&
                            poseSliderValid && velocitySliderValid &&
                            modifierFocusStable && unboundedFieldsScrubbable &&
                            playPause != nullptr && jumpStart != nullptr &&
                            jumpEnd != nullptr &&
                            playPause->property("enabled").toBool() &&
                            std::abs(playPause->property("width").toReal() -
                                     42.0) < 0.1 &&
                            std::abs(playPause->property("height").toReal() -
                                     42.0) < 0.1 &&
                            std::abs(jumpStart->property("width").toReal() -
                                     42.0) < 0.1 &&
                            std::abs(jumpStart->property("height").toReal() -
                                     42.0) < 0.1 &&
                            std::abs(jumpEnd->property("width").toReal() -
                                     42.0) < 0.1 &&
                            std::abs(jumpEnd->property("height").toReal() -
                                     42.0) < 0.1 &&
                            IsCenteredIcon(playIcon, 18.0) &&
                            IsCenteredIcon(pauseIcon, 18.0) &&
                            IsCenteredIcon(jumpStartIcon, 18.0) &&
                            IsCenteredIcon(jumpEndIcon, 18.0) &&
                            playIcon->isVisible() && !pauseIcon->isVisible() &&
                            ContainsStandardSlider(root) &&
                            !ContainsText(
                                    root,
                                    QStringLiteral("INPUT TIMELINE")) &&
                            keyboardStepping && manualDrivingUi &&
                            compactViewerHeader;
                    if (!editorStructure) {
                        std::cerr
                                << "editor checks: runSelector=" << runSelectorValid
                                << ", baseInputScript="
                                << baseInputScriptUiValid
                                << ", globalBaseInput="
                                << globalBaseInputScriptPlacement
                                << " (packs="
                                << (packsDirectorySection != nullptr
                                            ? packsDirectorySection->y()
                                            : -1.0)
                                << "+"
                                << (packsDirectorySection != nullptr
                                            ? packsDirectorySection->height()
                                            : -1.0)
                                << ", script="
                                << (baseInputScriptSection != nullptr
                                            ? baseInputScriptSection->y()
                                            : -1.0)
                                << "+"
                                << (baseInputScriptSection != nullptr
                                            ? baseInputScriptSection->height()
                                            : -1.0)
                                << ", tabs="
                                << (toolTabs != nullptr
                                            ? toolTabs->y() : -1.0)
                                << ")"
                                << ", bestInputs=" << bestInputsUiValid
                                << ", searchControls=" << searchControlsValid
                                << ", autoPacks=" << automaticPacksUi
                                << ", backend=" << backendSelectorValid
                                << ", selectors=" << algorithmSelectorsValid
                                << ", panels=" << everyOwnedPanelLoaded
                                << ", stuntField=" << stuntPointsFieldValid
                                << ", sections=" << configurationSectionsValid
                                << ", comboStyle=" << comboSlotsStyled
                                << ", comboText=" << settingComboTextValid
                                << ", passLayout=" << modifierPassLayoutValid
                                << ", debugger=" << debuggerUiValid
                                << ", wheel=" << wheelScrollingValid
                                << ", dropdown=" << dropdownStateUpdates
                                << ", insertion=" << insertionSlidersValid
                                << ", pose=" << poseSliderValid
                                << ", velocity=" << velocitySliderValid
                                << ", focus=" << modifierFocusStable
                                << ", scrub=" << unboundedFieldsScrubbable
                                << ", keyboard=" << keyboardStepping
                                << ", manual=" << manualDrivingUi
                                << ", compactHeader="
                                << compactViewerHeader
                                << " (dock="
                                << (playbackDock != nullptr
                                            ? playbackDock->width()
                                            : -1.0)
                                << ", button="
                                << (manualDriveButton != nullptr)
                                << "/"
                                << (manualDriveButton != nullptr
                                            ? manualDriveButton
                                                      ->property("enabled")
                                                      .toBool()
                                            : true)
                                << ", status="
                                << (manualDriveStatus != nullptr)
                                << "/"
                                << (manualDriveStatus != nullptr
                                            ? manualDriveStatus->isVisible()
                                            : true)
                                << ", focus="
                                << (manualInputFocus != nullptr)
                                << ", map="
                                << manualMapping(Qt::Key_Left)
                                           .toStdString()
                                << "/"
                                << manualMapping(Qt::Key_A).toStdString()
                                << "/"
                                << manualMapping(Qt::Key_Q).toStdString()
                                << "/"
                                << manualMapping(Qt::Key_Right)
                                           .toStdString()
                                << "/"
                                << manualMapping(Qt::Key_Up).toStdString()
                                << "/"
                                << manualMapping(Qt::Key_W).toStdString()
                                << "/"
                                << manualMapping(Qt::Key_Z).toStdString()
                                << "/"
                                << manualMapping(Qt::Key_Down).toStdString()
                                << "/"
                                << manualMapping(Qt::Key_S).toStdString()
                                << ")"
                                << '\n';
                    }
                    if (filled == nullptr || wire == nullptr) {
                        std::cerr << "track models were not created\n";
                        completed = true;
                        application.quit();
                        return;
                    }
                    QQuickWindow *const quickWindow =
                            qobject_cast<QQuickWindow *>(root);
                    if (quickWindow == nullptr) {
                        std::cerr << "Main.qml root is not a window\n";
                        completed = true;
                        application.quit();
                        return;
                    }

                    controller.setBaseInputScript(
                            QStringLiteral(
                                    "0.00 press up\n"
                                    "0.10 rel up"));
                    QCoreApplication::processEvents();
                    const QVariantList initialPreviewPaths =
                            viewer.trajectoryPaths();
                    QObject *const previewGeometry =
                            initialPreviewPaths.size() == 1
                            ? initialPreviewPaths.front()
                                      .toMap()
                                      .value(QStringLiteral("geometry"))
                                      .value<QObject *>()
                            : nullptr;
                    viewer.jumpToEnd();
                    const QVector3D shortAccelerationPosition =
                            viewer.carPosition();
                    controller.setBaseInputScript(
                            QStringLiteral(
                                    "0.00 press up\n"
                                    "0.20 rel up"));
                    QCoreApplication::processEvents();
                    viewer.jumpToEnd();
                    const QVector3D valueEditedPosition =
                            viewer.carPosition();
                    const bool valueEditUpdatedPreview =
                            viewer.trajectoryCount() == 1 &&
                            viewer.runCount() == 1 &&
                            viewer.currentInputScript().contains(
                                    QStringLiteral("0.20 rel up")) &&
                            (valueEditedPosition -
                             shortAccelerationPosition)
                                            .lengthSquared() >
                                    0.000001f;
                    controller.setBaseInputScript(
                            QStringLiteral(
                                    "0.00 press up\n"
                                    "0.00 press left\n"
                                    "0.20 rel left\n"
                                    "0.20 rel up"));
                    QCoreApplication::processEvents();
                    viewer.jumpToEnd();
                    const bool eventEditUpdatedPreview =
                            viewer.trajectoryCount() == 1 &&
                            viewer.runCount() == 1 &&
                            viewer.trajectoryPaths()
                                            .front()
                                            .toMap()
                                            .value(QStringLiteral("geometry"))
                                            .value<QObject *>() ==
                                    previewGeometry &&
                            viewer.previewInputScript().contains(
                                    QStringLiteral("press left")) &&
                            viewer.inputSample(1).steering < -0.99f;
                    controller.setBaseInputScript(
                            QStringLiteral("not a command"));
                    QCoreApplication::processEvents();
                    QCoreApplication::sendPostedEvents(
                            nullptr, QEvent::DeferredDelete);
                    const bool invalidEditRemovedStalePreview =
                            viewer.trajectoryCount() == 0 &&
                            viewer.runCount() == 0 &&
                            root->findChildren<QObject *>(
                                        QStringLiteral(
                                                "trajectoryPathModel"))
                                    .isEmpty();
                    controller.setBaseInputScript(
                            QStringLiteral(
                                    "0.00 press up\n"
                                    "0.00 press left\n"
                                    "0.20 rel left\n"
                                    "0.20 rel up"));
                    QCoreApplication::processEvents();
                    QCoreApplication::sendPostedEvents(
                            nullptr, QEvent::DeferredDelete);
                    const QList<QObject *> trajectoryModels =
                            root->findChildren<QObject *>(
                                    QStringLiteral("trajectoryPathModel"));
                    const QList<QObject *> rayTracingTrajectoryModels =
                            root->findChildren<QObject *>(
                                    QStringLiteral(
                                            "rayTracingTrajectoryPathModel"));
                    QObject *const rayTracingTrajectoryOverlay =
                            root->findChild<QObject *>(
                                    QStringLiteral(
                                            "rayTracingTrajectoryOverlay"));
                    const QVariantList finalPreviewPaths =
                            viewer.trajectoryPaths();
                    const bool trajectoryPreviewUiValid =
                            valueEditUpdatedPreview &&
                            eventEditUpdatedPreview &&
                            invalidEditRemovedStalePreview &&
                            root->findChild<QObject *>(
                                    QStringLiteral(
                                            "saveInputTrajectoryButton")) ==
                                    nullptr &&
                            root->findChild<QObject *>(
                                    QStringLiteral(
                                            "saveInputTrajectoryShortcut")) ==
                                    nullptr &&
                            !ContainsText(
                                    root,
                                    QStringLiteral("Save trajectory")) &&
                            viewer.previewInputScript() ==
                                    controller.baseInputScript() &&
                            viewer.trajectoryCount() == 1 &&
                            viewer.runCount() == 1 &&
                            viewer.selectedRunId() ==
                                    QStringLiteral("preview") &&
                            finalPreviewPaths.size() == 1 &&
                            finalPreviewPaths.front()
                                            .toMap()
                                            .value(QStringLiteral("kind"))
                                            .toString() ==
                                    QStringLiteral("preview") &&
                            finalPreviewPaths.front()
                                            .toMap()
                                            .value(QStringLiteral("name"))
                                            .toString() ==
                                    QStringLiteral("Manual") &&
                            trajectoryModels.size() == 1 &&
                            rayTracingTrajectoryModels.size() == 1 &&
                            rayTracingTrajectoryOverlay != nullptr &&
                            !rayTracingTrajectoryOverlay
                                     ->property("visible").toBool() &&
                            trajectoryModels.front()
                                    ->property("geometry")
                                    .value<QObject *>() != nullptr &&
                            trajectoryModels.front()
                                    ->property("visible").toBool() &&
                            trajectoryModels.front()
                                            ->property("geometry")
                                            .value<QObject *>() ==
                                    previewGeometry;
                    if (!trajectoryPreviewUiValid) {
                        std::cerr
                                << "automatic preview UI checks failed: value="
                                << valueEditUpdatedPreview
                                << ", event=" << eventEditUpdatedPreview
                                << ", invalid="
                                << invalidEditRemovedStalePreview
                                << ", paths=" << finalPreviewPaths.size()
                                << ", runs=" << viewer.runCount()
                                << ", selected="
                                << viewer.selectedRunId().toStdString()
                                << ", raster=" << trajectoryModels.size()
                                << ", ray="
                                << rayTracingTrajectoryModels.size()
                                << ", geometry="
                                << (trajectoryModels.size() == 1 &&
                                    trajectoryModels.front()
                                                    ->property("geometry")
                                                    .value<QObject *>() ==
                                            previewGeometry)
                                << ", scriptSync="
                                << (viewer.previewInputScript() ==
                                    controller.baseInputScript())
                                << '\n';
                    }

                    const QVector3D baselinePosition = viewer.carPosition();
                    const QVector3D bestPosition =
                            baselinePosition + QVector3D(5.0f, 0.0f, 0.0f);
                    std::vector<forevertas::SearchTimelineFrame> bestFrames;
                    bestFrames.reserve(3u);
                    for (std::int64_t timeMs : {0, 10, 20}) {
                        forevertas::SearchTimelineFrame frame;
                        frame.timeMs = timeMs;
                        frame.positionX = baselinePosition.x() +
                                5.0f + static_cast<float>(timeMs) / 10.0f;
                        frame.positionY = baselinePosition.y();
                        frame.positionZ = baselinePosition.z();
                        frame.rotationW = 1.0f;
                        frame.accelerate = timeMs >= 10 ? 1.0f : 0.0f;
                        frame.steering = static_cast<float>(timeMs) / 20.0f;
                        bestFrames.push_back(frame);
                    }
                    const std::vector<forevertas::SandboxInputEvent>
                            bestInputs{
                                    SwitchInput(
                                            0,
                                            forevertas::SandboxInputAction::
                                                    RaceRunning,
                                            true),
                                    SwitchInput(
                                            0,
                                            forevertas::SandboxInputAction::
                                                    Accelerate,
                                            true),
                                    SwitchInput(
                                            20,
                                            forevertas::SandboxInputAction::
                                                    Brake,
                                            true)};
                    viewer.addSearchRun(QString::fromLocal8Bit(argv[1]),
                                        QString::fromLocal8Bit(argv[2]),
                                        bestFrames,
                                        bestInputs);
                    std::vector<forevertas::SearchTimelineFrame>
                            firstImprovement = bestFrames;
                    std::vector<forevertas::SearchTimelineFrame>
                            secondImprovement = bestFrames;
                    for (forevertas::SearchTimelineFrame &frame :
                         firstImprovement) {
                        frame.positionZ += 2.0f;
                    }
                    for (forevertas::SearchTimelineFrame &frame :
                         secondImprovement) {
                        frame.positionZ += 4.0f;
                    }
                    viewer.addSearchImprovement(
                            QString::fromLocal8Bit(argv[1]),
                            QString::fromLocal8Bit(argv[2]),
                            firstImprovement,
                            QStringLiteral("optimized-cpu"),
                            9u,
                            1u);
                    viewer.addSearchImprovement(
                            QString::fromLocal8Bit(argv[1]),
                            QString::fromLocal8Bit(argv[2]),
                            secondImprovement,
                            QStringLiteral("optimized-cpu"),
                            9u,
                            2u);
                    QCoreApplication::processEvents();
                    QCoreApplication::processEvents();
                    const QVariantList improvementPaths =
                            viewer.trajectoryPaths();
                    const bool improvementTrajectoryUiValid =
                            viewer.trajectoryCount() == 3 &&
                            improvementPaths.size() == 3 &&
                            improvementPaths.at(1)
                                            .toMap()
                                            .value(QStringLiteral("name"))
                                            .toString() ==
                                    QStringLiteral("Improvement 1") &&
                            improvementPaths.at(1)
                                            .toMap()
                                            .value(QStringLiteral("opacity"))
                                            .toDouble() < 0.31 &&
                            improvementPaths.at(2)
                                            .toMap()
                                            .value(QStringLiteral("name"))
                                            .toString() ==
                                    QStringLiteral("Improvement 2") &&
                            improvementPaths.at(2)
                                            .toMap()
                                            .value(QStringLiteral("opacity"))
                                            .toDouble() > 0.95;

                    QTimer::singleShot(
                            250, &application,
                            [&, filled, wire, quickWindow, runSelector,
                             renderModeSelector, gpuRayTracingView,
                             rasterMapView, viewCamera,
                             mapEnvironment, daySkyTexture, mainMapLight,
                             fillMapLight, bestPosition,
                             baseInputScriptTextArea,
                             copyCurrentRaceInputsButton,
                             rayTracingTrajectoryOverlay,
                             trajectoryPreviewUiValid,
                             improvementTrajectoryUiValid]() {
                                QCoreApplication::sendPostedEvents(
                                        nullptr, QEvent::DeferredDelete);
                                const QList<QObject *> carRoots =
                                        root->findChildren<QObject *>(
                                                QStringLiteral("runCarRoot"));
                                const QList<QObject *> carFilledModels =
                                        root->findChildren<QObject *>(
                                                QStringLiteral(
                                                        "runCarFilledModel"));
                                const QList<QObject *> carFilledMaterials =
                                        root->findChildren<QObject *>(
                                                QStringLiteral(
                                                        "runCarFilledMaterial"));
                                const QList<QObject *> carWireModels =
                                        root->findChildren<QObject *>(
                                                QStringLiteral(
                                                        "runCarWireModel"));
                                const QList<QObject *> allTrajectoryModels =
                                        root->findChildren<QObject *>(
                                                QStringLiteral(
                                                        "trajectoryPathModel"));
                                const QList<QObject *>
                                        allRayTracingTrajectoryModels =
                                                root->findChildren<QObject *>(
                                                        QStringLiteral(
                                                                "rayTracingTrajectoryPathModel"));
                                const bool allTrajectoryModelsRendered =
                                        allTrajectoryModels.size() ==
                                                viewer.trajectoryCount() &&
                                        allRayTracingTrajectoryModels.size() ==
                                                viewer.trajectoryCount();
                                const QList<QObject *> visualModels =
                                        root->findChildren<QObject *>(
                                                QStringLiteral(
                                                        "trackVisualModel"));
                                const QList<QObject *> visualMaterials =
                                        root->findChildren<QObject *>(
                                                QStringLiteral(
                                                        "trackVisualMaterial"));
                                const QList<QObject *> visualBaseTextures =
                                        root->findChildren<QObject *>(
                                                QStringLiteral(
                                                        "trackVisualBaseTexture"));
                                const QList<QObject *> visualNormalTextures =
                                        root->findChildren<QObject *>(
                                                QStringLiteral(
                                                        "trackVisualNormalTexture"));
                                const auto materialState =
                                        [](const QObject *material) {
                                            return QVariantList{
                                                    material->property(
                                                            "baseColor"),
                                                    material->property(
                                                            "baseColorMap"),
                                                    material->property(
                                                            "normalMap"),
                                                    material->property(
                                                            "roughness"),
                                                    material->property(
                                                            "metalness"),
                                                    material->property(
                                                            "opacity"),
                                                    material->property(
                                                            "cullMode")};
                                        };
                                std::vector<QVariantList>
                                        materialStatesBeforeThemeChange;
                                materialStatesBeforeThemeChange.reserve(
                                        static_cast<std::size_t>(
                                                visualMaterials.size()));
                                for (const QObject *material :
                                     visualMaterials) {
                                    materialStatesBeforeThemeChange.push_back(
                                            materialState(material));
                                }
                                const QVariantList sceneStateBeforeThemeChange{
                                        filled->property("geometry"),
                                        wire->property("geometry"),
                                        mapEnvironment->property(
                                                "clearColor"),
                                        mapEnvironment->property(
                                                "lightProbe"),
                                        mapEnvironment->property(
                                                "backgroundMode"),
                                        mainMapLight->property("color"),
                                        mainMapLight->property("brightness"),
                                        fillMapLight->property("color"),
                                        fillMapLight->property("brightness"),
                                        viewCamera->property("fieldOfView"),
                                        viewCamera->property("clipNear"),
                                        viewCamera->property("clipFar")};
                                controller.setDarkMode(true);
                                QCoreApplication::processEvents();
                                bool loadedSceneThemeInvariant =
                                        sceneStateBeforeThemeChange ==
                                                QVariantList{
                                                        filled->property(
                                                                "geometry"),
                                                        wire->property(
                                                                "geometry"),
                                                        mapEnvironment
                                                                ->property(
                                                                        "clearColor"),
                                                        mapEnvironment
                                                                ->property(
                                                                        "lightProbe"),
                                                        mapEnvironment
                                                                ->property(
                                                                        "backgroundMode"),
                                                        mainMapLight->property(
                                                                "color"),
                                                        mainMapLight->property(
                                                                "brightness"),
                                                        fillMapLight->property(
                                                                "color"),
                                                        fillMapLight->property(
                                                                "brightness"),
                                                        viewCamera->property(
                                                                "fieldOfView"),
                                                        viewCamera->property(
                                                                "clipNear"),
                                                        viewCamera->property(
                                                                "clipFar")};
                                for (qsizetype index = 0;
                                     index < visualMaterials.size();
                                     ++index) {
                                    loadedSceneThemeInvariant &=
                                            materialStatesBeforeThemeChange
                                                    .at(static_cast<
                                                        std::size_t>(index)) ==
                                            materialState(
                                                    visualMaterials.at(index));
                                }
                                controller.setDarkMode(false);
                                QCoreApplication::processEvents();
                                const int expectedCarModels =
                                        static_cast<int>(
                                                viewer.ellipsoidCount() *
                                                viewer.runCount());
                                bool rootsVisible =
                                        carRoots.size() ==
                                        viewer.runCount();
                                for (const QObject *rootNode : carRoots) {
                                    rootsVisible &= rootNode
                                                            ->property("visible")
                                                            .toBool() &&
                                            std::abs(rootNode
                                                             ->property("opacity")
                                                             .toReal() -
                                                     1.0) < 0.001;
                                }

                                const QVariant filledGeometry =
                                        filled->property("geometry");
                                const QVariant wireGeometry =
                                        wire->property("geometry");
                                const bool geometryAttached =
                                        filledGeometry.isValid() &&
                                        !filledGeometry.isNull() &&
                                        wireGeometry.isValid() &&
                                        !wireGeometry.isNull();
                                const int initialVisibleVisualModels =
                                        VisibleModelCount(visualModels);
                                const bool rayTracingSupported =
                                        gpuRayTracingView != nullptr &&
                                        gpuRayTracingView
                                                ->property("supported")
                                                .toBool();
                                const int wireframeIndex =
                                        rayTracingSupported ? 4 : 3;
                                const int highContrastIndex =
                                        rayTracingSupported ? 5 : 4;
                                bool renderModeOptionsValid =
                                        renderModeSelector != nullptr &&
                                        renderModeSelector->property("count")
                                                        .toInt() ==
                                                (rayTracingSupported ? 6 : 5);
                                if (renderModeOptionsValid) {
                                    if (rayTracingSupported) {
                                        renderModeSelector->setProperty(
                                                "currentIndex", 1);
                                        renderModeOptionsValid =
                                                renderModeSelector
                                                                ->property(
                                                                        "currentValue")
                                                                .toString() ==
                                                        QStringLiteral(
                                                                "textured-rt") &&
                                                renderModeSelector
                                                                ->property(
                                                                        "displayText")
                                                                .toString() ==
                                                        QStringLiteral(
                                                                "Textured (RT)");
                                    }
                                    renderModeSelector->setProperty(
                                            "currentIndex", wireframeIndex);
                                    renderModeOptionsValid &=
                                            renderModeSelector
                                                    ->property("currentValue")
                                                    .toString() ==
                                                    QStringLiteral(
                                                            "wireframe") &&
                                            renderModeSelector
                                                    ->property("displayText")
                                                    .toString() ==
                                                    QStringLiteral(
                                                            "Wireframe");
                                    renderModeSelector->setProperty(
                                            "currentIndex",
                                            highContrastIndex);
                                    renderModeOptionsValid &=
                                            renderModeSelector
                                                    ->property("currentValue")
                                                    .toString() ==
                                                    QStringLiteral(
                                                            "material-debug") &&
                                            renderModeSelector
                                                    ->property("displayText")
                                                    .toString() ==
                                                    QStringLiteral(
                                                            "High Contrast");
                                    renderModeSelector->setProperty(
                                            "currentIndex", 0);
                                }
                                const bool initialModelState =
                                        !filled->property("visible").toBool() &&
                                        !wire->property("visible").toBool() &&
                                        renderModeOptionsValid &&
                                        renderModeSelector
                                                        ->property("currentValue")
                                                        .toString() ==
                                                QStringLiteral("textured") &&
                                        visualModels.size() ==
                                                viewer.visualInstances().size() &&
                                        viewer.visualTriangleCount() > 0 &&
                                        viewer.visualMeshCount() > 0 &&
                                        viewer.materialCount() > 0 &&
                                        initialVisibleVisualModels > 0 &&
                                        ModelsHaveGeometry(
                                                visualModels,
                                                visualModels.size()) &&
                                        VisualMaterialsAreBoundAndShared(
                                                visualModels,
                                                visualMaterials,
                                                visualBaseTextures,
                                                visualNormalTextures,
                                                viewer) &&
                                        ModelsHaveState(carFilledModels,
                                                        expectedCarModels,
                                                        true) &&
                                        FilledModelsHaveBakedRunPalettes(
                                                carFilledModels,
                                                carFilledMaterials,
                                                expectedCarModels) &&
                                        ModelsHaveState(carWireModels,
                                                        expectedCarModels,
                                                        false);
                                bool rayTracingModeValid =
                                        gpuRayTracingView != nullptr &&
                                        rasterMapView != nullptr &&
                                        rayTracingTrajectoryOverlay != nullptr &&
                                        !gpuRayTracingView
                                                 ->property("visible")
                                                 .toBool() &&
                                        !gpuRayTracingView
                                                 ->property("active")
                                                 .toBool() &&
                                        !gpuRayTracingView
                                                 ->property("status")
                                                 .toString()
                                                 .isEmpty();
                                if (rayTracingSupported) {
                                    root->setProperty(
                                            "renderMode",
                                            QStringLiteral("textured-rt"));
                                    QCoreApplication::processEvents();
                                    rayTracingModeValid &=
                                            root->property(
                                                        "rayTracingEnabled")
                                                            .toBool() &&
                                            gpuRayTracingView
                                                    ->property("visible")
                                                    .toBool() &&
                                            gpuRayTracingView
                                                    ->property("active")
                                                    .toBool() &&
                                            rayTracingTrajectoryOverlay
                                                    ->property("visible")
                                                    .toBool() &&
                                            !rasterMapView
                                                     ->property("visible")
                                                     .toBool();
                                    root->setProperty(
                                            "renderMode",
                                            QStringLiteral("textured"));
                                    QCoreApplication::processEvents();
                                    rayTracingModeValid &=
                                            !root->property(
                                                         "rayTracingEnabled")
                                                     .toBool() &&
                                            !gpuRayTracingView
                                                     ->property("visible")
                                                     .toBool() &&
                                            !gpuRayTracingView
                                                     ->property("active")
                                                     .toBool() &&
                                            !rayTracingTrajectoryOverlay
                                                     ->property("visible")
                                                     .toBool() &&
                                            rasterMapView
                                                    ->property("visible")
                                                    .toBool();
                                }
                                const bool optimizedRenderState =
                                        viewCamera != nullptr &&
                                        viewCamera->property("clipNear")
                                                        .toDouble() >= 0.05 &&
                                        viewCamera->property("clipFar")
                                                        .toDouble() >
                                                viewCamera->property("clipNear")
                                                        .toDouble() &&
                                        viewCamera->property("clipFar")
                                                                .toDouble() /
                                                        viewCamera
                                                                ->property(
                                                                        "clipNe"
                                                                        "ar")
                                                                .toDouble() <=
                                                50001.0 &&
                                        mainMapLight != nullptr &&
                                        !mainMapLight->property("castsShadow")
                                                 .toBool() &&
                                        std::all_of(
                                                visualModels.cbegin(),
                                                visualModels.cend(),
                                                [](const QObject *model) {
                                                    return !model->property(
                                                                         "casts"
                                                                         "Shado"
                                                                         "ws")
                                                                    .toBool();
                                                });
                                const QUrl skySource =
                                        daySkyTexture != nullptr
                                        ? daySkyTexture->property("source")
                                                  .toUrl()
                                        : QUrl();
                                const bool daylightEnvironment =
                                        mapEnvironment != nullptr &&
                                        daySkyTexture != nullptr &&
                                        mainMapLight != nullptr &&
                                        fillMapLight != nullptr &&
                                        mapEnvironment
                                                        ->property(
                                                                "probeExposure")
                                                        .toDouble() >=
                                                0.8 &&
                                        mapEnvironment
                                                        ->property(
                                                                "skyboxBlur"
                                                                "Amount")
                                                        .toDouble() ==
                                                0.0 &&
                                        skySource.scheme() ==
                                                QStringLiteral("qrc") &&
                                        skySource.path() ==
                                                QStringLiteral(
                                                        "/environment/"
                                                        "day_sky.png") &&
                                        mainMapLight
                                                        ->property("brightness")
                                                        .toDouble() >=
                                                1.0 &&
                                        fillMapLight
                                                        ->property("brightness")
                                                        .toDouble() >
                                                0.0;

                                const bool bestSelectedInitially =
                                        viewer.runCount() == 2 &&
                                        viewer.runOptions().size() == 2 &&
                                        viewer.runPoses().size() == 2 &&
                                        viewer.selectedRunId() ==
                                                QStringLiteral("best") &&
                                        viewer.tickCount() == 3 &&
                                        (viewer.carPosition() - bestPosition)
                                                        .length() < 0.001f &&
                                        runSelector != nullptr &&
                                        runSelector->property("count").toInt() ==
                                                2 &&
                                        runSelector
                                                        ->property("currentValue")
                                                        .toString() ==
                                                QStringLiteral("best") &&
                                        runSelector
                                                        ->property("displayText")
                                                        .toString() ==
                                                QStringLiteral("Best");

                                viewer.setTimeMs(10);
                                controller.setBaseInputScript(
                                        QStringLiteral(
                                                "0.00 press down"));
                                QCoreApplication::processEvents();
                                const bool copyInvoked =
                                        copyCurrentRaceInputsButton != nullptr &&
                                        copyCurrentRaceInputsButton
                                                ->property("enabled").toBool() &&
                                        QMetaObject::invokeMethod(
                                                copyCurrentRaceInputsButton,
                                                "clicked",
                                                Qt::DirectConnection);
                                QCoreApplication::processEvents();
                                const bool copyCurrentRaceInputsValid =
                                        copyInvoked &&
                                        controller.baseInputScript() ==
                                                QStringLiteral(
                                                        "0.00 press up") &&
                                        baseInputScriptTextArea != nullptr &&
                                        baseInputScriptTextArea
                                                        ->property("text")
                                                        .toString() ==
                                                controller.baseInputScript();
                                viewer.jumpToStart();
                                QCoreApplication::processEvents();

                                const auto activateRun =
                                        [runSelector](int index) {
                                            return runSelector != nullptr &&
                                                    QMetaObject::invokeMethod(
                                                            runSelector,
                                                            "activated",
                                                            Qt::DirectConnection,
                                                            Q_ARG(int, index));
                                        };
                                const bool bestActivated = activateRun(1);
                                QCoreApplication::processEvents();
                                QCoreApplication::processEvents();
                                const bool onlyBestSelected =
                                        bestActivated &&
                                        viewer.selectedRunId() ==
                                                QStringLiteral("best") &&
                                        viewer.tickCount() == 3 &&
                                        (viewer.carPosition() - bestPosition)
                                                .length() < 0.001f;

                                root->setProperty(
                                        "renderMode",
                                        QStringLiteral("neutral"));
                                QCoreApplication::processEvents();
                                const bool neutralModeState =
                                        !filled->property("visible").toBool() &&
                                        !wire->property("visible").toBool() &&
                                        VisibleModelCount(visualModels) ==
                                                initialVisibleVisualModels &&
                                        std::all_of(
                                                visualMaterials.cbegin(),
                                                visualMaterials.cend(),
                                                [](const QObject *material) {
                                                    return material
                                                            ->property(
                                                                    "baseColorMap")
                                                            .value<QObject *>() ==
                                                            nullptr &&
                                                            material
                                                                    ->property(
                                                                            "normalMap")
                                                                    .value<QObject *>() ==
                                                            nullptr;
                                                });
                                root->setProperty(
                                        "renderMode",
                                        QStringLiteral("collision"));
                                QCoreApplication::processEvents();
                                const bool collisionModeState =
                                        filled->property("visible").toBool() &&
                                        !wire->property("visible").toBool() &&
                                        ModelsHaveState(
                                                visualModels,
                                                visualModels.size(),
                                                false);
                                root->setProperty(
                                        "renderMode",
                                        QStringLiteral("material-debug"));
                                QCoreApplication::processEvents();
                                const bool materialDebugState =
                                        !filled->property("visible").toBool() &&
                                        !wire->property("visible").toBool() &&
                                        VisibleModelCount(visualModels) ==
                                                initialVisibleVisualModels;
                                root->setProperty(
                                        "renderMode",
                                        QStringLiteral("wireframe"));
                                QCoreApplication::processEvents();
                                const bool wireframeState =
                                        !filled->property("visible").toBool() &&
                                        wire->property("visible").toBool() &&
                                        ModelsHaveState(
                                                visualModels,
                                                visualModels.size(),
                                                false) &&
                                        ModelsHaveState(carFilledModels,
                                                        expectedCarModels,
                                                        false) &&
                                        ModelsHaveState(carWireModels,
                                                        expectedCarModels,
                                                        true);
                                root->setProperty(
                                        "renderMode",
                                        QStringLiteral("textured"));
                                QCoreApplication::processEvents();
                                const bool restoredState =
                                        !filled->property("visible").toBool() &&
                                        !wire->property("visible").toBool() &&
                                        VisibleModelCount(visualModels) ==
                                                initialVisibleVisualModels &&
                                        ModelsHaveState(carFilledModels,
                                                        expectedCarModels,
                                                        true) &&
                                        ModelsHaveState(carWireModels,
                                                        expectedCarModels,
                                                        false);

                                auto *const whiteboardOverlay =
                                        qobject_cast<QQuickItem *>(
                                                root->findChild<QObject *>(
                                                        QStringLiteral(
                                                                "whiteboardOverlay")));
                                auto *const whiteboardViewport =
                                        qobject_cast<QQuickItem *>(
                                                root->findChild<QObject *>(
                                                        QStringLiteral(
                                                                "raceViewport")));
                                auto *const whiteboardToolbar =
                                        qobject_cast<QQuickItem *>(
                                                root->findChild<QObject *>(
                                                        QStringLiteral(
                                                                "whiteboardToolbar")));
                                QObject *const whiteboardModeToggle =
                                        root->findChild<QObject *>(
                                                QStringLiteral(
                                                        "whiteboardModeToggle"));
                                QObject *const whiteboardModeToggleLabel =
                                        root->findChild<QObject *>(
                                                QStringLiteral(
                                                        "whiteboardModeToggleLabel"));
                                QObject *const whiteboardDrawingInput =
                                        root->findChild<QObject *>(
                                                QStringLiteral(
                                                        "whiteboardDrawingInput"));
                                QObject *const whiteboardDrawingRepeater =
                                        root->findChild<QObject *>(
                                                QStringLiteral(
                                                        "whiteboardDrawingRepeater"));
                                QObject *const whiteboardPlaneRepeater =
                                        root->findChild<QObject *>(
                                                QStringLiteral(
                                                        "whiteboardPlaneRepeater"));
                                auto *const whiteboardPlaneView =
                                        qobject_cast<QQuickItem *>(
                                                root->findChild<QObject *>(
                                                        QStringLiteral(
                                                                "whiteboardPlaneView")));
                                QObject *const whiteboardDrawingList =
                                        root->findChild<QObject *>(
                                                QStringLiteral(
                                                        "whiteboardDrawingList"));
                                auto *const whiteboard =
                                        viewer.whiteboard();
                                whiteboard->setActive(true);
                                whiteboard->setTool(
                                        QStringLiteral("line"));
                                const bool whiteboardLineAdded =
                                        whiteboard->beginItem(0.15, 0.2) &&
                                        whiteboard->updateItem(0.7, 0.6) &&
                                        whiteboard->finishItem();
                                whiteboard->setTool(
                                        QStringLiteral("text"));
                                const bool whiteboardTextAdded =
                                        whiteboard->addText(
                                                0.24,
                                                0.3,
                                                QStringLiteral(
                                                        "Apex note")) == 1;
                                QCoreApplication::processEvents();
                                const QColor lightWhiteboardToolText =
                                        whiteboardOverlay != nullptr
                                        ? whiteboardOverlay
                                                  ->property(
                                                          "toolbarControlText")
                                                  .value<QColor>()
                                        : QColor();
                                controller.setDarkMode(true);
                                QCoreApplication::processEvents();
                                const QColor darkWhiteboardToolText =
                                        whiteboardOverlay != nullptr
                                        ? whiteboardOverlay
                                                  ->property(
                                                          "toolbarControlText")
                                                  .value<QColor>()
                                        : QColor();
                                controller.setDarkMode(false);
                                QCoreApplication::processEvents();
                                const bool whiteboardToolThemeContrast =
                                        whiteboardOverlay != nullptr &&
                                        lightWhiteboardToolText ==
                                                QColor(QStringLiteral(
                                                        "#202421")) &&
                                        darkWhiteboardToolText ==
                                                QColor(QStringLiteral(
                                                        "#f0f3ef")) &&
                                        whiteboardOverlay
                                                        ->property(
                                                                "toolbarControlText")
                                                        .value<QColor>() ==
                                                lightWhiteboardToolText;
                                const bool whiteboardActiveState =
                                        whiteboardOverlay != nullptr &&
                                        whiteboardToolbar != nullptr &&
                                        whiteboardModeToggle != nullptr &&
                                        whiteboardModeToggle
                                                ->property("checked")
                                                .toBool() &&
                                        whiteboardDrawingInput != nullptr &&
                                        whiteboardDrawingInput
                                                ->property("enabled")
                                                .toBool() &&
                                        whiteboardLineAdded &&
                                        whiteboardTextAdded &&
                                        whiteboard->count() == 2 &&
                                        whiteboardDrawingRepeater != nullptr &&
                                        whiteboardDrawingRepeater
                                                        ->property("count")
                                                        .toInt() == 2 &&
                                        VisibleModelCount(visualModels) ==
                                                initialVisibleVisualModels;
                                const QVector3D whiteboardTarget =
                                        whiteboardViewport != nullptr
                                        ? whiteboardViewport
                                                  ->property("cameraTarget")
                                                  .value<QVector3D>()
                                        : QVector3D();
                                const QVariantMap whiteboardCapture{
                                        {QStringLiteral("targetX"),
                                         whiteboardTarget.x()},
                                        {QStringLiteral("targetY"),
                                         whiteboardTarget.y()},
                                        {QStringLiteral("targetZ"),
                                         whiteboardTarget.z()},
                                        {QStringLiteral("yaw"),
                                         whiteboardViewport != nullptr
                                         ? whiteboardViewport
                                                 ->property("orbitYaw")
                                                 .toDouble()
                                         : 0.0},
                                        {QStringLiteral("pitch"),
                                         whiteboardViewport != nullptr
                                         ? whiteboardViewport
                                                 ->property("orbitPitch")
                                                 .toDouble()
                                         : 0.0},
                                        {QStringLiteral("distance"),
                                         whiteboardViewport != nullptr
                                         ? whiteboardViewport
                                                 ->property(
                                                         "orbitDistance")
                                                 .toDouble()
                                         : 0.0},
                                        {QStringLiteral("planeX"),
                                         whiteboardTarget.x()},
                                        {QStringLiteral("planeY"),
                                         whiteboardTarget.y()},
                                        {QStringLiteral("planeZ"),
                                         whiteboardTarget.z()},
                                        {QStringLiteral("planeWidth"), 12.0},
                                        {QStringLiteral("planeHeight"), 7.0}};
                                const bool whiteboardPlaced =
                                        whiteboardViewport != nullptr &&
                                        whiteboard->captureCurrentBoard(
                                                QStringLiteral(
                                                        "Smoke drawing"),
                                                whiteboardCapture) == 0;
                                QCoreApplication::processEvents();
                                QVariant pickedWhiteboard;
                                const bool whiteboardWorldPick =
                                        whiteboardPlaneView != nullptr &&
                                        QMetaObject::invokeMethod(
                                                whiteboardPlaneView,
                                                "pickBoard",
                                                Q_RETURN_ARG(
                                                        QVariant,
                                                        pickedWhiteboard),
                                                Q_ARG(
                                                        QVariant,
                                                        QVariant(
                                                                whiteboardPlaneView
                                                                        ->width()
                                                                * 0.5)),
                                                Q_ARG(
                                                        QVariant,
                                                        QVariant(
                                                                whiteboardPlaneView
                                                                        ->height()
                                                                * 0.5))) &&
                                        pickedWhiteboard.toInt() == 0;
                                const QVariantMap placedBoard =
                                        whiteboard->boards()
                                                .value(0)
                                                .toMap();
                                const QString planeObjectName =
                                        QStringLiteral("whiteboardPlane_")
                                        + placedBoard
                                                  .value(
                                                          QStringLiteral(
                                                                  "id"))
                                                  .toString();
                                QObject *const placedPlane =
                                        root->findChild<QObject *>(
                                                planeObjectName);
                                const bool whiteboardPlaneState =
                                        whiteboardPlaced &&
                                        whiteboardWorldPick &&
                                        whiteboard->count() == 0 &&
                                        whiteboard->boardCount() == 1 &&
                                        whiteboardPlaneRepeater != nullptr &&
                                        whiteboardPlaneRepeater
                                                        ->property("count")
                                                        .toInt() == 1 &&
                                        whiteboardDrawingRepeater
                                                        ->property("count")
                                                        .toInt() == 0 &&
                                        whiteboardDrawingList != nullptr &&
                                        placedPlane != nullptr &&
                                        whiteboard->setBoardVisible(0, false);
                                QCoreApplication::processEvents();
                                const bool planeInactiveWhenHidden =
                                        whiteboard->visibleBoards().isEmpty();
                                const int hiddenRepeaterCount =
                                        whiteboardPlaneRepeater
                                                ->property("count")
                                                .toInt();
                                const bool hiddenRole =
                                        !whiteboard->boards()
                                                 .value(0)
                                                 .toMap()
                                                 .value(
                                                         QStringLiteral(
                                                                 "visible"))
                                                 .toBool();
                                const bool whiteboardHiddenState =
                                        whiteboardPlaneState &&
                                        planeInactiveWhenHidden &&
                                        hiddenRepeaterCount == 0 &&
                                        hiddenRole &&
                                        whiteboard->boardCount() == 1;
                                const bool whiteboardShownAgain =
                                        whiteboard->setBoardVisible(0, true);
                                QCoreApplication::processEvents();
                                whiteboard->setActive(false);
                                QCoreApplication::processEvents();
                                const QColor lightWhiteboardModeText =
                                        whiteboardModeToggleLabel != nullptr
                                        ? whiteboardModeToggleLabel
                                                  ->property("color")
                                                  .value<QColor>()
                                        : QColor();
                                controller.setDarkMode(true);
                                QCoreApplication::processEvents();
                                const QColor darkWhiteboardModeText =
                                        whiteboardModeToggleLabel != nullptr
                                        ? whiteboardModeToggleLabel
                                                  ->property("color")
                                                  .value<QColor>()
                                        : QColor();
                                controller.setDarkMode(false);
                                QCoreApplication::processEvents();
                                const bool whiteboardModeThemeContrast =
                                        whiteboardModeToggleLabel != nullptr &&
                                        lightWhiteboardModeText ==
                                                QColor(QStringLiteral(
                                                        "#202421")) &&
                                        darkWhiteboardModeText ==
                                                QColor(QStringLiteral(
                                                        "#f0f3ef")) &&
                                        whiteboardModeToggleLabel
                                                        ->property("color")
                                                        .value<QColor>() ==
                                                lightWhiteboardModeText;
                                const bool whiteboardIntegrated =
                                        whiteboardActiveState &&
                                        whiteboardToolThemeContrast &&
                                        whiteboardModeThemeContrast &&
                                        whiteboardHiddenState &&
                                        whiteboardShownAgain &&
                                        whiteboardPlaneRepeater
                                                        ->property("count")
                                                        .toInt() == 1 &&
                                        !whiteboardModeToggle
                                                 ->property("checked")
                                                 .toBool() &&
                                        !whiteboardDrawingInput
                                                 ->property("enabled")
                                                 .toBool() &&
                                        whiteboardToolbar->width() <= 198.1 &&
                                        whiteboard->count() == 0 &&
                                        whiteboard->boardCount() == 1 &&
                                        whiteboardDrawingRepeater
                                                        ->property("count")
                                                        .toInt() == 0 &&
                                        VisibleModelCount(visualModels) ==
                                                initialVisibleVisualModels;

                                QTemporaryDir imageExportDirectory;
                                const QString backgroundImagePath =
                                        imageExportDirectory.filePath(
                                                QStringLiteral(
                                                        "board-background.png"));
                                QVariant backgroundExportStarted;
                                const bool backgroundExportInvoked =
                                        imageExportDirectory.isValid() &&
                                        whiteboardViewport != nullptr &&
                                        whiteboard->setBoardVisible(0, false) &&
                                        QMetaObject::invokeMethod(
                                                whiteboardViewport,
                                                "exportWhiteboardBackground",
                                                Q_RETURN_ARG(
                                                        QVariant,
                                                        backgroundExportStarted),
                                                Q_ARG(QVariant, QVariant(0)),
                                                Q_ARG(
                                                        QVariant,
                                                                QVariant(
                                                                        QUrl::fromLocalFile(
                                                                        backgroundImagePath))));
                                const bool exportStartedState =
                                        backgroundExportStarted.toBool();
                                const bool exportBusyState =
                                        whiteboardViewport
                                                ->property(
                                                        "exportingWhiteboardImage")
                                                .toBool();
                                auto *const exportOverlay =
                                        qobject_cast<QQuickItem *>(
                                                root->findChild<QObject *>(
                                                        QStringLiteral(
                                                                "whiteboardOverlay")));
                                auto *const exportHeader =
                                        qobject_cast<QQuickItem *>(
                                                root->findChild<QObject *>(
                                                        QStringLiteral(
                                                                "raceViewerHeader")));
                                auto *const exportDock =
                                        qobject_cast<QQuickItem *>(
                                                root->findChild<QObject *>(
                                                        QStringLiteral(
                                                                "playbackDock")));
                                const bool exportOverlayHidden =
                                        exportOverlay != nullptr &&
                                        !exportOverlay->isVisible();
                                const bool exportHeaderHidden =
                                        exportHeader != nullptr &&
                                        !exportHeader->isVisible();
                                const bool exportDockHidden =
                                        exportDock != nullptr &&
                                        !exportDock->isVisible();
                                const bool exportPlaneMode =
                                        whiteboardPlaneView != nullptr &&
                                        whiteboardPlaneView
                                                ->property("exportMode")
                                                .toBool();
                                const bool exportForcedPlane =
                                        whiteboardPlaneRepeater != nullptr &&
                                        whiteboardPlaneRepeater
                                                        ->property("count")
                                                        .toInt() == 1;
                                const bool exportCaptureState =
                                        backgroundExportInvoked &&
                                        exportStartedState &&
                                        exportBusyState &&
                                        exportOverlayHidden &&
                                        exportHeaderHidden &&
                                        exportDockHidden &&
                                        exportPlaneMode &&
                                        exportForcedPlane;
                                QEventLoop imageExportLoop;
                                QTimer::singleShot(
                                        800,
                                        &imageExportLoop,
                                        &QEventLoop::quit);
                                imageExportLoop.exec();
                                const QImage backgroundImage(
                                        backgroundImagePath);
                                auto *const postExportViewport =
                                        qobject_cast<QQuickItem *>(
                                                root->findChild<QObject *>(
                                                        QStringLiteral(
                                                                "raceViewport")));
                                auto *const postExportPlaneView =
                                        qobject_cast<QQuickItem *>(
                                                root->findChild<QObject *>(
                                                        QStringLiteral(
                                                                "whiteboardPlaneView")));
                                const bool fullBackgroundExportValid =
                                        exportCaptureState &&
                                        postExportViewport != nullptr &&
                                        !postExportViewport
                                                 ->property(
                                                         "exportingWhiteboardImage")
                                                 .toBool() &&
                                        postExportPlaneView != nullptr &&
                                        !postExportPlaneView
                                                 ->property("exportMode")
                                                 .toBool() &&
                                        postExportPlaneView
                                                        ->property(
                                                                "forcedBoardIndex")
                                                        .toInt() == -1 &&
                                        QFileInfo(backgroundImagePath).size() >
                                                0 &&
                                        !backgroundImage.isNull() &&
                                        backgroundImage.width() ==
                                                qRound(
                                                        postExportViewport
                                                                ->width()) &&
                                        backgroundImage.height() ==
                                                qRound(
                                                        postExportViewport
                                                                ->height()) &&
                                        whiteboard->operationMessage().contains(
                                                QStringLiteral(
                                                        "with background exported"));
                                const bool whiteboardVisibilityRestored =
                                        whiteboard->setBoardVisible(0, true);

                                completed = true;
                                exitCode =
                                        geometryAttached && rootsVisible &&
                                                        initialModelState &&
                                                        bestSelectedInitially &&
                                                        onlyBestSelected &&
                                                        neutralModeState &&
                                                        collisionModeState &&
                                                        materialDebugState &&
                                                        wireframeState &&
                                                        restoredState &&
                                                        rayTracingModeValid &&
                                                        optimizedRenderState &&
                                                        daylightEnvironment &&
                                                        loadedSceneThemeInvariant &&
                                                        trajectoryPreviewUiValid &&
                                                        improvementTrajectoryUiValid &&
                                                        allTrajectoryModelsRendered &&
                                                        copyCurrentRaceInputsValid &&
                                                        editorStructure &&
                                                        whiteboardIntegrated &&
                                                        fullBackgroundExportValid &&
                                                        whiteboardVisibilityRestored
                                                ? 0
                                                : 1;
                                if (exitCode != 0) {
                                    std::cerr
                                            << "viewer run switching failed: "
                                               "geometry="
                                            << geometryAttached
                                            << ", roots=" << carRoots.size()
                                            << ", filledModels="
                                            << carFilledModels.size()
                                            << ", filledMaterials="
                                            << carFilledMaterials.size()
                                            << ", wireModels="
                                            << carWireModels.size()
                                            << ", visualModels="
                                            << visualModels.size()
                                            << ", visualInstances="
                                            << viewer.visualInstances().size()
                                            << ", visualTriangles="
                                            << viewer.visualTriangleCount()
                                            << ", visualMeshes="
                                            << viewer.visualMeshCount()
                                            << ", materials="
                                            << viewer.materialCount()
                                            << ", expectedModels="
                                            << expectedCarModels
                                            << ", initial=" << initialModelState
                                            << ", bestInitial="
                                            << bestSelectedInitially
                                            << ", onlyBestSelected="
                                            << onlyBestSelected
                                            << ", copyCurrentRaceInputs="
                                            << copyCurrentRaceInputsValid
                                            << ", trajectoryPreview="
                                            << trajectoryPreviewUiValid
                                            << "/"
                                            << viewer.trajectoryCount()
                                            << "/"
                                            << improvementTrajectoryUiValid
                                            << "/"
                                            << allTrajectoryModelsRendered
                                            << "/"
                                            << allTrajectoryModels.size()
                                            << "/"
                                            << allRayTracingTrajectoryModels
                                                       .size()
                                            << "/"
                                            << (copyCurrentRaceInputsButton !=
                                                                nullptr
                                                        ? copyCurrentRaceInputsButton
                                                                  ->property(
                                                                          "enabled")
                                                                  .toBool()
                                                        : false)
                                            << " script='"
                                            << controller.baseInputScript()
                                                       .toStdString()
                                            << "'"
                                            << ", collisionMode="
                                            << collisionModeState
                                            << ", neutralMode="
                                            << neutralModeState
                                            << ", materialDebug="
                                            << materialDebugState
                                            << ", wireframe=" << wireframeState
                                            << ", restored=" << restoredState
                                            << ", whiteboard="
                                            << whiteboardActiveState << "/"
                                            << whiteboardIntegrated << "/"
                                            << whiteboardToolThemeContrast << "/"
                                            << whiteboardModeThemeContrast << "/"
                                            << lightWhiteboardToolText
                                                       .name()
                                                       .toStdString()
                                            << "/"
                                            << darkWhiteboardToolText
                                                       .name()
                                                       .toStdString()
                                            << "/"
                                            << whiteboard->count()
                                            << "/placed="
                                            << whiteboardPlaced
                                            << "/plane="
                                            << whiteboardPlaneState
                                            << "/worldPick="
                                            << whiteboardWorldPick
                                            << "/hidden="
                                            << whiteboardHiddenState
                                            << "/boards="
                                            << whiteboard->boardCount()
                                            << "/repeater="
                                            << (whiteboardPlaneRepeater
                                                        ? whiteboardPlaneRepeater
                                                                  ->property(
                                                                          "count")
                                                                  .toInt()
                                                        : -1)
                                            << "/planeObject="
                                            << (placedPlane != nullptr)
                                            << "/hiddenObject="
                                            << planeInactiveWhenHidden
                                            << "/hiddenRepeater="
                                            << hiddenRepeaterCount
                                            << "/hiddenRole="
                                            << hiddenRole
                                            << "/modelVisible="
                                            << whiteboard->boards()
                                                       .value(0)
                                                       .toMap()
                                                       .value(
                                                               QStringLiteral(
                                                                       "visible"))
                                                       .toBool()
                                            << "/shownAgain="
                                            << whiteboardShownAgain
                                            << "/imageExport="
                                            << fullBackgroundExportValid
                                            << "/captureState="
                                            << exportCaptureState
                                            << "/file="
                                            << QFileInfo(
                                                       backgroundImagePath)
                                                       .size()
                                            << "/captureBits="
                                            << backgroundExportInvoked << "/"
                                            << exportStartedState << "/"
                                            << exportBusyState << "/"
                                            << exportOverlayHidden << "/"
                                            << exportHeaderHidden << "/"
                                            << exportDockHidden << "/"
                                            << exportPlaneMode << "/"
                                            << exportForcedPlane
                                            << "/image="
                                            << backgroundImage.width() << "x"
                                            << backgroundImage.height()
                                            << "/viewport="
                                            << (postExportViewport
                                                        ? postExportViewport
                                                                  ->width()
                                                        : -1.0)
                                            << "x"
                                            << (postExportViewport
                                                        ? postExportViewport
                                                                  ->height()
                                                        : -1.0)
                                            << "/message="
                                            << whiteboard->operationMessage()
                                                       .toStdString()
                                            << ", optimizedRenderState="
                                            << optimizedRenderState
                                            << ", daylightEnvironment="
                                            << daylightEnvironment
                                            << ", themeSceneInvariant="
                                            << loadedSceneThemeInvariant
                                            << ", clipNear="
                                            << (viewCamera
                                                        ? viewCamera
                                                                  ->property(
                                                                          "clip"
                                                                          "Nea"
                                                                          "r")
                                                                  .toDouble()
                                                        : -1.0)
                                            << ", clipFar="
                                            << (viewCamera
                                                        ? viewCamera
                                                                  ->property(
                                                                          "clip"
                                                                          "Far")
                                                                  .toDouble()
                                                        : -1.0)
                                            << ", mainCastsShadow="
                                            << (mainMapLight &&
                                                mainMapLight
                                                        ->property(
                                                                "castsShadow")
                                                        .toBool())
                                            << ", editorStructure="
                                            << editorStructure << '\n';
                                }
                                static_cast<void>(quickWindow);
                                application.quit();
                            });
                });
            });

    QTimer::singleShot(170000, &application, [&]() {
        if (completed) {
            return;
        }
        completed = true;
        std::cerr << "viewer QML smoke test timed out\n";
        application.quit();
    });

    viewer.loadMap(QString::fromLocal8Bit(argv[1]),
                   QString::fromLocal8Bit(argv[2]));
    application.exec();
    return exitCode;
}
