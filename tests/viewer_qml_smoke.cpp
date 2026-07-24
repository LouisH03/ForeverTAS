#include "app/search_controller.h"
#include "viewer/race_timeline_item.h"
#include "viewer/race_viewer_controller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

#ifndef FOREVERTAS_SOURCE_DIR
#error "FOREVERTAS_SOURCE_DIR must be defined"
#endif

namespace {

qsizetype DifferentPixelCount(const QImage &leftSource,
                              const QImage &rightSource) {
    if (leftSource.isNull() || rightSource.isNull() ||
        leftSource.size() != rightSource.size()) {
        return 0;
    }
    const QImage left = leftSource.convertToFormat(QImage::Format_RGBA8888);
    const QImage right = rightSource.convertToFormat(QImage::Format_RGBA8888);
    qsizetype different = 0;
    for (int y = 52; y < left.height() - 78; ++y) {
        const int viewportWidth = left.width() * 2 / 3;
        for (int x = 0; x < viewportWidth; ++x) {
            const int byteOffset = x * 4;
            const uchar *const leftPixel = left.constScanLine(y) + byteOffset;
            const uchar *const rightPixel = right.constScanLine(y) + byteOffset;
            const int difference =
                    std::abs(static_cast<int>(leftPixel[0]) -
                             static_cast<int>(rightPixel[0])) +
                    std::abs(static_cast<int>(leftPixel[1]) -
                             static_cast<int>(rightPixel[1])) +
                    std::abs(static_cast<int>(leftPixel[2]) -
                             static_cast<int>(rightPixel[2]));
            if (difference > 6) {
                ++different;
            }
        }
    }
    return different;
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

    QObject::connect(
            &viewer,
            &forevertas::viewer::RaceViewerController::stateChanged,
            &application,
            [&]() {
                if (completed || viewer.loading() || !viewer.loaded()) {
                    return;
                }
                QTimer::singleShot(500, &application, [&]() {
                    if (completed) {
                        return;
                    }
                    QObject *const filled = root->findChild<QObject *>(
                            QStringLiteral("trackFilledModel"));
                    QObject *const wire = root->findChild<QObject *>(
                            QStringLiteral("trackWireModel"));
                    QObject *const car = root->findChild<QObject *>(
                            QStringLiteral("carCollisionRoot"));
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
                    auto *const fpsCounter = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("fpsCounter")));
                    QObject *const fpsCounterLabel =
                            root->findChild<QObject *>(
                                    QStringLiteral("fpsCounterLabel"));
                    QObject *const fpsFrameAnimation =
                            root->findChild<QObject *>(
                                    QStringLiteral("fpsFrameAnimation"));
                    QObject *const wireframeSwitch =
                            root->findChild<QObject *>(
                                    QStringLiteral("wireframeSwitch"));
                    QObject *const wireframeLabel =
                            root->findChild<QObject *>(
                                    QStringLiteral("wireframeLabel"));
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
                    QObject *const velocitySettings =
                            root->findChild<QObject *>(QStringLiteral(
                                    "velocityEvaluationSettings"));
                    const qint64 keyboardStartTick =
                            std::clamp<qint64>(
                                    viewer.tickCount() / 2,
                                    1,
                                    viewer.tickCount() - 2);
                    viewer.setCurrentTick(keyboardStartTick);
                    const bool backwardInvoked =
                            stepBackward != nullptr &&
                            QMetaObject::invokeMethod(
                                    stepBackward,
                                    "activated",
                                    Qt::DirectConnection);
                    const bool steppedBackward = backwardInvoked &&
                            !viewer.playing() &&
                            viewer.currentTick() == keyboardStartTick - 1;
                    const bool forwardInvoked =
                            stepForward != nullptr &&
                            QMetaObject::invokeMethod(
                                    stepForward,
                                    "activated",
                                    Qt::DirectConnection);
                    const bool steppedForward = forwardInvoked &&
                            !viewer.playing() &&
                            viewer.currentTick() == keyboardStartTick;
                    const bool keyboardStepping =
                            steppedBackward && steppedForward &&
                            stepBackward->property("enabled").toBool() &&
                            stepForward->property("enabled").toBool() &&
                            stepBackward->property("sequence").toString() ==
                                    QStringLiteral("Left") &&
                            stepForward->property("sequence").toString() ==
                                    QStringLiteral("Right");
                    const bool fpsCounterValid =
                            raceViewerHeader != nullptr &&
                            fpsCounter != nullptr &&
                            fpsCounter->parentItem() == raceViewerHeader &&
                            std::abs(fpsCounter->x() +
                                             fpsCounter->width() * 0.5 -
                                     raceViewerHeader->width() * 0.5) < 0.1 &&
                            fpsCounterLabel != nullptr &&
                            fpsCounterLabel->property("text")
                                    .toString()
                                    .endsWith(QStringLiteral(" FPS")) &&
                            root->property("viewerFps").toInt() > 0 &&
                            fpsFrameAnimation != nullptr &&
                            fpsFrameAnimation->property("running").toBool();
                    const bool wireframeTextIsWhite =
                            wireframeSwitch != nullptr &&
                            wireframeLabel != nullptr &&
                            wireframeLabel->property("color").value<QColor>() ==
                                    QColor(QStringLiteral("#ffffff"));
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
                                    5 &&
                            searchAlgorithmCombo->property("currentValue")
                                            .toString() ==
                                    QStringLiteral("basic-brute-force") &&
                            modifierComposition
                                            ->property("firstPassSelectedId")
                                            .toString() ==
                                    QStringLiteral("random-steering") &&
                            evaluationTargetCombo->property("currentValue")
                                            .toString() ==
                                    QStringLiteral("velocity") &&
                            basicBruteForceSettings != nullptr &&
                            modifierComposition
                                    ->property("firstPassSettingsLoaded")
                                    .toBool() &&
                            velocitySettings != nullptr;
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
                            searchAlgorithmCombo != nullptr &&
                            evaluationTargetCombo != nullptr &&
                            addModifierCombo != nullptr &&
                            searchAlgorithmCombo->property("slotStyled")
                                    .toBool() &&
                            evaluationTargetCombo->property("slotStyled")
                                    .toBool() &&
                            addModifierCombo->property("slotStyled").toBool() &&
                            modifierComposition
                                    ->property("firstPassSlotStyled").toBool();
                    bool everyOwnedPanelLoaded =
                            evaluationTargetSelector != nullptr &&
                            modifierComposition != nullptr;
                    const std::array<std::pair<const char *, const char *>, 5>
                            evaluationPanels{{
                                    {"velocity",
                                     "velocityEvaluationSettings"},
                                    {"finish-time",
                                     "finishTimeEvaluationSettings"},
                                    {"volume-entry-time",
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
                            timelinePanel != nullptr && viewport != nullptr &&
                            timelinePanel->x() < viewport->x() &&
                            fpsCounterValid && wireframeTextIsWhite &&
                            automaticPacksUi && algorithmSelectorsValid &&
                            everyOwnedPanelLoaded && configurationSectionsValid &&
                            comboSlotsStyled && modifierFocusStable &&
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
                            !ContainsStandardSlider(root) &&
                            !ContainsText(
                                    root,
                                    QStringLiteral("INPUT TIMELINE")) &&
                            keyboardStepping;
                    const QList<QObject *> carFilledModels =
                            root->findChildren<QObject *>(
                                    QStringLiteral("carFilledModel"));
                    const QList<QObject *> carWireModels =
                            root->findChildren<QObject *>(
                                    QStringLiteral("carWireModel"));
                    if (filled == nullptr || wire == nullptr ||
                        car == nullptr) {
                        std::cerr << "track models were not created\n";
                        completed = true;
                        application.quit();
                        return;
                    }
                    QQuickWindow *const window =
                            qobject_cast<QQuickWindow *>(root);
                    if (window == nullptr) {
                        std::cerr << "Main.qml root is not a window\n";
                        completed = true;
                        application.quit();
                        return;
                    }

                    const QVariant filledGeometry =
                            filled->property("geometry");
                    const QVariant wireGeometry = wire->property("geometry");
                    const bool filledAttached = filledGeometry.isValid() &&
                            !filledGeometry.isNull();
                    const bool wireAttached = wireGeometry.isValid() &&
                            !wireGeometry.isNull();
                    const bool filledVisible =
                            filled->property("visible").toBool();
                    const int expectedCarModels =
                            static_cast<int>(viewer.ellipsoidCount());
                    const bool initialCarState = ModelsHaveState(
                            carFilledModels, expectedCarModels, true) &&
                            ModelsHaveState(
                                    carWireModels, expectedCarModels, false);
                    const QImage withCar = window->grabWindow();
                    car->setProperty("visible", false);
                    QTimer::singleShot(
                            150,
                            &application,
                            [&, filledAttached, wireAttached, filledVisible,
                             initialCarState, expectedCarModels,
                             carFilledModels, carWireModels, withCar, car,
                             filled, wire, window]() {
                                const QImage withoutCar =
                                        window->grabWindow();
                                const qsizetype carPixels =
                                        DifferentPixelCount(
                                                withCar, withoutCar);
                                car->setProperty("visible", true);
                                const QImage withFilled =
                                        window->grabWindow();
                                filled->setProperty("visible", false);
                                QTimer::singleShot(
                                        150,
                                        &application,
                                        [&, filledAttached, wireAttached,
                                         filledVisible, initialCarState,
                                         expectedCarModels, carFilledModels,
                                         carWireModels, carPixels, withFilled,
                                         car, wire, window]() {
                                const QImage withoutFilled =
                                        window->grabWindow();
                                const qsizetype filledPixels =
                                        DifferentPixelCount(
                                                withFilled, withoutFilled);

                                root->setProperty("wireframeMode", true);
                                QTimer::singleShot(
                                        150,
                                        &application,
                                        [&, filledAttached, wireAttached,
                                         filledVisible, initialCarState,
                                         expectedCarModels, carFilledModels,
                                         carWireModels, carPixels,
                                         filledPixels, car, wire, window]() {
                                            const bool wireVisible =
                                                    wire->property("visible")
                                                            .toBool();
                                            const bool wireCarState =
                                                    ModelsHaveState(
                                                            carFilledModels,
                                                            expectedCarModels,
                                                            false) &&
                                                    ModelsHaveState(
                                                            carWireModels,
                                                            expectedCarModels,
                                                            true);
                                            const QImage withWire =
                                                    window->grabWindow();
                                            wire->setProperty(
                                                    "visible", false);
                                            QTimer::singleShot(
                                                    150,
                                                    &application,
                                                    [&, filledAttached,
                                                     wireAttached,
                                                     filledVisible,
                                                     initialCarState,
                                                     expectedCarModels,
                                                     carFilledModels,
                                                     carWireModels,
                                                     carPixels, filledPixels,
                                                     wireVisible,
                                                     wireCarState, withWire,
                                                     car, wire, window]() {
                                                        const QImage
                                                                withoutWire =
                                                                        window->grabWindow();
                                                        const qsizetype
                                                                wirePixels =
                                                                        DifferentPixelCount(
                                                                                withWire,
                                                                                withoutWire);
                                                        wire->setProperty(
                                                                "visible",
                                                                true);
                                                        root->setProperty(
                                                                "wireframeMode",
                                                                false);
                                                        QTimer::singleShot(
                                                                150,
                                                                &application,
                                                                [&, filledAttached,
                                                                 wireAttached,
                                                                 filledVisible,
                                                                 initialCarState,
                                                                 expectedCarModels,
                                                                 carFilledModels,
                                                                 carWireModels,
                                                                 carPixels,
                                                                 filledPixels,
                                                                 wireVisible,
                                                                 wireCarState,
                                                                 wirePixels,
                                                                 car, window]() {
                                                                    const bool
                                                                            restoredCarState =
                                                                                    ModelsHaveState(
                                                                                            carFilledModels,
                                                                                            expectedCarModels,
                                                                                            true) &&
                                                                                    ModelsHaveState(
                                                                                            carWireModels,
                                                                                            expectedCarModels,
                                                                                            false);
                                                                    const QImage
                                                                            restoredCar =
                                                                                    window->grabWindow();
                                                                    car->setProperty(
                                                                            "visible",
                                                                            false);
                                                                    QTimer::singleShot(
                                                                            150,
                                                                            &application,
                                                                            [&, filledAttached,
                                                                             wireAttached,
                                                                             filledVisible,
                                                                             initialCarState,
                                                                             carPixels,
                                                                             filledPixels,
                                                                             wireVisible,
                                                                             wireCarState,
                                                                             wirePixels,
                                                                             restoredCarState,
                                                                             restoredCar,
                                                                             window]() {
                                                                                const qsizetype
                                                                                        restoredCarPixels =
                                                                                                DifferentPixelCount(
                                                                                                        restoredCar,
                                                                                                        window->grabWindow());
                                                                                const bool
                                                                                        pixelCaptureAvailable =
                                                                                                carPixels >
                                                                                                100;
                                                                                const bool
                                                                                        renderedSceneVisible =
                                                                                                !pixelCaptureAvailable ||
                                                                                                (filledPixels >
                                                                                                         100 &&
                                                                                                 wirePixels >
                                                                                                         100 &&
                                                                                                 restoredCarPixels >
                                                                                                         100);
                                                                                completed =
                                                                                        true;
                                                                                exitCode =
                                                                                        filledAttached &&
                                                                                                wireAttached &&
                                                                                                filledVisible &&
                                                                                                wireVisible &&
                                                                                                initialCarState &&
                                                                                                wireCarState &&
                                                                                                restoredCarState &&
                                                                                                renderedSceneVisible &&
                                                                                                editorStructure
                                                                                        ? 0
                                                                                        : 1;
                                                                                if (exitCode !=
                                                                                    0) {
                                                                                    std::cerr
                                                                                            << "viewer rendering failed: filledAttached="
                                                                                            << filledAttached
                                                                                            << ", wireAttached="
                                                                                            << wireAttached
                                                                                            << ", filledVisible="
                                                                                            << filledVisible
                                                                                            << ", wireVisible="
                                                                                            << wireVisible
                                                                                            << ", initialCarState="
                                                                                            << initialCarState
                                                                                            << ", wireCarState="
                                                                                            << wireCarState
                                                                                            << ", restoredCarState="
                                                                                            << restoredCarState
                                                                                            << ", editorStructure="
                                                                                            << editorStructure
                                                                                            << ", carPixels="
                                                                                            << carPixels
                                                                                            << ", pixelCaptureAvailable="
                                                                                            << pixelCaptureAvailable
                                                                                            << ", filledPixels="
                                                                                            << filledPixels
                                                                                            << ", wirePixels="
                                                                                            << wirePixels
                                                                                            << ", restoredCarPixels="
                                                                                            << restoredCarPixels
                                                                                            << '\n';
                                                                                }
                                                                                application.quit();
                                                                            });
                                                                });
                                                    });
                                        });
                                        });
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

    viewer.loadReplay(QString::fromLocal8Bit(argv[1]),
                      QString::fromLocal8Bit(argv[2]));
    application.exec();
    return exitCode;
}
