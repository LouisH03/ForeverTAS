#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using forevervalidator::DiscriminatedResult;
using namespace forevervalidator;
using namespace forevervalidator::experimental;

template<typename T, typename Error>
T Require(DiscriminatedResult<T, Error> result, const char *operation) {
    if (!result) {
        std::string message(operation);
        if (!result.Error().diagnostic.empty()) {
            message += ": ";
            message += result.Error().diagnostic;
        }
        throw std::runtime_error(std::move(message));
    }
    return std::move(result).Value();
}

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool Finite(float value) {
    return std::isfinite(value);
}

bool Finite(const Vector3 &value) {
    return Finite(value.x) && Finite(value.y) && Finite(value.z);
}

bool Finite(const PhysicsSandboxTransform &value) {
    return Finite(value.basisX) && Finite(value.basisY) &&
            Finite(value.basisZ) && Finite(value.translation);
}

bool DifferentTransform(const PhysicsSandboxTransform &left,
                        const PhysicsSandboxTransform &right) {
    const auto different = [](const Vector3 &a, const Vector3 &b) {
        constexpr float localEpsilon = 0.001f;
        return std::fabs(a.x - b.x) > localEpsilon ||
                std::fabs(a.y - b.y) > localEpsilon ||
                std::fabs(a.z - b.z) > localEpsilon;
    };
    return different(left.translation, right.translation) ||
            different(left.basisX, right.basisX) ||
            different(left.basisY, right.basisY) ||
            different(left.basisZ, right.basisZ);
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: forevertas-render-scene-data <Packs> <replay>\n";
        return 2;
    }

    try {
        const ReplayIdentity identity{argv[2]};
        AssetSource source = Require(
                OpenInstalledPackDirectory(argv[1]),
                "opening Packs directory failed");
        AssetBytes replay = Require(
                ReadNativeReplayFile(argv[2], identity),
                "reading replay failed");
        PhysicsSandboxOptions options;
        options.backend = SimulationBackend::OptimizedCpu;
        options.tickDurationMs = 10u;
        PhysicsSandbox sandbox = Require(
                CreatePhysicsSandbox(std::move(source), options),
                "creating sandbox failed");
        static_cast<void>(Require(
                sandbox.LoadReplay({replay.data(), replay.size()}, identity),
                "loading replay failed"));
        const PhysicsSandboxSceneView collisionScene = Require(
                sandbox.ReadScene(), "reading collision scene failed");
        const PhysicsSandboxRenderSceneHandle renderScene = Require(
                sandbox.ReadRenderScene(), "reading render scene failed");

        bool okay = Check(renderScene != nullptr,
                          "render-scene handle was null");
        if (!renderScene) return 1;
        okay &= Check(!renderScene->meshes.empty(),
                      "render scene has no visual meshes");
        okay &= Check(!renderScene->materials.empty(),
                      "render scene has no materials");
        okay &= Check(!renderScene->instances.empty(),
                      "render scene has no instances");
        okay &= Check(renderScene->instances.size() >
                              renderScene->meshes.size(),
                      "visual meshes were duplicated per placement");

        std::size_t uv0Meshes = 0u;
        std::size_t uv1Meshes = 0u;
        std::size_t indexedMeshes = 0u;
        std::size_t nonSequentialMeshes = 0u;
        std::size_t normalMeshes = 0u;
        std::size_t subsetCount = 0u;
        std::size_t referencedUvVertices = 0u;
        std::uint64_t resourceTriangles = 0u;
        float minimumU = std::numeric_limits<float>::infinity();
        float maximumU = -std::numeric_limits<float>::infinity();
        float minimumV = std::numeric_limits<float>::infinity();
        float maximumV = -std::numeric_limits<float>::infinity();

        for (const PhysicsSandboxRenderMesh &mesh : renderScene->meshes) {
            okay &= Check(!mesh.vertices.empty(),
                          "visual mesh has no vertices");
            okay &= Check(!mesh.indices.empty() &&
                                  mesh.indices.size() % 3u == 0u,
                          "visual mesh is not an indexed triangle list");
            okay &= Check(!mesh.subsets.empty(),
                          "visual mesh has no material range");
            resourceTriangles += mesh.indices.size() / 3u;
            indexedMeshes += !mesh.indices.empty();
            uv0Meshes += mesh.hasUv0;
            uv1Meshes += mesh.hasUv1;
            normalMeshes += mesh.hasNormals;
            subsetCount += mesh.subsets.size();

            bool nonSequential = false;
            std::vector<unsigned char> covered(mesh.indices.size(), 0u);
            for (const PhysicsSandboxRenderSubset &subset : mesh.subsets) {
                const std::size_t begin = subset.indexOffset;
                const std::size_t end = begin + subset.indexCount;
                okay &= Check(begin <= mesh.indices.size() &&
                                      end <= mesh.indices.size() &&
                                      subset.indexCount % 3u == 0u,
                              "material range is outside the index buffer");
                okay &= Check(subset.materialSlot == 0u,
                              "single-material visual used an invalid slot");
                if (end <= covered.size()) {
                    for (std::size_t index = begin; index < end; ++index) {
                        ++covered[index];
                    }
                }
            }
            okay &= Check(std::all_of(
                                  covered.cbegin(),
                                  covered.cend(),
                                  [](unsigned char count) {
                                      return count == 1u;
                                  }),
                          "material ranges do not partition the index buffer");

            for (std::size_t position = 0u;
                 position < mesh.indices.size();
                 ++position) {
                const std::uint32_t vertexIndex = mesh.indices[position];
                okay &= Check(vertexIndex < mesh.vertices.size(),
                              "visual index references a missing vertex");
                if (vertexIndex >= mesh.vertices.size()) continue;
                nonSequential |= vertexIndex != position;
                const PhysicsSandboxRenderVertex &vertex =
                        mesh.vertices[vertexIndex];
                okay &= Check(Finite(vertex.position),
                              "visual position is not finite");
                if (mesh.hasNormals) {
                    const float lengthSquared =
                            vertex.normal.x * vertex.normal.x +
                            vertex.normal.y * vertex.normal.y +
                            vertex.normal.z * vertex.normal.z;
                    okay &= Check(Finite(vertex.normal) &&
                                          lengthSquared > 0.01f,
                                  "authored visual normal is invalid");
                }
                if (mesh.hasUv0) {
                    okay &= Check(Finite(vertex.uv0.x) &&
                                          Finite(vertex.uv0.y),
                                  "indexed UV0 value is not finite");
                    minimumU = std::min(minimumU, vertex.uv0.x);
                    maximumU = std::max(maximumU, vertex.uv0.x);
                    minimumV = std::min(minimumV, vertex.uv0.y);
                    maximumV = std::max(maximumV, vertex.uv0.y);
                    ++referencedUvVertices;
                }
                if (mesh.hasUv1) {
                    okay &= Check(Finite(vertex.uv1.x) &&
                                          Finite(vertex.uv1.y),
                                  "indexed UV1 value is not finite");
                }
            }
            nonSequentialMeshes += nonSequential;
        }

        okay &= Check(uv0Meshes > 0u && referencedUvVertices > 1000u,
                      "TASmania exported no authored indexed UV0 data");
        okay &= Check(maximumU - minimumU > 0.1f &&
                              maximumV - minimumV > 0.1f,
                      "TASmania UV0 values do not vary");
        okay &= Check(normalMeshes > 0u,
                      "TASmania exported no authored normals");
        okay &= Check(nonSequentialMeshes > 0u,
                      "all visual index buffers were synthesized sequentially");
        okay &= Check(subsetCount >= renderScene->meshes.size(),
                      "visual material ranges are incomplete");

        std::vector<std::size_t> meshUses(renderScene->meshes.size(), 0u);
        std::vector<std::size_t> materialUses(
                renderScene->materials.size(), 0u);
        std::vector<PhysicsSandboxTransform> firstMeshTransforms(
                renderScene->meshes.size());
        std::vector<bool> hasFirstTransform(renderScene->meshes.size(), false);
        std::size_t authoredInstances = 0u;
        std::size_t placedBlockInstances = 0u;
        std::size_t transformedInstances = 0u;
        std::size_t sharedMeshInstances = 0u;
        bool repeatedMeshHasDifferentTransform = false;
        std::uint64_t instancedTriangles = 0u;

        for (const PhysicsSandboxRenderInstance &instance :
             renderScene->instances) {
            okay &= Check(instance.meshIndex < renderScene->meshes.size(),
                          "instance references a missing visual mesh");
            okay &= Check(instance.materialIndex <
                                  renderScene->materials.size(),
                          "instance references a missing material");
            okay &= Check(Finite(instance.worldTransform),
                          "instance world transform is not finite");
            if (instance.meshIndex >= renderScene->meshes.size() ||
                instance.materialIndex >= renderScene->materials.size()) {
                continue;
            }
            ++meshUses[instance.meshIndex];
            ++materialUses[instance.materialIndex];
            transformedInstances +=
                    std::fabs(instance.worldTransform.translation.x) > 0.001f ||
                    std::fabs(instance.worldTransform.translation.y) > 0.001f ||
                    std::fabs(instance.worldTransform.translation.z) > 0.001f;
            if (hasFirstTransform[instance.meshIndex]) {
                repeatedMeshHasDifferentTransform |= DifferentTransform(
                        firstMeshTransforms[instance.meshIndex],
                        instance.worldTransform);
            } else {
                firstMeshTransforms[instance.meshIndex] =
                        instance.worldTransform;
                hasFirstTransform[instance.meshIndex] = true;
            }
            authoredInstances += instance.provenance.authored &&
                    instance.provenance.placementIdentity.has_value() &&
                    !instance.provenance.blockName.empty();
            placedBlockInstances +=
                    instance.purpose ==
                            PhysicsSandboxScenePurpose::PlacedBlock ||
                    instance.purpose ==
                            PhysicsSandboxScenePurpose::SubMobil;
            if (instance.visible && instance.lodLevel == 0u) {
                instancedTriangles +=
                        renderScene->meshes[instance.meshIndex]
                                .indices.size() /
                        3u;
            }
        }
        for (std::size_t useCount : meshUses) {
            sharedMeshInstances += useCount > 1u ? useCount : 0u;
        }

        okay &= Check(sharedMeshInstances > 0u &&
                              repeatedMeshHasDifferentTransform,
                      "repeated placements do not share transformed meshes");
        okay &= Check(std::any_of(
                              materialUses.cbegin(),
                              materialUses.cend(),
                              [](std::size_t count) { return count > 1u; }),
                      "materials were duplicated per instance");
        okay &= Check(authoredInstances > 0u,
                      "authored block provenance was not exported");
        okay &= Check(placedBlockInstances > 0u,
                      "scene purpose does not identify block visuals");
        okay &= Check(transformedInstances > 0u,
                      "world transforms were not exported");
        okay &= Check(resourceTriangles > 0u && instancedTriangles > 0u,
                      "visual triangle counts are empty");
        okay &= Check(instancedTriangles !=
                              collisionScene.collisionTriangles.size(),
                      "visual scene aliases the collision triangle stream");

        std::cout << "render-scene-data: meshes=" << renderScene->meshes.size()
                  << ", instances=" << renderScene->instances.size()
                  << ", materials=" << renderScene->materials.size()
                  << ", subsets=" << subsetCount
                  << ", indexedMeshes=" << indexedMeshes
                  << ", nonSequentialMeshes=" << nonSequentialMeshes
                  << ", uv0Meshes=" << uv0Meshes
                  << ", uv1Meshes=" << uv1Meshes
                  << ", referencedUvVertices=" << referencedUvVertices
                  << ", authoredInstances=" << authoredInstances
                  << ", sharedMeshInstances=" << sharedMeshInstances
                  << ", resourceTriangles=" << resourceTriangles
                  << ", instancedTriangles=" << instancedTriangles
                  << ", collisionTriangles="
                  << collisionScene.collisionTriangles.size() << '\n';
        return okay ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
