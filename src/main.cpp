#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

namespace {

using forevervalidator::Vector3;
using forevervalidator::experimental::PhysicsSandboxCarState;
using forevervalidator::experimental::PhysicsSandboxStateView;

bool SameVector(const Vector3 &left, const Vector3 &right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool SameCar(const PhysicsSandboxCarState &left,
             const PhysicsSandboxCarState &right) {
    return left.rotationX == right.rotationX &&
           left.rotationY == right.rotationY &&
           left.rotationZ == right.rotationZ &&
           left.rotationW == right.rotationW &&
           SameVector(left.position, right.position) &&
           SameVector(left.linearSpeed, right.linearSpeed) &&
           SameVector(left.angularSpeed, right.angularSpeed) &&
           SameVector(left.force, right.force) &&
           SameVector(left.torque, right.torque);
}

bool SameState(const PhysicsSandboxStateView &left,
               const PhysicsSandboxStateView &right) {
    return left.tick == right.tick &&
           left.timeMs == right.timeMs &&
           left.mapEnvironment == right.mapEnvironment &&
           left.vehicleModel == right.vehicleModel &&
           left.playMode == right.playMode &&
           SameCar(left.car, right.car) &&
           left.accelerate == right.accelerate &&
           left.brake == right.brake &&
           left.steering == right.steering &&
           left.checkpointsCollected == right.checkpointsCollected &&
           left.checkpointsTotal == right.checkpointsTotal &&
           left.completedLaps == right.completedLaps &&
           left.totalLaps == right.totalLaps &&
           left.raceCompleted == right.raceCompleted &&
           left.finishTimeMs == right.finishTimeMs &&
           left.respawnCount == right.respawnCount &&
           left.stuntsScore == right.stuntsScore;
}

template<typename Error>
int ReportError(const char *operation, const Error &error) {
    std::cerr << operation << " failed";
    if (!error.diagnostic.empty()) {
        std::cerr << ": " << error.diagnostic;
    }
    std::cerr << '\n';
    return 1;
}

void PrintUsage(const char *program) {
    std::cout << "Usage: " << program << " PACK_DIRECTORY REPLAY\n";
}

}  // namespace

int main(int argc, char **argv) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    if (argc == 2 && std::string(argv[1]) == "--help") {
        PrintUsage(argv[0]);
        return 0;
    }
    if (argc != 3) {
        PrintUsage(argv[0]);
        return 2;
    }

    const ReplayIdentity identity{argv[2]};
    Result<AssetSource> source = OpenInstalledPackDirectory(argv[1]);
    if (!source) {
        return ReportError("opening pack directory", source.Error());
    }
    Result<AssetBytes> replay = ReadNativeReplayFile(argv[2], identity);
    if (!replay) {
        return ReportError("reading replay", replay.Error());
    }

    PhysicsSandboxOptions options;
    options.backend = SimulationBackend::Reference;
    PhysicsSandboxResult<PhysicsSandbox> created = CreatePhysicsSandbox(
            std::move(source).Value(), options);
    if (!created) {
        return ReportError("creating sandbox", created.Error());
    }
    PhysicsSandbox sandbox = std::move(created).Value();

    const ByteView replayBytes{replay.Value().data(), replay.Value().size()};
    PhysicsSandboxResult<PhysicsSandboxStateView> loaded =
            sandbox.LoadReplay(replayBytes, identity);
    if (!loaded) {
        return ReportError("loading replay", loaded.Error());
    }
    PhysicsSandboxResult<std::vector<PhysicsSandboxInputEvent>> inputs =
            sandbox.ReadInputs();
    if (!inputs) {
        return ReportError("reading inputs", inputs.Error());
    }
    PhysicsSandboxResult<PhysicsSandboxStateView> initial = sandbox.ReadState();
    if (!initial) {
        return ReportError("reading initial state", initial.Error());
    }

    PhysicsSandboxResult<PhysicsSandboxStateView> prefix =
            sandbox.AdvanceTicks(16u);
    if (!prefix) {
        return ReportError("advancing prefix", prefix.Error());
    }
    PhysicsSandboxResult<PhysicsSandboxState> snapshot = sandbox.CaptureState();
    if (!snapshot) {
        return ReportError("capturing state", snapshot.Error());
    }
    PhysicsSandboxResult<PhysicsSandboxStateView> expected =
            sandbox.AdvanceTicks(8u);
    if (!expected) {
        return ReportError("advancing after snapshot", expected.Error());
    }

    PhysicsSandboxResult<PhysicsSandboxStateView> restored =
            sandbox.RestoreState(snapshot.Value());
    if (!restored) {
        return ReportError("restoring state", restored.Error());
    }
    if (!SameState(restored.Value(), snapshot.Value().View())) {
        std::cerr << "restored snapshot does not exactly match its state\n";
        return 1;
    }
    PhysicsSandboxResult<PhysicsSandboxStateView> replayed =
            sandbox.AdvanceTicks(8u);
    if (!replayed) {
        return ReportError("replaying after restore", replayed.Error());
    }
    if (!SameState(expected.Value(), replayed.Value())) {
        std::cerr << "restored simulation diverged\n";
        return 1;
    }

    const PhysicsSandboxStateView &state = replayed.Value();
    std::cout << "inputs=" << inputs.Value().size()
              << " initial_tick=" << initial.Value().tick
              << " final_tick=" << state.tick
              << " position=" << state.car.position.x << ','
              << state.car.position.y << ',' << state.car.position.z
              << " restored=yes\n";
    return 0;
}
