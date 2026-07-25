#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <set>
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

Vector3 TransformPoint(const PhysicsSandboxTransform &transform,
                       const Vector3 &point) {
    return {transform.translation.x + transform.basisX.x * point.x +
                    transform.basisY.x * point.y + transform.basisZ.x * point.z,
            transform.translation.y + transform.basisX.y * point.x +
                    transform.basisY.y * point.y + transform.basisZ.y * point.z,
            transform.translation.z + transform.basisX.z * point.x +
                    transform.basisY.z * point.y +
                    transform.basisZ.z * point.z};
}

void ExpandBounds(const Vector3 &point, Vector3 &minimum, Vector3 &maximum) {
    minimum.x = std::min(minimum.x, point.x);
    minimum.y = std::min(minimum.y, point.y);
    minimum.z = std::min(minimum.z, point.z);
    maximum.x = std::max(maximum.x, point.x);
    maximum.y = std::max(maximum.y, point.y);
    maximum.z = std::max(maximum.z, point.z);
}

const char *PurposeName(PhysicsSandboxScenePurpose purpose) {
    switch (purpose) {
    case PhysicsSandboxScenePurpose::Environment:
        return "Environment";
    case PhysicsSandboxScenePurpose::PlacedBlock:
        return "PlacedBlock";
    case PhysicsSandboxScenePurpose::SubMobil:
        return "SubMobil";
    case PhysicsSandboxScenePurpose::Clip:
        return "Clip";
    case PhysicsSandboxScenePurpose::Helper:
        return "Helper";
    case PhysicsSandboxScenePurpose::CheckpointTrigger:
        return "CheckpointTrigger";
    case PhysicsSandboxScenePurpose::DedicatedInitialCollision:
        return "DedicatedInitialCollision";
    case PhysicsSandboxScenePurpose::Pylon:
        return "Pylon";
    case PhysicsSandboxScenePurpose::Decoration:
        return "Decoration";
    case PhysicsSandboxScenePurpose::Terrain:
        return "Terrain";
    case PhysicsSandboxScenePurpose::Generated:
        return "Generated";
    }
    return "Unknown";
}

std::string GeometryPlacementKey(const PhysicsSandboxRenderInstance &instance) {
    std::string key;
    key.reserve(sizeof(instance.meshIndex) + sizeof(instance.worldTransform));
    key.append(reinterpret_cast<const char *>(&instance.meshIndex),
               sizeof(instance.meshIndex));
    key.append(reinterpret_cast<const char *>(&instance.worldTransform),
               sizeof(instance.worldTransform));
    return key;
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
        std::uint64_t instancedVertices = 0u;
        struct PurposeStats {
            std::size_t instances = 0u;
            std::size_t visibleLod0 = 0u;
            std::uint64_t triangles = 0u;
            std::set<std::uint32_t> meshes;
            std::set<std::uint32_t> materials;
            std::set<std::string> blocks;
            std::set<std::string> descriptors;
            std::set<std::string> sceneObjects;
            Vector3 boundsMin{};
            Vector3 boundsMax{};
            bool hasBounds = false;
        };
        std::map<PhysicsSandboxScenePurpose, PurposeStats> purposeStats;
        std::map<std::uint8_t, PurposeStats> surfaceStats;
        struct BlockStats {
            std::size_t instances = 0u;
            std::uint64_t triangles = 0u;
            std::set<PhysicsSandboxScenePurpose> purposes;
            std::set<std::uint8_t> surfaces;
        };
        std::map<std::string, BlockStats> blockStats;
        std::map<std::string, PurposeStats> sceneObjectStats;
        struct PlacementStats {
            std::size_t count = 0u;
            std::set<PhysicsSandboxScenePurpose> purposes;
            std::set<std::uint32_t> materials;
            std::set<std::string> blocks;
        };
        std::map<std::string, PlacementStats> geometryPlacements;

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
                const std::uint64_t triangles =
                        renderScene->meshes[instance.meshIndex].indices.size() /
                        3u;
                instancedTriangles += triangles;
                instancedVertices +=
                        renderScene->meshes[instance.meshIndex].vertices.size();
                PurposeStats &purpose = purposeStats[instance.purpose];
                ++purpose.visibleLod0;
                purpose.triangles += triangles;
                const std::uint8_t surfaceId =
                        renderScene->materials[instance.materialIndex]
                                .surfaceMaterialId;
                PurposeStats &surface = surfaceStats[surfaceId];
                ++surface.visibleLod0;
                surface.triangles += triangles;
                PurposeStats *const sceneObject =
                        instance.provenance.sceneObjectId.empty()
                                ? nullptr
                                : &sceneObjectStats[instance.provenance
                                                            .sceneObjectId];
                if (sceneObject != nullptr) {
                    ++sceneObject->visibleLod0;
                    sceneObject->triangles += triangles;
                    sceneObject->meshes.insert(instance.meshIndex);
                    sceneObject->materials.insert(instance.materialIndex);
                }
                const PhysicsSandboxRenderMesh &mesh =
                        renderScene->meshes[instance.meshIndex];
                for (int corner = 0; corner < 8; ++corner) {
                    const Vector3 local{(corner & 1) != 0 ? mesh.boundsMax.x
                                                          : mesh.boundsMin.x,
                                        (corner & 2) != 0 ? mesh.boundsMax.y
                                                          : mesh.boundsMin.y,
                                        (corner & 4) != 0 ? mesh.boundsMax.z
                                                          : mesh.boundsMin.z};
                    const Vector3 world =
                            TransformPoint(instance.worldTransform, local);
                    for (PurposeStats *stats : {&purpose, sceneObject}) {
                        if (stats == nullptr)
                            continue;
                        if (!stats->hasBounds) {
                            stats->boundsMin = world;
                            stats->boundsMax = world;
                            stats->hasBounds = true;
                        } else {
                            ExpandBounds(world, stats->boundsMin,
                                         stats->boundsMax);
                        }
                    }
                }

                PlacementStats &placement =
                        geometryPlacements[GeometryPlacementKey(instance)];
                ++placement.count;
                placement.purposes.insert(instance.purpose);
                placement.materials.insert(instance.materialIndex);
                if (!instance.provenance.blockName.empty()) {
                    placement.blocks.insert(instance.provenance.blockName);
                }
            }
            PurposeStats &purpose = purposeStats[instance.purpose];
            ++purpose.instances;
            purpose.meshes.insert(instance.meshIndex);
            purpose.materials.insert(instance.materialIndex);
            if (!instance.provenance.blockName.empty()) {
                purpose.blocks.insert(instance.provenance.blockName);
            }
            if (!instance.provenance.descriptorPath.empty()) {
                purpose.descriptors.insert(instance.provenance.descriptorPath);
            }
            if (!instance.provenance.sceneObjectId.empty()) {
                purpose.sceneObjects.insert(instance.provenance.sceneObjectId);
            }
            const std::uint8_t surfaceId =
                    renderScene->materials[instance.materialIndex]
                            .surfaceMaterialId;
            PurposeStats &surface = surfaceStats[surfaceId];
            ++surface.instances;
            surface.meshes.insert(instance.meshIndex);
            surface.materials.insert(instance.materialIndex);
            if (!instance.provenance.blockName.empty()) {
                surface.blocks.insert(instance.provenance.blockName);
            }
            BlockStats &block =
                    blockStats[instance.provenance.blockName.empty()
                                       ? std::string("(none)")
                                       : instance.provenance.blockName];
            ++block.instances;
            block.purposes.insert(instance.purpose);
            block.surfaces.insert(surfaceId);
            if (instance.visible && instance.lodLevel == 0u) {
                block.triangles +=
                        renderScene->meshes[instance.meshIndex].indices.size() /
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
                  << ", uv0Meshes=" << uv0Meshes << ", uv1Meshes=" << uv1Meshes
                  << ", referencedUvVertices=" << referencedUvVertices
                  << ", authoredInstances=" << authoredInstances
                  << ", sharedMeshInstances=" << sharedMeshInstances
                  << ", resourceTriangles=" << resourceTriangles
                  << ", instancedVertices=" << instancedVertices
                  << ", instancedTriangles=" << instancedTriangles
                  << ", collisionTriangles="
                  << collisionScene.collisionTriangles.size() << '\n';
        for (const auto &[purpose, stats] : purposeStats) {
            std::cout << "purpose: name=" << PurposeName(purpose)
                      << ", instances=" << stats.instances
                      << ", visibleLod0=" << stats.visibleLod0
                      << ", meshes=" << stats.meshes.size()
                      << ", materials=" << stats.materials.size()
                      << ", blocks=" << stats.blocks.size()
                      << ", descriptors=" << stats.descriptors.size()
                      << ", sceneObjects=" << stats.sceneObjects.size()
                      << ", triangles=" << stats.triangles;
            if (stats.hasBounds) {
                std::cout << ", bounds=" << stats.boundsMin.x << ":"
                          << stats.boundsMin.y << ":" << stats.boundsMin.z
                          << ".." << stats.boundsMax.x << ":"
                          << stats.boundsMax.y << ":" << stats.boundsMax.z;
            }
            if (!stats.descriptors.empty()) {
                std::cout << ", descriptorNames=";
                bool first = true;
                for (const std::string &descriptor : stats.descriptors) {
                    std::cout << (first ? "" : "|") << descriptor;
                    first = false;
                }
            }
            if (!stats.sceneObjects.empty()) {
                std::cout << ", sceneObjectNames=";
                bool first = true;
                for (const std::string &object : stats.sceneObjects) {
                    std::cout << (first ? "" : "|") << object;
                    first = false;
                }
            }
            std::cout << '\n';
        }
        for (const auto &[objectName, stats] : sceneObjectStats) {
            std::cout << "scene-object: name=" << objectName
                      << ", visibleLod0=" << stats.visibleLod0
                      << ", triangles=" << stats.triangles
                      << ", bounds=" << stats.boundsMin.x << ":"
                      << stats.boundsMin.y << ":" << stats.boundsMin.z << ".."
                      << stats.boundsMax.x << ":" << stats.boundsMax.y << ":"
                      << stats.boundsMax.z << '\n';
        }
        for (const auto &[surfaceId, stats] : surfaceStats) {
            std::cout << "surface: id=" << static_cast<unsigned int>(surfaceId)
                      << ", instances=" << stats.instances
                      << ", visibleLod0=" << stats.visibleLod0
                      << ", meshes=" << stats.meshes.size()
                      << ", materials=" << stats.materials.size()
                      << ", blocks=" << stats.blocks.size()
                      << ", triangles=" << stats.triangles;
            if (surfaceId == 7u || surfaceId == 26u || surfaceId == 30u) {
                std::cout << ", turboBlocks=";
                bool first = true;
                for (const std::string &block : stats.blocks) {
                    std::cout << (first ? "" : "|") << block;
                    first = false;
                }
            }
            std::cout << '\n';
        }
        for (const auto &[blockName, stats] : blockStats) {
            std::cout << "block: name=" << blockName
                      << ", instances=" << stats.instances
                      << ", triangles=" << stats.triangles << ", purposes=";
            bool first = true;
            for (PhysicsSandboxScenePurpose purpose : stats.purposes) {
                std::cout << (first ? "" : "|") << PurposeName(purpose);
                first = false;
            }
            std::cout << ", surfaces=";
            first = true;
            for (std::uint8_t surfaceId : stats.surfaces) {
                std::cout << (first ? "" : "|")
                          << static_cast<unsigned int>(surfaceId);
                first = false;
            }
            std::cout << '\n';
        }
        std::size_t duplicatePlacements = 0u;
        std::size_t crossPurposePlacements = 0u;
        std::size_t conflictingMaterialPlacements = 0u;
        for (const auto &[key, stats] : geometryPlacements) {
            static_cast<void>(key);
            if (stats.count > 1u) {
                duplicatePlacements += stats.count - 1u;
                crossPurposePlacements += stats.purposes.size() > 1u;
                conflictingMaterialPlacements += stats.materials.size() > 1u;
            }
        }
        std::cout << "overlap-audit: exactDuplicateInstances="
                  << duplicatePlacements
                  << ", crossPurposePlacements=" << crossPurposePlacements
                  << ", conflictingMaterialPlacements="
                  << conflictingMaterialPlacements << '\n';
        return okay ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
