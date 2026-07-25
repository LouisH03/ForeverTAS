#include "app/search_controller.h"
#include "viewer/race_timeline_item.h"
#include "viewer/race_viewer_controller.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QInputDevice>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariant>
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
    return geometries.size() >= 2;
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
                    QObject *const renderModeSelector =
                            root->findChild<QObject *>(
                                    QStringLiteral("renderModeSelector"));
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
                    auto *const runSelector = qobject_cast<QQuickItem *>(
                            root->findChild<QObject *>(
                                    QStringLiteral("runSelector")));
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
                    QObject *const settingsScroll = root->findChild<QObject *>(
                            QStringLiteral("settingsScroll"));
                    QObject *const settingsWheelRedirector =
                            root->property("settingsWheelRedirectorObject")
                                    .value<QObject *>();
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
                    QObject *const bestInputsScrollView =
                            root->findChild<QObject *>(QStringLiteral(
                                    "bestInputsScrollView"));
                    QObject *const bestInputsTextArea =
                            root->findChild<QObject *>(QStringLiteral(
                                    "bestInputsTextArea"));
                    QObject *const copyBestInputsButton =
                            root->findChild<QObject *>(QStringLiteral(
                                    "copyBestInputsButton"));
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
                    const bool runSelectorValid =
                            raceViewerHeader != nullptr &&
                            runSelector != nullptr &&
                            runSelector->parentItem() == raceViewerHeader &&
                            std::abs(runSelector->x() +
                                             runSelector->width() * 0.5 -
                                     raceViewerHeader->width() * 0.5) < 0.6 &&
                            runSelector->property("count").toInt() == 1 &&
                            runSelector->property("currentValue").toString() ==
                                    QStringLiteral("baseline") &&
                            runSelector->property("displayText").toString() ==
                                    QStringLiteral("Baseline");
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
                    const bool modifierPassLayoutValid =
                            modifierComposition != nullptr &&
                            modifierComposition
                                    ->property("firstPassHeaderLayoutValid")
                                    .toBool();

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
                            flickable->setProperty("contentY", 0.0);
                            const bool comboAccepted = sendWheel(
                                    comboItem,
                                    QPointF(comboItem->width() * 0.5,
                                            comboItem->height() * 0.5),
                                    -120);
                            const double afterCombo =
                                    flickable->property("contentY").toDouble();
                            wheelScrollingValid &= comboAccepted &&
                                    afterCombo > 0.0;

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
                            activateCombo(evaluationTargetCombo, 3);
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
                            activateCombo(evaluationTargetCombo, 4);
                    QCoreApplication::processEvents();
                    QObject *const rotationWeightSlider =
                            root->findChild<QObject *>(
                                    QStringLiteral("rotationWeightSlider"));
                    const bool poseSliderValid =
                            rotationWeightSlider != nullptr &&
                            rotationWeightSlider->property("from").toReal() ==
                                    0.0 &&
                            rotationWeightSlider->property("to").toReal() ==
                                    100.0;

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
                            timelinePanel != nullptr && viewport != nullptr &&
                            timelinePanel->x() < viewport->x() &&
                            runSelectorValid && bestInputsUiValid &&
                            searchControlsValid && searchMetricsUiValid &&
                            removedSectionDescriptions &&
                            automaticPacksUi && algorithmSelectorsValid &&
                            everyOwnedPanelLoaded && configurationSectionsValid &&
                            comboSlotsStyled && modifierPassLayoutValid &&
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
                            keyboardStepping;
                    if (!editorStructure) {
                        std::cerr
                                << "editor checks: runSelector=" << runSelectorValid
                                << ", bestInputs=" << bestInputsUiValid
                                << ", searchControls=" << searchControlsValid
                                << ", autoPacks=" << automaticPacksUi
                                << ", selectors=" << algorithmSelectorsValid
                                << ", panels=" << everyOwnedPanelLoaded
                                << ", sections=" << configurationSectionsValid
                                << ", comboStyle=" << comboSlotsStyled
                                << ", passLayout=" << modifierPassLayoutValid
                                << ", wheel=" << wheelScrollingValid
                                << ", dropdown=" << dropdownStateUpdates
                                << ", insertion=" << insertionSlidersValid
                                << ", pose=" << poseSliderValid
                                << ", velocity=" << velocitySliderValid
                                << ", focus=" << modifierFocusStable
                                << ", scrub=" << unboundedFieldsScrubbable
                                << ", keyboard=" << keyboardStepping << '\n';
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

                    const qint64 baselineTickCount = viewer.tickCount();
                    const QVector3D baselinePosition = viewer.carPosition();
                    const QVector3D bestPosition =
                            baselinePosition + QVector3D(7.0f, 0.0f, 0.0f);
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
                    viewer.addSearchRun(QString::fromLocal8Bit(argv[1]),
                                        QString::fromLocal8Bit(argv[2]),
                                        bestFrames);
                    QCoreApplication::processEvents();
                    QCoreApplication::processEvents();

                    QTimer::singleShot(
                            250, &application,
                            [&, filled, wire, quickWindow, runSelector,
                             renderModeSelector, viewCamera,
                             mapEnvironment, daySkyTexture, mainMapLight,
                             fillMapLight, baselineTickCount, bestPosition]() {
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
                                const int expectedCarModels =
                                        static_cast<int>(
                                                viewer.ellipsoidCount() *
                                                viewer.runCount());
                                bool rootsVisible = carRoots.size() == 2;
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
                                bool renderModeOptionsValid =
                                        renderModeSelector != nullptr &&
                                        renderModeSelector->property("count")
                                                        .toInt() == 5;
                                if (renderModeOptionsValid) {
                                    renderModeSelector->setProperty(
                                            "currentIndex", 3);
                                    renderModeOptionsValid =
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
                                            "currentIndex", 4);
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
                                const QColor skyTopColor =
                                        daySkyTexture != nullptr
                                        ? daySkyTexture
                                                  ->property("skyTopColor")
                                                  .value<QColor>()
                                        : QColor();
                                const bool daylightEnvironment =
                                        mapEnvironment != nullptr &&
                                        daySkyTexture != nullptr &&
                                        mainMapLight != nullptr &&
                                        fillMapLight != nullptr &&
                                        mapEnvironment
                                                        ->property(
                                                                "probeExposure")
                                                        .toDouble() >=
                                                0.5 &&
                                        daySkyTexture
                                                        ->property("skyEnergy")
                                                        .toDouble() >=
                                                1.0 &&
                                        daySkyTexture
                                                        ->property("sunEnergy")
                                                        .toDouble() >=
                                                1.0 &&
                                        skyTopColor.isValid() &&
                                        skyTopColor.blue() >
                                                skyTopColor.green() &&
                                        skyTopColor.green() >
                                                skyTopColor.red() &&
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

                                const auto activateRun =
                                        [runSelector](int index) {
                                            return runSelector != nullptr &&
                                                    QMetaObject::invokeMethod(
                                                            runSelector,
                                                            "activated",
                                                            Qt::DirectConnection,
                                                            Q_ARG(int, index));
                                        };
                                const bool baselineActivated = activateRun(0);
                                QCoreApplication::processEvents();
                                QCoreApplication::processEvents();
                                const bool baselineSelected =
                                        baselineActivated &&
                                        viewer.selectedRunId() ==
                                                QStringLiteral("baseline") &&
                                        viewer.tickCount() == baselineTickCount &&
                                        (viewer.carPosition() - bestPosition)
                                                        .length() > 0.1f;

                                const bool bestActivated = activateRun(1);
                                QCoreApplication::processEvents();
                                QCoreApplication::processEvents();
                                const bool bestReselected =
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

                                completed = true;
                                exitCode =
                                        geometryAttached && rootsVisible &&
                                                        initialModelState &&
                                                        bestSelectedInitially &&
                                                        baselineSelected &&
                                                        bestReselected &&
                                                        neutralModeState &&
                                                        collisionModeState &&
                                                        materialDebugState &&
                                                        wireframeState &&
                                                        restoredState &&
                                                        optimizedRenderState &&
                                                        daylightEnvironment &&
                                                        editorStructure
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
                                            << ", baselineSelected="
                                            << baselineSelected
                                            << ", bestReselected="
                                            << bestReselected
                                            << ", collisionMode="
                                            << collisionModeState
                                            << ", neutralMode="
                                            << neutralModeState
                                            << ", materialDebug="
                                            << materialDebugState
                                            << ", wireframe=" << wireframeState
                                            << ", restored=" << restoredState
                                            << ", optimizedRenderState="
                                            << optimizedRenderState
                                            << ", daylightEnvironment="
                                            << daylightEnvironment
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

    viewer.loadReplay(QString::fromLocal8Bit(argv[1]),
                      QString::fromLocal8Bit(argv[2]));
    application.exec();
    return exitCode;
}
