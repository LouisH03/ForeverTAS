#include "viewer/race_viewer_controller.h"
#include "viewer/simulation_debugger_model.h"

#include <QGuiApplication>
#include <QHash>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantMap>

#include <cmath>
#include <iostream>

namespace {

constexpr auto kVehicleSource =
        "src/simulation/runtime/replay_vehicle_simulation.cpp";
constexpr auto kStableInspectionSource =
        "src/engine/physics/world/physics_step.cpp";
constexpr int kApplyControlsLine = 31;
constexpr int kRelocatedBreakpointRequestLine = 34;
constexpr int kRelocatedBreakpointLine = 37;
constexpr int kPreparedBodyLine = 200;

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

QString SourceLine(
        forevertas::viewer::SimulationDebuggerModel &model,
        const QString &path,
        int line) {
    if (!model.selectFile(path)) {
        return {};
    }
    const QVariantList lines = model.lines();
    return line > 0 && line <= lines.size()
                   ? lines[line - 1]
                             .toMap()
                             .value(QStringLiteral("text"))
                             .toString()
                   : QString();
}

double
VectorComponent(const QVariantMap &frame, const QString &name, int component) {
    const QVariantList values = frame.value(name).toList();
    return component >= 0 && component < values.size()
                   ? values[component].toDouble()
                   : 0.0;
}

bool HasRealEngineTree(forevertas::viewer::SimulationDebuggerModel &model) {
    const bool physics = model.selectFile(
            QStringLiteral("src/engine/physics/world/physics_step.cpp"));
    const bool runtime = model.selectFile(QStringLiteral(
            "src/simulation/runtime/replay_simulation_runtime.cpp"));
    bool synthetic = false;
    for (const QVariant &value : model.fileEntries()) {
        const QString path =
                value.toMap().value(QStringLiteral("path")).toString();
        synthetic |=
                path.contains(QStringLiteral("adapter"), Qt::CaseInsensitive);
    }
    return physics && runtime && !synthetic;
}

bool HasModifiedEntry(
        forevertas::viewer::SimulationDebuggerModel &model,
        const QString &path) {
    for (const QVariant &entry : model.fileEntries()) {
        const QVariantMap data = entry.toMap();
        if (data.value(QStringLiteral("path")).toString() == path) {
            return data.value(QStringLiteral("modified")).toBool();
        }
    }
    return false;
}

QVariantMap FileEntry(
        forevertas::viewer::SimulationDebuggerModel &model,
        const QString &path) {
    for (const QVariant &entry : model.fileEntries()) {
        const QVariantMap data = entry.toMap();
        if (data.value(QStringLiteral("path")).toString() == path) {
            return data;
        }
    }
    return {};
}

bool ActiveLineIsSelectedAndMarked(
        forevertas::viewer::SimulationDebuggerModel &model) {
    if (model.activeLine() <= 0 ||
        model.selectedFilePath() != model.activeFilePath()) {
        return false;
    }
    const QVariantList lines = model.lines();
    return model.activeLine() <= static_cast<int>(lines.size()) &&
           lines[model.activeLine() - 1]
                   .toMap()
                   .value(QStringLiteral("active"))
                   .toBool();
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: forevertas-simulation-debugger-tests <Packs> "
                     "<replay>\n";
        return 2;
    }

    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication application(argc, argv);
    QSettings().clear();
    forevertas::viewer::RaceViewerController viewer;
    auto *const model = viewer.simulationDebugger();
    bool okay = true;
    okay &= Check(
            model->toggleBreakpoint(
                    QString::fromLatin1(kVehicleSource), kApplyControlsLine),
            "persistent breakpoint setup failed");
    {
        forevertas::viewer::SimulationDebuggerModel restored;
        const QVariantMap restoredEntry =
                FileEntry(restored, QString::fromLatin1(kVehicleSource));
        okay &= Check(
                restored.selectFile(QString::fromLatin1(kVehicleSource)) &&
                        restored.lines()
                                .at(kApplyControlsLine - 1)
                                .toMap()
                                .value(QStringLiteral("breakpoint"))
                                .toBool() &&
                        restoredEntry.value(QStringLiteral("breakpoint"))
                                .toBool(),
                "line and file breakpoint state did not survive model "
                "reconstruction");
    }
    okay &= Check(
            model->toggleBreakpoint(
                    QString::fromLatin1(kVehicleSource), kApplyControlsLine),
            "persistent breakpoint cleanup failed");
    okay &=
            Check(!viewer.startSimulationDebugger(),
                  "debugger started without a loaded replay");

    enum class Phase {
        Loading,
        WaitingInitialFrame,
        WaitingApplyControls,
        WaitingEditedSourceStep,
        WaitingSubstep,
        WaitingSourceLineStep,
        WaitingTickStep,
        WaitingPastLine,
        WaitingDeferredTick,
        WaitingRestoredTick,
        WaitingInvalidLine,
        WaitingRepeatedBreakpoint,
        WaitingCompileFailure,
        WaitingPauseStop,
        Finished,
    };
    Phase phase = Phase::Loading;
    QString originalApplyLine;
    QHash<qint64, QVariantMap> frames;
    qint64 frameCountBeforeInvalid = 0;
    qint64 firstBreakpointTick = -1;
    qint64 stepTick = -1;
    qint64 framesBeforeSteps = -1;
    QString sourceStepFile;
    int sourceStepLine = -1;
    QString heldInspectionFile;
    int selectionChangesWhileRunning = 0;

    QObject::connect(
            model,
            &forevertas::viewer::SimulationDebuggerModel::selectionChanged,
            &application,
            [&]() {
                if (model->running()) {
                    ++selectionChangesWhileRunning;
                }
            });

    QObject::connect(
            model,
            &forevertas::viewer::SimulationDebuggerModel::frameProduced,
            &application,
            [&](const QVariantMap &frame) {
                const qint64 tick =
                        frame.value(QStringLiteral("tick")).toLongLong();
                frames.insert(tick, frame);
                if (phase == Phase::WaitingInitialFrame && tick == 0) {
                    okay &= Check(
                            model->active() && viewer.selectedRunId() ==
                                                       QStringLiteral("debug"),
                            "native debugger did not initialize its viewer "
                            "run");
                    okay &= Check(
                            viewer.durationMs() ==
                                    frame.value(QStringLiteral("durationMs"))
                                            .toLongLong(),
                            "live viewer did not retain the replay duration");
                    okay &= Check(
                            model->selectFile(QString::fromLatin1(
                                    kStableInspectionSource)),
                            "could not select a stable inspection file before "
                            "continuous playback");
                    heldInspectionFile = model->selectedFilePath();
                    phase = Phase::WaitingApplyControls;
                    viewer.play();
                } else if (phase == Phase::WaitingTickStep && tick == 1) {
                    const double forward = VectorComponent(
                            frame, QStringLiteral("linearSpeed"), 2);
                    okay &= Check(
                            std::abs(forward) < 0.02,
                            "edited real ApplyControls statement did not alter "
                            "reference physics");
                } else if (phase == Phase::WaitingDeferredTick && tick == 2) {
                    const double forward = VectorComponent(
                            frame, QStringLiteral("linearSpeed"), 2);
                    okay &= Check(
                            std::abs(forward) < 0.03,
                            "restoring an already executed line changed the "
                            "current tick");
                    phase = Phase::WaitingRestoredTick;
                } else if (phase == Phase::WaitingRestoredTick && tick == 3) {
                    const double previous = VectorComponent(
                            frames.value(2), QStringLiteral("linearSpeed"), 2);
                    const double restored = VectorComponent(
                            frame, QStringLiteral("linearSpeed"), 2);
                    okay &= Check(
                            restored > previous + 0.05,
                            "past-line restoration did not take effect on the "
                            "next tick");
                    okay &=
                            Check(VectorComponent(
                                          frames.value(1),
                                          QStringLiteral("linearSpeed"),
                                          2) < 0.02,
                                  "a later source edit changed a past "
                                  "simulated tick");
                    okay &=
                            Check(model->toggleBreakpoint(
                                          QString::fromLatin1(kVehicleSource),
                                          kApplyControlsLine),
                                  "invalid-edit breakpoint was rejected");
                    phase = Phase::WaitingInvalidLine;
                }
            });

    QTimer poll;
    poll.setInterval(2);
    QObject::connect(&poll, &QTimer::timeout, &application, [&]() {
        if (phase == Phase::WaitingApplyControls && !model->running() &&
            model->activeFilePath() == QString::fromLatin1(kVehicleSource) &&
            model->activeLine() == kApplyControlsLine) {
            okay &= Check(
                    !heldInspectionFile.isEmpty() &&
                            heldInspectionFile != model->activeFilePath() &&
                            selectionChangesWhileRunning == 0 &&
                            ActiveLineIsSelectedAndMarked(*model),
                    "continuous playback moved the inspected source file or "
                    "failed to restore the exact breakpoint location");
            okay &= Check(
                    !model->lines().at(kRelocatedBreakpointRequestLine - 1)
                                    .toMap()
                                    .value(QStringLiteral("breakpoint"))
                                    .toBool() &&
                            model->lines()
                                    .at(kRelocatedBreakpointLine - 1)
                                    .toMap()
                                    .value(QStringLiteral("breakpoint"))
                                    .toBool() &&
                            model->toggleBreakpoint(
                                    QString::fromLatin1(kVehicleSource),
                                    kRelocatedBreakpointLine),
                    "non-executable breakpoint was not moved to the resolved "
                    "native source line");
            stepTick = model->executionTick();
            framesBeforeSteps = viewer.tickCount();
            okay &= Check(
                    model->canStepSource() && model->canStepTick() &&
                            model->stepSourceLine(),
                    "source-line step could not execute the pending edited "
                    "line");
            phase = Phase::WaitingEditedSourceStep;
        } else if (
                phase == Phase::WaitingEditedSourceStep && !model->stepping() &&
                model->activeLine() > 0) {
            okay &= Check(
                    model->executionTick() == stepTick &&
                            viewer.tickCount() == framesBeforeSteps &&
                            ActiveLineIsSelectedAndMarked(*model) &&
                            model->statusText().startsWith(QStringLiteral(
                                    "Source-line step completed")),
                    "edited source-line step advanced a physics tick or did "
                    "not settle at the next source location");
            okay &=
                    Check(model->toggleBreakpoint(
                                  QString::fromLatin1(kVehicleSource),
                                  kApplyControlsLine),
                          "initial execution breakpoint could not be removed");
            okay &=
                    Check(model->canStepSource() && model->stepSubstep(),
                          "native substep could not start");
            phase = Phase::WaitingSubstep;
        } else if (
                phase == Phase::WaitingSubstep && !model->stepping() &&
                model->activeLine() > 0) {
            okay &= Check(
                    model->executionTick() == stepTick &&
                            viewer.tickCount() == framesBeforeSteps &&
                            ActiveLineIsSelectedAndMarked(*model) &&
                            model->statusText().startsWith(
                                    QStringLiteral("Substep completed")),
                    "native substep advanced a physics tick or lost its source "
                    "location");
            sourceStepFile = model->activeFilePath();
            sourceStepLine = model->activeLine();
            okay &=
                    Check(model->canStepSource() && model->stepSourceLine(),
                          "native source-line step could not start");
            phase = Phase::WaitingSourceLineStep;
        } else if (
                phase == Phase::WaitingSourceLineStep && !model->stepping() &&
                model->activeLine() > 0) {
            okay &= Check(
                    model->executionTick() == stepTick &&
                            viewer.tickCount() == framesBeforeSteps &&
                            ActiveLineIsSelectedAndMarked(*model) &&
                            (model->activeFilePath() != sourceStepFile ||
                             model->activeLine() != sourceStepLine),
                    "source-line step did not advance to a new source line");
            okay &=
                    Check(model->canStepTick() && model->stepTick(),
                          "one-tick step could not start");
            phase = Phase::WaitingTickStep;
        } else if (
                phase == Phase::WaitingTickStep && !model->stepping() &&
                model->executionTick() == stepTick + 1) {
            okay &= Check(
                    viewer.tickCount() == framesBeforeSteps + 1 &&
                            frames.contains(stepTick + 1) &&
                            model->statusText() == QStringLiteral(
                                                           "Advanced exactly "
                                                           "one physics tick."),
                    "one-tick step did not produce exactly one new tick");
            okay &=
                    Check(model->toggleBreakpoint(
                                  QString::fromLatin1(kVehicleSource),
                                  kPreparedBodyLine),
                          "future real-source breakpoint was rejected");
            phase = Phase::WaitingPastLine;
            viewer.play();
        } else if (
                phase == Phase::WaitingPastLine && !model->running() &&
                model->activeFilePath() ==
                        QString::fromLatin1(kVehicleSource) &&
                model->activeLine() == kPreparedBodyLine &&
                !model->variables().isEmpty()) {
            QString pinName;
            for (const QVariant &value : model->variables()) {
                const QString name =
                        value.toMap().value(QStringLiteral("name")).toString();
                if (!name.isEmpty()) {
                    pinName = name;
                    break;
                }
            }
            okay &=
                    Check(!pinName.isEmpty() && model->togglePinned(pinName) &&
                                  !model->pinnedVariables().isEmpty(),
                          "native variable pinning failed");
            okay &= Check(
                    model->selectFile(QString::fromLatin1(kVehicleSource)) &&
                            !model->lines()
                                     .at(kPreparedBodyLine - 1)
                                     .toMap()
                                     .value(QStringLiteral("inlineValue"))
                                     .toString()
                                     .isEmpty(),
                    "in-scope variable value was not shown beside code");
            okay &= Check(
                    model->updateLine(kApplyControlsLine, originalApplyLine) &&
                            !model->hasEdits(),
                    "restoring a past line failed");
            model->toggleBreakpoint(
                    QString::fromLatin1(kVehicleSource), kPreparedBodyLine);
            phase = Phase::WaitingDeferredTick;
            viewer.play();
        } else if (
                phase == Phase::WaitingInvalidLine && !model->running() &&
                model->activeFilePath() ==
                        QString::fromLatin1(kVehicleSource) &&
                model->activeLine() == kApplyControlsLine) {
            firstBreakpointTick = model->executionTick();
            phase = Phase::WaitingRepeatedBreakpoint;
            viewer.play();
        } else if (
                phase == Phase::WaitingRepeatedBreakpoint &&
                !model->running() &&
                model->activeFilePath() ==
                        QString::fromLatin1(kVehicleSource) &&
                model->activeLine() == kApplyControlsLine &&
                model->executionTick() > firstBreakpointTick) {
            okay &=
                    Check(model->executionTick() > firstBreakpointTick,
                          "breakpoint did not stop again on the next tick");
            model->toggleBreakpoint(
                    QString::fromLatin1(kVehicleSource), kApplyControlsLine);
            okay &= Check(
                    model->selectFile(QString::fromLatin1(kVehicleSource)) &&
                            model->updateLine(
                                    kApplyControlsLine,
                                    QStringLiteral("this is not valid C++;")),
                    "invalid native edit could not be staged");
            frameCountBeforeInvalid = viewer.tickCount();
            phase = Phase::WaitingCompileFailure;
            viewer.play();
        } else if (
                phase == Phase::WaitingCompileFailure && !model->running() &&
                !model->editError().isEmpty()) {
            okay &=
                    Check(viewer.tickCount() == frameCountBeforeInvalid,
                          "invalid native C++ advanced the simulation");
            model->resetEdits();
            okay &=
                    Check(!model->hasEdits() && model->editError().isEmpty(),
                          "in-memory source edit reset failed");
            phase = Phase::WaitingPauseStop;
            viewer.play();
            QTimer::singleShot(2, &application, [&viewer]() {
                viewer.pause();
            });
        } else if (
                phase == Phase::WaitingPauseStop && !model->running() &&
                model->activeLine() > 0 && !model->variables().isEmpty() &&
                model->statusText().startsWith(QStringLiteral("Paused"))) {
            okay &= Check(
                    !model->activeFilePath().isEmpty() &&
                            ActiveLineIsSelectedAndMarked(*model) &&
                            selectionChangesWhileRunning == 0,
                    "normal pause did not settle at a real source line");
            viewer.stopSimulationDebugger();
            phase = Phase::Finished;
            poll.stop();
            application.exit(okay ? 0 : 1);
        }
    });

    QObject::connect(
            &viewer,
            &forevertas::viewer::RaceViewerController::stateChanged,
            &application,
            [&]() {
                if (phase != Phase::Loading || viewer.loading() ||
                    !viewer.loaded()) {
                    return;
                }
                okay &=
                        Check(model->available() && HasRealEngineTree(*model),
                              "source tree is not the real adaptive reference "
                              "engine");
                originalApplyLine = SourceLine(
                        *model,
                        QString::fromLatin1(kVehicleSource),
                        kApplyControlsLine);
                okay &= Check(
                        !model->updateLine(
                                kApplyControlsLine,
                                originalApplyLine +
                                        QStringLiteral("\ninvalid")) &&
                                SourceLine(
                                        *model,
                                        QString::fromLatin1(kVehicleSource),
                                        kApplyControlsLine) ==
                                        originalApplyLine,
                        "multi-line input escaped the source-line edit "
                        "boundary");
                const QVariantMap functionLine = model->lines().at(28).toMap();
                const QString lightHighlight =
                        functionLine
                                .value(QStringLiteral("highlighted"))
                                .toString();
                model->setDarkMode(true);
                const QString darkHighlight =
                        model->lines()
                                .at(28)
                                .toMap()
                                .value(QStringLiteral("highlighted"))
                                .toString();
                okay &= Check(
                        model->darkMode() && darkHighlight != lightHighlight &&
                                darkHighlight.contains(
                                        QStringLiteral("#80b9ef")),
                        "source syntax colors did not switch to the dark "
                        "theme");
                model->setDarkMode(false);
                okay &= Check(
                        !originalApplyLine.isEmpty() &&
                                lightHighlight.contains(
                                        QStringLiteral("<span")) &&
                                model->toggleBreakpoint(
                                        QString::fromLatin1(kVehicleSource),
                                        kApplyControlsLine) &&
                                model->toggleBreakpoint(
                                        QString::fromLatin1(kVehicleSource),
                                        kRelocatedBreakpointRequestLine) &&
                                model->updateLine(
                                        kApplyControlsLine,
                                        QStringLiteral(
                                                "  car_.ApplyControlInput("
                                                "CSceneVehicleCar::"
                                                "SControlInput{0.0f, 0.0f, "
                                                "0.0f});")),
                        "future real-engine source edit setup failed");
                const QVariantMap editedLine =
                        model->lines().at(kApplyControlsLine - 1).toMap();
                const QVariantMap editedBreakpointEntry =
                        FileEntry(*model, QString::fromLatin1(kVehicleSource));
                okay &= Check(
                        editedLine.value(QStringLiteral("modified")).toBool() &&
                                editedLine.value(QStringLiteral("breakpoint"))
                                        .toBool() &&
                                editedLine.value(QStringLiteral("original"))
                                                .toString() ==
                                        originalApplyLine &&
                                HasModifiedEntry(
                                        *model,
                                        QString::fromLatin1(kVehicleSource)) &&
                                editedBreakpointEntry
                                        .value(QStringLiteral("modified"))
                                        .toBool() &&
                                editedBreakpointEntry
                                        .value(QStringLiteral("breakpoint"))
                                        .toBool(),
                        "combined line edit and persistent file breakpoint "
                        "tracking failed");
                phase = Phase::WaitingInitialFrame;
                okay &= Check(
                        model->hasEdits() && viewer.startSimulationDebugger(),
                        "native reference debugger did not start");
                poll.start();
            });

    QTimer::singleShot(120000, &application, [&]() {
        std::cerr << "native simulation debugger test timed out; phase="
                  << static_cast<int>(phase)
                  << ", status=" << model->statusText().toStdString()
                  << ", error=" << model->editError().toStdString()
                  << ", file=" << model->activeFilePath().toStdString()
                  << ", line=" << model->activeLine()
                  << ", tick=" << model->executionTick()
                  << ", frames=" << viewer.tickCount() << '\n';
        application.exit(1);
    });

    viewer.loadMap(
            QString::fromLocal8Bit(argv[1]),
            QString::fromLocal8Bit(argv[2]),
            QStringLiteral("reference"));
    return application.exec();
}
