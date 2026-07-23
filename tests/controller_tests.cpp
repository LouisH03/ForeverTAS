#include "app/search_controller.h"
#include "app/packs_directory_finder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <iostream>

namespace {

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool HasOption(const QVariantList &options,
               const QString &id,
               const QString &label,
               const QString &settingsComponent) {
    for (const QVariant &value : options) {
        const QVariantMap option = value.toMap();
        if (option.value(QStringLiteral("id")).toString() == id &&
            option.value(QStringLiteral("label")).toString() == label &&
            option.value(QStringLiteral("settingsComponent")).toString() ==
                    settingsComponent) {
            return true;
        }
    }
    return false;
}

void SetValidPaths(forevertas::app::SearchController &controller,
                   const QString &packsDirectory,
                   const QString &replayPath) {
    controller.setPacksDirectory(packsDirectory);
    controller.setReplayPath(replayPath);
}

bool TestValidation(const QString &packsDirectory,
                    const QString &replayPath) {
    using forevertas::app::SearchController;

    SearchController controller;
    SetValidPaths(controller, packsDirectory, replayPath);
    bool okay = Check(controller.canStart(),
                      "valid defaults and paths did not enable Start");
    okay &= Check(
            HasOption(controller.searchAlgorithmOptions(),
                      QStringLiteral("basic-brute-force"),
                      QStringLiteral("Basic brute force"),
                      QStringLiteral("BasicBruteForceSearchSettings.qml")),
            "search algorithm registry metadata was not exposed");
    okay &= Check(
            HasOption(controller.mutationAlgorithmOptions(),
                      QStringLiteral("random-steering"),
                      QStringLiteral("Random steering"),
                      QStringLiteral("RandomSteeringMutationSettings.qml")),
            "mutation algorithm registry metadata was not exposed");
    okay &= Check(
            HasOption(controller.evaluationTargetOptions(),
                      QStringLiteral("maximum-speed"),
                      QStringLiteral("Maximum speed"),
                      QStringLiteral("MaximumSpeedEvaluationSettings.qml")),
            "evaluation target registry metadata was not exposed");

    controller.setSearchAlgorithmId(QStringLiteral("missing-search"));
    okay &= Check(!controller.canStart(),
                  "unknown search algorithm enabled Start");
    controller.setSearchAlgorithmId(QStringLiteral("basic-brute-force"));
    controller.setMutationAlgorithmId(QStringLiteral("missing-mutation"));
    okay &= Check(!controller.canStart(),
                  "unknown mutation algorithm enabled Start");
    controller.setMutationAlgorithmId(QStringLiteral("random-steering"));
    controller.setEvaluationTargetId(QStringLiteral("missing-evaluation"));
    okay &= Check(!controller.canStart(),
                  "unknown evaluation target enabled Start");
    controller.setEvaluationTargetId(QStringLiteral("maximum-speed"));
    okay &= Check(controller.canStart(),
                  "restored algorithm selections did not enable Start");

    controller.setSearchAlgorithmSetting(
            QStringLiteral("minMutateMs"), QStringLiteral("1001"));
    okay &= Check(!controller.canStart(),
                  "unaligned mutation time enabled Start");
    controller.setSearchAlgorithmSetting(
            QStringLiteral("minMutateMs"), QStringLiteral("1000"));

    controller.setSearchAlgorithmSetting(
            QStringLiteral("maxMutateMs"), QStringLiteral("999"));
    okay &= Check(!controller.canStart(),
                  "reversed mutation range enabled Start");
    controller.setSearchAlgorithmSetting(
            QStringLiteral("maxMutateMs"), QStringLiteral("6000"));

    controller.setSearchAlgorithmSetting(
            QStringLiteral("minEvalTimeMs"), QStringLiteral("999"));
    okay &= Check(!controller.canStart(),
                  "evaluation before mutation enabled Start");
    controller.setSearchAlgorithmSetting(
            QStringLiteral("minEvalTimeMs"), QStringLiteral("1000"));

    controller.setSearchAlgorithmSetting(
            QStringLiteral("maxEvalTimeMs"), QStringLiteral("999"));
    okay &= Check(!controller.canStart(),
                  "reversed evaluation range enabled Start");
    controller.setSearchAlgorithmSetting(
            QStringLiteral("maxEvalTimeMs"), QStringLiteral("6000"));

    controller.setSearchAlgorithmSetting(
            QStringLiteral("attemptCount"), QStringLiteral("0"));
    okay &= Check(!controller.canStart(), "zero attempts enabled Start");
    controller.setSearchAlgorithmSetting(
            QStringLiteral("attemptCount"),
            QStringLiteral("18446744073709551615"));
    okay &= Check(controller.canStart(),
                  "maximum uint64 attempt count was rejected");
    controller.setSearchAlgorithmSetting(
            QStringLiteral("attemptCount"),
            QStringLiteral("18446744073709551616"));
    okay &= Check(!controller.canStart(),
                  "uint64 attempt overflow enabled Start");
    controller.setSearchAlgorithmSetting(
            QStringLiteral("attemptCount"), QStringLiteral("10"));

    controller.setMutationAlgorithmSetting(
            QStringLiteral("seed"), QStringLiteral("4294967295"));
    okay &= Check(controller.canStart(),
                  "maximum uint32 mutation seed was rejected");
    controller.setMutationAlgorithmSetting(
            QStringLiteral("seed"), QStringLiteral("4294967296"));
    okay &= Check(!controller.canStart(),
                  "uint32 mutation seed overflow enabled Start");
    controller.setMutationAlgorithmSetting(
            QStringLiteral("seed"), QStringLiteral("123"));

    controller.setSearchAlgorithmSetting(
            QStringLiteral("minMutateMs"), QStringLiteral("not-a-number"));
    okay &= Check(!controller.canStart(),
                  "non-decimal time enabled Start");

    const QString maximumAlignedTime =
            QStringLiteral("9223372036854775800");
    controller.setSearchAlgorithmSetting(
            QStringLiteral("minMutateMs"), maximumAlignedTime);
    controller.setSearchAlgorithmSetting(
            QStringLiteral("maxMutateMs"), maximumAlignedTime);
    controller.setSearchAlgorithmSetting(
            QStringLiteral("minEvalTimeMs"), maximumAlignedTime);
    controller.setSearchAlgorithmSetting(
            QStringLiteral("maxEvalTimeMs"), maximumAlignedTime);
    okay &= Check(controller.canStart(),
                  "largest aligned int64 time was rejected");

    controller.setSearchAlgorithmSetting(
            QStringLiteral("minMutateMs"),
            QStringLiteral("9223372036854775808"));
    okay &= Check(!controller.canStart(),
                  "int64 time overflow enabled Start");
    controller.setSearchAlgorithmSetting(
            QStringLiteral("minMutateMs"), QStringLiteral("1000"));
    controller.setSearchAlgorithmSetting(
            QStringLiteral("maxMutateMs"), QStringLiteral("6000"));
    controller.setSearchAlgorithmSetting(
            QStringLiteral("minEvalTimeMs"), QStringLiteral("1000"));
    controller.setSearchAlgorithmSetting(
            QStringLiteral("maxEvalTimeMs"), QStringLiteral("6000"));

    const QVariantMap beforeUnknown = controller.searchAlgorithmSettings();
    controller.setSearchAlgorithmSetting(
            QStringLiteral("notRegistered"), QStringLiteral("123"));
    okay &= Check(controller.searchAlgorithmSettings() == beforeUnknown,
                  "unknown option setting was accepted");

    controller.setPacksDirectory(replayPath);
    okay &= Check(!controller.canStart(),
                  "file was accepted as Packs directory");
    controller.setPacksDirectory(packsDirectory);
    controller.setReplayPath(packsDirectory);
    okay &= Check(!controller.canStart(),
                  "directory was accepted as replay file");
    controller.setReplayPath(replayPath);
    okay &= Check(controller.canStart(),
                  "restored valid settings did not enable Start");
    return okay;
}

bool TestPersistence(const QString &packsDirectory,
                     const QString &replayPath) {
    {
        forevertas::app::SearchController controller;
        SetValidPaths(controller, packsDirectory, replayPath);
        controller.setSearchAlgorithmSetting(
                QStringLiteral("minMutateMs"), QStringLiteral("20"));
        controller.setSearchAlgorithmSetting(
                QStringLiteral("maxMutateMs"), QStringLiteral("40"));
        controller.setSearchAlgorithmSetting(
                QStringLiteral("minEvalTimeMs"), QStringLiteral("30"));
        controller.setSearchAlgorithmSetting(
                QStringLiteral("maxEvalTimeMs"), QStringLiteral("50"));
        controller.setSearchAlgorithmSetting(
                QStringLiteral("attemptCount"), QStringLiteral("42"));
        controller.setMutationAlgorithmSetting(
                QStringLiteral("seed"), QStringLiteral("4294967295"));
        controller.setSearchAlgorithmId(QStringLiteral("missing-search"));
        controller.setSearchAlgorithmId(QStringLiteral("basic-brute-force"));
        controller.setMutationAlgorithmId(QStringLiteral("missing-mutation"));
        controller.setMutationAlgorithmId(QStringLiteral("random-steering"));
        controller.setEvaluationTargetId(QStringLiteral("missing-evaluation"));
        controller.setEvaluationTargetId(QStringLiteral("maximum-speed"));
        QSettings().sync();
    }

    forevertas::app::SearchController restored;
    bool okay = true;
    okay &= Check(restored.packsDirectory() == packsDirectory,
                  "Packs directory was not persisted");
    okay &= Check(restored.replayPath() == replayPath,
                  "replay path was not persisted");
    okay &= Check(restored.searchAlgorithmSettings()
                                  .value(QStringLiteral("minMutateMs"))
                                  .toString() == QStringLiteral("20"),
                  "minimum mutation time was not persisted");
    okay &= Check(restored.searchAlgorithmSettings()
                                  .value(QStringLiteral("maxMutateMs"))
                                  .toString() == QStringLiteral("40"),
                  "maximum mutation time was not persisted");
    okay &= Check(restored.searchAlgorithmSettings()
                                  .value(QStringLiteral("minEvalTimeMs"))
                                  .toString() == QStringLiteral("30"),
                  "minimum evaluation time was not persisted");
    okay &= Check(restored.searchAlgorithmSettings()
                                  .value(QStringLiteral("maxEvalTimeMs"))
                                  .toString() == QStringLiteral("50"),
                  "maximum evaluation time was not persisted");
    okay &= Check(restored.searchAlgorithmSettings()
                                  .value(QStringLiteral("attemptCount"))
                                  .toString() == QStringLiteral("42"),
                  "attempt count was not persisted");
    okay &= Check(restored.mutationAlgorithmSettings()
                                  .value(QStringLiteral("seed"))
                                  .toString() ==
                          QStringLiteral("4294967295"),
                  "mutation seed was not persisted");
    okay &= Check(restored.searchAlgorithmId() ==
                          QStringLiteral("basic-brute-force"),
                  "search algorithm selection was not persisted");
    okay &= Check(restored.mutationAlgorithmId() ==
                          QStringLiteral("random-steering"),
                  "mutation algorithm selection was not persisted");
    okay &= Check(restored.evaluationTargetId() ==
                          QStringLiteral("maximum-speed"),
                  "evaluation target selection was not persisted");
    okay &= Check(restored.canStart(),
                  "persisted valid settings did not enable Start");
    okay &= Check(
            QSettings()
                            .value(QStringLiteral(
                                    "configuration/search/basic-brute-force/"
                                    "attemptCount"))
                            .toString() == QStringLiteral("42"),
            "search option setting was not namespaced");
    okay &= Check(
            QSettings()
                            .value(QStringLiteral(
                                    "configuration/mutation/random-steering/"
                                    "seed"))
                            .toString() == QStringLiteral("4294967295"),
            "mutation option setting was not namespaced");
    return okay;
}

bool TestLegacySettingsMigration() {
    QSettings().clear();
    const QString previousSearchId =
            QStringLiteral("seri" "al-brute-force");
    QSettings().setValue(QStringLiteral("selection/searchAlgorithm"),
                         previousSearchId);
    QSettings().setValue(
            QStringLiteral("configuration/search/%1/attemptCount")
                    .arg(previousSearchId),
            QStringLiteral("88"));
    QSettings().setValue(QStringLiteral("search/minMutateMs"),
                         QStringLiteral("1200"));
    QSettings().setValue(QStringLiteral("search/maxMutateMs"),
                         QStringLiteral("2400"));
    QSettings().setValue(QStringLiteral("search/minEvalTimeMs"),
                         QStringLiteral("1300"));
    QSettings().setValue(QStringLiteral("search/maxEvalTimeMs"),
                         QStringLiteral("2500"));
    QSettings().setValue(QStringLiteral("search/attemptCount"),
                         QStringLiteral("77"));
    QSettings().setValue(QStringLiteral("search/mutationSeed"),
                         QStringLiteral("987"));

    const forevertas::app::SearchController controller;
    bool okay = Check(
            controller.searchAlgorithmSettings()
                            .value(QStringLiteral("minMutateMs"))
                            .toString() == QStringLiteral("1200"),
            "legacy search settings were not loaded");
    okay &= Check(
            controller.searchAlgorithmSettings()
                            .value(QStringLiteral("attemptCount"))
                            .toString() == QStringLiteral("88"),
            "previous namespaced attempt count was not loaded");
    okay &= Check(
            controller.mutationAlgorithmSettings()
                            .value(QStringLiteral("seed"))
                            .toString() == QStringLiteral("987"),
            "legacy mutation settings were not loaded");
    okay &= Check(controller.searchAlgorithmId() ==
                          QStringLiteral("basic-brute-force"),
                  "previous search ID was not canonicalized");
    okay &= Check(
            QSettings().value(QStringLiteral("selection/searchAlgorithm"))
                            .toString() == QStringLiteral("basic-brute-force"),
            "canonical search ID was not persisted");
    okay &= Check(
            QSettings()
                            .value(QStringLiteral(
                                    "configuration/search/basic-brute-force/"
                                    "attemptCount"))
                            .toString() == QStringLiteral("88"),
            "previous namespaced setting was not promoted");
    QSettings().clear();
    return okay;
}

bool TestAutomaticPacksDetection() {
    using forevertas::app::FindInstalledPacksDirectory;
    using forevertas::app::SearchController;

    QTemporaryDir searchRoot;
    if (!searchRoot.isValid()) {
        return Check(false, "failed to create automatic search root");
    }
    const QString detectedPacks = searchRoot.filePath(QStringLiteral(
            "Games/tmuf-prefix/drive_c/Program Files (x86)/"
            "TmUnitedForever/Packs"));
    if (!QDir().mkpath(detectedPacks)) {
        return Check(false, "failed to create detected Packs directory");
    }
    QFile packList(QDir(detectedPacks).filePath(
            QStringLiteral("packlist.dat")));
    if (!packList.open(QIODevice::WriteOnly)) {
        return Check(false, "failed to create packlist.dat");
    }
    packList.write("test");
    packList.close();

    const QString pattern = searchRoot.filePath(QStringLiteral(
            "Games/*/drive_c/Program Files (x86)/"
            "TmUnitedForever/Packs"));
    const QString canonical = QFileInfo(detectedPacks).canonicalFilePath();
    bool okay = Check(
            FindInstalledPacksDirectory({pattern}) == canonical,
            "wildcard Packs search did not find the candidate");

    QSettings().clear();
    {
        SearchController controller(QStringList{pattern});
        QSignalSpy detectedSpy(
                &controller,
                &SearchController::autoDetectedPacksDirectoryChanged);
        QThread *publicationThread = nullptr;
        QObject::connect(
                &controller,
                &SearchController::autoDetectedPacksDirectoryChanged,
                &controller,
                [&]() { publicationThread = QThread::currentThread(); });
        okay &= Check(controller.packsDirectory().isEmpty(),
                      "automatic detection changed the active path");
        okay &= Check(controller.autoDetectedPacksDirectory().isEmpty(),
                      "automatic detection blocked construction");
        okay &= Check(detectedSpy.wait(2000),
                      "automatic detection did not publish asynchronously");
        okay &= Check(controller.autoDetectedPacksDirectory() == canonical,
                      "automatic detection did not propose the path");
        okay &= Check(publicationThread == controller.thread(),
                      "automatic detection published off the controller thread");
        okay &= Check(!QSettings().contains(
                              QStringLiteral("paths/packsDirectory")),
                      "automatic detection persisted before Apply");
        controller.applyAutoDetectedPacksDirectory();
        QSettings().sync();
        okay &= Check(controller.packsDirectory() == canonical,
                      "Apply did not activate the detected path");
        okay &= Check(controller.autoDetectedPacksDirectory().isEmpty(),
                      "Apply did not hide the automatic suggestion");
        okay &= Check(
                QSettings()
                                .value(QStringLiteral("paths/packsDirectory"))
                                .toString() == canonical,
                "Apply did not persist the detected path");
    }

    QSettings().clear();
    QSettings().setValue(
            QStringLiteral("paths/packsDirectory"), detectedPacks);
    {
        SearchController controller(QStringList{pattern});
        QSignalSpy detectedSpy(
                &controller,
                &SearchController::autoDetectedPacksDirectoryChanged);
        QTest::qWait(50);
        okay &= Check(controller.autoDetectedPacksDirectory().isEmpty(),
                      "saved Packs path did not suppress detection");
        okay &= Check(detectedSpy.isEmpty(),
                      "saved Packs path still launched detection");
    }

    QSettings().clear();
    {
        SearchController controller(QStringList{pattern});
        QSignalSpy detectedSpy(
                &controller,
                &SearchController::autoDetectedPacksDirectoryChanged);
        controller.setPacksDirectory(searchRoot.path());
        QTest::qWait(100);
        okay &= Check(controller.autoDetectedPacksDirectory().isEmpty(),
                      "pending detection overrode a manual path");
        okay &= Check(detectedSpy.isEmpty(),
                      "pending detection published after a manual path");
    }

    QSettings().clear();
    {
        SearchController controller(QStringList{pattern});
        QSignalSpy detectedSpy(
                &controller,
                &SearchController::autoDetectedPacksDirectoryChanged);
        okay &= Check(detectedSpy.wait(2000),
                      "manual-clear test did not receive a suggestion");
        okay &= Check(!controller.autoDetectedPacksDirectory().isEmpty(),
                      "manual-clear test did not start with a suggestion");
        controller.setPacksDirectory(searchRoot.path());
        okay &= Check(controller.autoDetectedPacksDirectory().isEmpty(),
                      "manual path change did not hide the suggestion");
    }
    QSettings().clear();
    return okay;
}

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(
            QStringLiteral("ForeverTASTests"));
    QCoreApplication::setApplicationName(
            QStringLiteral("ControllerTests"));
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();

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
            TestLegacySettingsMigration() &&
            TestValidation(packsDirectory.path(), replayPath) &&
            TestPersistence(packsDirectory.path(), replayPath);
    QSettings().clear();
    return okay ? 0 : 1;
}
