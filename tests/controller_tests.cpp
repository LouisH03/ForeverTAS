#include "app/search_controller.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <iostream>

namespace {

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
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

    controller.setMinMutateMs(QStringLiteral("1001"));
    okay &= Check(!controller.canStart(),
                  "unaligned mutation time enabled Start");
    controller.setMinMutateMs(QStringLiteral("1000"));

    controller.setMaxMutateMs(QStringLiteral("999"));
    okay &= Check(!controller.canStart(),
                  "reversed mutation range enabled Start");
    controller.setMaxMutateMs(QStringLiteral("6000"));

    controller.setMinEvalTimeMs(QStringLiteral("999"));
    okay &= Check(!controller.canStart(),
                  "evaluation before mutation enabled Start");
    controller.setMinEvalTimeMs(QStringLiteral("1000"));

    controller.setMaxEvalTimeMs(QStringLiteral("999"));
    okay &= Check(!controller.canStart(),
                  "reversed evaluation range enabled Start");
    controller.setMaxEvalTimeMs(QStringLiteral("6000"));

    controller.setAttemptCount(QStringLiteral("0"));
    okay &= Check(!controller.canStart(), "zero attempts enabled Start");
    controller.setAttemptCount(
            QStringLiteral("18446744073709551615"));
    okay &= Check(controller.canStart(),
                  "maximum uint64 attempt count was rejected");
    controller.setAttemptCount(
            QStringLiteral("18446744073709551616"));
    okay &= Check(!controller.canStart(),
                  "uint64 attempt overflow enabled Start");
    controller.setAttemptCount(QStringLiteral("10"));

    controller.setMutationSeed(QStringLiteral("4294967295"));
    okay &= Check(controller.canStart(),
                  "maximum uint32 mutation seed was rejected");
    controller.setMutationSeed(QStringLiteral("4294967296"));
    okay &= Check(!controller.canStart(),
                  "uint32 mutation seed overflow enabled Start");
    controller.setMutationSeed(QStringLiteral("123"));

    controller.setMinMutateMs(QStringLiteral("not-a-number"));
    okay &= Check(!controller.canStart(),
                  "non-decimal time enabled Start");

    const QString maximumAlignedTime =
            QStringLiteral("9223372036854775800");
    controller.setMinMutateMs(maximumAlignedTime);
    controller.setMaxMutateMs(maximumAlignedTime);
    controller.setMinEvalTimeMs(maximumAlignedTime);
    controller.setMaxEvalTimeMs(maximumAlignedTime);
    okay &= Check(controller.canStart(),
                  "largest aligned int64 time was rejected");

    controller.setMinMutateMs(QStringLiteral("9223372036854775808"));
    okay &= Check(!controller.canStart(),
                  "int64 time overflow enabled Start");
    controller.setMinMutateMs(QStringLiteral("1000"));
    controller.setMaxMutateMs(QStringLiteral("6000"));
    controller.setMinEvalTimeMs(QStringLiteral("1000"));
    controller.setMaxEvalTimeMs(QStringLiteral("6000"));

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
        controller.setMinMutateMs(QStringLiteral("20"));
        controller.setMaxMutateMs(QStringLiteral("40"));
        controller.setMinEvalTimeMs(QStringLiteral("30"));
        controller.setMaxEvalTimeMs(QStringLiteral("50"));
        controller.setAttemptCount(QStringLiteral("42"));
        controller.setMutationSeed(QStringLiteral("4294967295"));
        QSettings().sync();
    }

    forevertas::app::SearchController restored;
    bool okay = true;
    okay &= Check(restored.packsDirectory() == packsDirectory,
                  "Packs directory was not persisted");
    okay &= Check(restored.replayPath() == replayPath,
                  "replay path was not persisted");
    okay &= Check(restored.minMutateMs() == QStringLiteral("20"),
                  "minimum mutation time was not persisted");
    okay &= Check(restored.maxMutateMs() == QStringLiteral("40"),
                  "maximum mutation time was not persisted");
    okay &= Check(restored.minEvalTimeMs() == QStringLiteral("30"),
                  "minimum evaluation time was not persisted");
    okay &= Check(restored.maxEvalTimeMs() == QStringLiteral("50"),
                  "maximum evaluation time was not persisted");
    okay &= Check(restored.attemptCount() == QStringLiteral("42"),
                  "attempt count was not persisted");
    okay &= Check(restored.mutationSeed() ==
                          QStringLiteral("4294967295"),
                  "mutation seed was not persisted");
    okay &= Check(restored.canStart(),
                  "persisted valid settings did not enable Start");
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

    const bool okay =
            TestValidation(packsDirectory.path(), replayPath) &&
            TestPersistence(packsDirectory.path(), replayPath);
    QSettings().clear();
    return okay ? 0 : 1;
}
