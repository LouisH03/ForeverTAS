#include "app/packs_directory_finder.h"
#include "app/search_controller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QVariantMap>

#include <clocale>
#include <iostream>
#include <string>

namespace {

using forevertas::app::SearchController;

bool Check(bool condition, const char *message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

class NumericLocaleGuard final {
public:
    NumericLocaleGuard() {
        if (const char *const current = std::setlocale(LC_NUMERIC, nullptr)) {
            original_ = current;
        }
    }

    ~NumericLocaleGuard() {
        if (!original_.empty()) {
            std::setlocale(LC_NUMERIC, original_.c_str());
        }
    }

    bool ActivateCommaDecimalLocale() {
        constexpr const char *candidates[] = {
                "fr_FR.utf8", "fr_FR.UTF-8", "de_DE.utf8", "de_DE.UTF-8"};
        for (const char *const candidate : candidates) {
            if (std::setlocale(LC_NUMERIC, candidate) == nullptr) continue;
            const lconv *const details = std::localeconv();
            if (details != nullptr && details->decimal_point != nullptr &&
                std::string(details->decimal_point) == ",") {
                return true;
            }
        }
        return false;
    }

private:
    std::string original_;
};

bool HasOption(const QVariantList &options,
               const QString &id,
               const QString &component) {
    for (const QVariant &value : options) {
        const QVariantMap option = value.toMap();
        if (option.value(QStringLiteral("id")).toString() == id &&
            option.value(QStringLiteral("settingsComponent")).toString() ==
                    component) {
            return true;
        }
    }
    return false;
}

QVariantMap Pass(const SearchController &controller, int index) {
    return controller.modifierPasses().at(index).toMap();
}

QString PassId(const SearchController &controller, int index) {
    return Pass(controller, index).value(QStringLiteral("id")).toString();
}

QVariantMap PassSettings(const SearchController &controller, int index) {
    return Pass(controller, index)
            .value(QStringLiteral("settings"))
            .toMap();
}

void SetValidPaths(SearchController &controller,
                   const QString &packsDirectory,
                   const QString &replayPath) {
    controller.setPacksDirectory(packsDirectory);
    controller.setReplayPath(replayPath);
}

bool TestRegistryAndValidation(const QString &packsDirectory,
                               const QString &replayPath) {
    QSettings().clear();
    SearchController controller;
    SetValidPaths(controller, packsDirectory, replayPath);

    bool okay = Check(controller.canStart(),
                      "valid defaults and paths did not enable Start");
    okay &= Check(controller.searchAlgorithmOptions().size() == 1,
                  "unexpected search algorithm count");
    okay &= Check(controller.modifierOptions().size() == 5,
                  "required modifier options were not exposed");
    okay &= Check(controller.evaluationTargetOptions().size() == 5,
                  "required evaluation targets were not exposed");
    okay &= Check(
            HasOption(controller.modifierOptions(),
                      QStringLiteral("existing-event-perturbation"),
                      QStringLiteral("ExistingEventPerturbationSettings.qml")),
            "existing-event perturbation metadata was not exposed");
    okay &= Check(
            HasOption(controller.modifierOptions(),
                      QStringLiteral("smooth-steering"),
                      QStringLiteral("SmoothSteeringSettings.qml")),
            "smooth steering metadata was not exposed");
    okay &= Check(
            HasOption(controller.modifierOptions(),
                      QStringLiteral("input-insertion"),
                      QStringLiteral("InputInsertionSettings.qml")),
            "input insertion metadata was not exposed");
    okay &= Check(
            HasOption(controller.modifierOptions(),
                      QStringLiteral("input-deletion"),
                      QStringLiteral("InputDeletionSettings.qml")),
            "input deletion metadata was not exposed");
    okay &= Check(
            HasOption(controller.evaluationTargetOptions(),
                      QStringLiteral("finish-time"),
                      QStringLiteral("FinishTimeEvaluationSettings.qml")),
            "finish target metadata was not exposed");
    okay &= Check(
            HasOption(controller.evaluationTargetOptions(),
                      QStringLiteral("volume-entry-time"),
                      QStringLiteral("VolumeEntryEvaluationSettings.qml")),
            "volume target metadata was not exposed");
    okay &= Check(controller.modifierPasses().size() == 1 &&
                          PassId(controller, 0) ==
                                  QStringLiteral("random-steering"),
                  "default modifier pass was incorrect");

    controller.setSearchAlgorithmSetting(
            QStringLiteral("attemptCount"), QStringLiteral("0"));
    okay &= Check(!controller.canStart(), "zero attempts enabled Start");
    controller.setSearchAlgorithmSetting(
            QStringLiteral("attemptCount"), QStringLiteral("10"));

    controller.setModifierPassSetting(
            0, QStringLiteral("seed"), QStringLiteral("4294967296"));
    okay &= Check(!controller.canStart(),
                  "modifier seed overflow enabled Start");
    controller.setModifierPassSetting(
            0, QStringLiteral("seed"), QStringLiteral("123"));

    controller.setModifierPassSetting(
            0, QStringLiteral("minTimeMs"), QStringLiteral("1001"));
    okay &= Check(!controller.canStart(),
                  "unaligned modifier time enabled Start");
    controller.setModifierPassSetting(
            0, QStringLiteral("minTimeMs"), QStringLiteral("1000"));

    controller.setEvaluationTargetSetting(
            QStringLiteral("minTimeMs"), QStringLiteral("1001"));
    okay &= Check(!controller.canStart(),
                  "unaligned evaluation time enabled Start");
    controller.setEvaluationTargetSetting(
            QStringLiteral("minTimeMs"), QStringLiteral("1000"));

    controller.setEvaluationTargetId(QStringLiteral("finish-time"));
    okay &= Check(controller.canStart(),
                  "finish target defaults did not validate");
    controller.setEvaluationTargetId(QStringLiteral("missing-target"));
    okay &= Check(!controller.canStart(),
                  "unknown evaluation target enabled Start");
    controller.setEvaluationTargetId(QStringLiteral("velocity"));
    okay &= Check(controller.canStart(),
                  "restored valid target did not enable Start");
    return okay;
}

bool TestCompositionEditing(const QString &packsDirectory,
                            const QString &replayPath) {
    QSettings().clear();
    SearchController controller;
    SetValidPaths(controller, packsDirectory, replayPath);

    controller.addModifierPass(QStringLiteral("input-deletion"));
    bool okay = Check(controller.modifierPasses().size() == 2,
                      "modifier pass was not added");
    controller.setModifierPassSetting(
            1, QStringLiteral("steerMaxCount"), QStringLiteral("4"));
    okay &= Check(PassSettings(controller, 1)
                                  .value(QStringLiteral("steerMaxCount"))
                                  .toString() == QStringLiteral("4"),
                  "pass-owned setting was not changed");

    controller.moveModifierPass(1, 0);
    okay &= Check(PassId(controller, 0) ==
                          QStringLiteral("input-deletion") &&
                          PassId(controller, 1) ==
                                  QStringLiteral("random-steering"),
                  "modifier pass order did not change");

    controller.setModifierPassId(1, QStringLiteral("smooth-steering"));
    okay &= Check(PassId(controller, 1) ==
                          QStringLiteral("smooth-steering") &&
                          PassSettings(controller, 1).contains(
                                  QStringLiteral("radiusMs")),
                  "modifier pass type did not replace its settings");

    controller.removeModifierPass(1);
    controller.removeModifierPass(0);
    okay &= Check(controller.modifierPasses().isEmpty(),
                  "modifier passes were not removed");
    okay &= Check(!controller.canStart(),
                  "empty modifier composition enabled Start");
    controller.addModifierPass(QStringLiteral("random-steering"));
    okay &= Check(controller.canStart(),
                  "restored modifier composition did not enable Start");
    return okay;
}

bool TestPersistence(const QString &packsDirectory,
                     const QString &replayPath) {
    QSettings().clear();
    {
        SearchController controller;
        SetValidPaths(controller, packsDirectory, replayPath);
        controller.setSearchAlgorithmSetting(
                QStringLiteral("attemptCount"), QStringLiteral("42"));
        controller.setModifierPassSetting(
                0, QStringLiteral("seed"), QStringLiteral("321"));
        controller.addModifierPass(QStringLiteral("input-deletion"));
        controller.setModifierPassSetting(
                1, QStringLiteral("steerMaxCount"), QStringLiteral("4"));
        controller.moveModifierPass(1, 0);
        controller.setEvaluationTargetId(QStringLiteral("point-target"));
        controller.setEvaluationTargetSetting(
                QStringLiteral("x"), QStringLiteral("12.5"));
        QSettings().sync();
    }

    SearchController restored;
    bool okay = Check(restored.searchAlgorithmSettings()
                                  .value(QStringLiteral("attemptCount"))
                                  .toString() == QStringLiteral("42"),
                      "search settings were not persisted");
    okay &= Check(restored.modifierPasses().size() == 2,
                  "modifier pass count was not persisted");
    okay &= Check(PassId(restored, 0) == QStringLiteral("input-deletion") &&
                          PassSettings(restored, 0)
                                          .value(QStringLiteral(
                                                  "steerMaxCount"))
                                          .toString() == QStringLiteral("4"),
                  "first modifier pass was not persisted");
    okay &= Check(PassId(restored, 1) == QStringLiteral("random-steering") &&
                          PassSettings(restored, 1)
                                          .value(QStringLiteral("seed"))
                                          .toString() == QStringLiteral("321"),
                  "second modifier pass was not persisted");
    okay &= Check(restored.evaluationTargetId() ==
                          QStringLiteral("point-target") &&
                          restored.evaluationTargetSettings()
                                          .value(QStringLiteral("x"))
                                          .toString() == QStringLiteral("12.5"),
                  "evaluation target configuration was not persisted");
    okay &= Check(QSettings().contains(
                          QStringLiteral("composition/modifiers")),
                  "modifier composition JSON was not persisted");
    return okay;
}

bool TestLocaleIndependentPersistedDecimals(const QString &packsDirectory,
                                             const QString &replayPath) {
    NumericLocaleGuard locale;
    if (!locale.ActivateCommaDecimalLocale()) {
        return Check(false, "no comma-decimal locale is installed for testing");
    }

    QSettings().clear();
    {
        SearchController controller;
        SetValidPaths(controller, packsDirectory, replayPath);
        controller.setEvaluationTargetId(QStringLiteral("point-target"));
        controller.setEvaluationTargetSetting(
                QStringLiteral("x"), QStringLiteral("12.5"));
        controller.setEvaluationTargetSetting(
                QStringLiteral("y"), QStringLiteral("-3.25"));
        controller.setModifierPassId(
                0, QStringLiteral("existing-event-perturbation"));
        controller.setModifierPassSetting(
                0, QStringLiteral("steerDeltaMin"), QStringLiteral("-0.25"));
        controller.setModifierPassSetting(
                0, QStringLiteral("steerDeltaMax"), QStringLiteral("0.25"));

        bool okay = Check(
                controller.canStart(),
                "UI-entered dot decimals failed under comma LC_NUMERIC");
        controller.setEvaluationTargetSetting(
                QStringLiteral("x"), QStringLiteral("12,5"));
        okay &= Check(!controller.canStart(),
                      "UI-entered comma decimal was accepted");
        controller.setEvaluationTargetSetting(
                QStringLiteral("x"), QStringLiteral("12.5"));
        okay &= Check(controller.canStart(),
                      "restoring a dot decimal did not restore validation");
        if (!okay) return false;
        QSettings().sync();
    }

    SearchController restored;
    SetValidPaths(restored, packsDirectory, replayPath);
    bool okay = Check(
            restored.canStart(),
            "persisted dot decimals failed under comma LC_NUMERIC");
    okay &= Check(
            restored.evaluationTargetId() == QStringLiteral("point-target") &&
                    restored.evaluationTargetSettings()
                                    .value(QStringLiteral("x"))
                                    .toString() == QStringLiteral("12.5") &&
                    restored.evaluationTargetSettings()
                                    .value(QStringLiteral("y"))
                                    .toString() == QStringLiteral("-3.25"),
            "persisted evaluation decimals changed representation");
    okay &= Check(
            PassId(restored, 0) ==
                            QStringLiteral("existing-event-perturbation") &&
                    PassSettings(restored, 0)
                                    .value(QStringLiteral("steerDeltaMin"))
                                    .toString() == QStringLiteral("-0.25") &&
                    PassSettings(restored, 0)
                                    .value(QStringLiteral("steerDeltaMax"))
                                    .toString() == QStringLiteral("0.25"),
            "persisted modifier decimals changed representation");
    return okay;
}

bool TestLegacyMigration() {
    QSettings().clear();
    QSettings().setValue(QStringLiteral("selection/mutationAlgorithm"),
                         QStringLiteral("random-steering"));
    QSettings().setValue(QStringLiteral("search/minMutateMs"),
                         QStringLiteral("1200"));
    QSettings().setValue(QStringLiteral("search/maxMutateMs"),
                         QStringLiteral("2400"));
    QSettings().setValue(QStringLiteral("search/mutationSeed"),
                         QStringLiteral("987"));
    QSettings().setValue(QStringLiteral("selection/evaluationTarget"),
                         QStringLiteral("maximum-speed"));

    SearchController controller;
    bool okay = Check(controller.modifierPasses().size() == 1 &&
                              PassId(controller, 0) ==
                                      QStringLiteral("random-steering"),
                      "legacy mutation selection was not migrated");
    okay &= Check(PassSettings(controller, 0)
                                  .value(QStringLiteral("minTimeMs"))
                                  .toString() == QStringLiteral("1200") &&
                          PassSettings(controller, 0)
                                  .value(QStringLiteral("maxTimeMs"))
                                  .toString() == QStringLiteral("2400") &&
                          PassSettings(controller, 0)
                                  .value(QStringLiteral("seed"))
                                  .toString() == QStringLiteral("987"),
                  "legacy modifier settings were not migrated");
    okay &= Check(controller.evaluationTargetId() ==
                          QStringLiteral("velocity") &&
                          QSettings()
                                          .value(QStringLiteral(
                                                  "selection/evaluationTarget"))
                                          .toString() ==
                                  QStringLiteral("velocity"),
                  "legacy evaluation target was not canonicalized");
    okay &= Check(QSettings().contains(
                          QStringLiteral("composition/modifiers")),
                  "migrated modifier composition was not persisted");
    return okay;
}

bool TestAutomaticPacksDetection() {
    QSettings().clear();
    QTemporaryDir root;
    if (!root.isValid()) return Check(false, "failed to create search root");
    const QString packs = root.filePath(QStringLiteral(
            "Games/prefix/drive_c/Program Files (x86)/"
            "TmUnitedForever/Packs"));
    if (!QDir().mkpath(packs)) {
        return Check(false, "failed to create detected Packs directory");
    }
    QFile packList(QDir(packs).filePath(QStringLiteral("packlist.dat")));
    if (!packList.open(QIODevice::WriteOnly)) {
        return Check(false, "failed to create packlist.dat");
    }
    packList.write("test");
    packList.close();
    const QString pattern = root.filePath(QStringLiteral(
            "Games/*/drive_c/Program Files (x86)/TmUnitedForever/Packs"));
    const QString canonical = QFileInfo(packs).canonicalFilePath();

    SearchController controller(QStringList{pattern});
    QSignalSpy spy(&controller,
                   &SearchController::autoDetectedPacksDirectoryChanged);
    QThread *publicationThread = nullptr;
    QObject::connect(
            &controller,
            &SearchController::autoDetectedPacksDirectoryChanged,
            &controller,
            [&]() { publicationThread = QThread::currentThread(); });
    bool okay = Check(controller.autoDetectedPacksDirectory().isEmpty(),
                      "automatic detection blocked construction");
    okay &= Check(spy.wait(2000),
                  "automatic detection did not publish asynchronously");
    okay &= Check(controller.autoDetectedPacksDirectory() == canonical,
                  "automatic detection proposed the wrong path");
    okay &= Check(publicationThread == controller.thread(),
                  "automatic detection published off the controller thread");
    controller.applyAutoDetectedPacksDirectory();
    okay &= Check(controller.packsDirectory() == canonical &&
                          controller.autoDetectedPacksDirectory().isEmpty(),
                  "Apply did not activate and hide the detected path");
    return okay;
}

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForeverTASTests"));
    QCoreApplication::setApplicationName(
            QStringLiteral("SearchControllerTests"));
    QStandardPaths::setTestModeEnabled(true);

    QTemporaryDir packsDirectory;
    if (!packsDirectory.isValid()) {
        std::cerr << "failed to create temporary Packs directory\n";
        return 1;
    }
    const QString replayPath =
            packsDirectory.filePath(QStringLiteral("run.Replay.Gbx"));
    QFile replay(replayPath);
    if (!replay.open(QIODevice::WriteOnly)) {
        std::cerr << "failed to create temporary replay file\n";
        return 1;
    }
    replay.write("test");
    replay.close();

    const bool okay = TestAutomaticPacksDetection() &&
            TestRegistryAndValidation(packsDirectory.path(), replayPath) &&
            TestCompositionEditing(packsDirectory.path(), replayPath) &&
            TestPersistence(packsDirectory.path(), replayPath) &&
            TestLocaleIndependentPersistedDecimals(
                    packsDirectory.path(), replayPath) &&
            TestLegacyMigration();
    QSettings().clear();
    return okay ? 0 : 1;
}
