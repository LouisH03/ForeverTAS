# Textured map renderer

ForeverTAS reads ForeverValidator's immutable experimental render scene after a
replay load. That scene contains shared visual meshes, final material
references, lightweight instances, composed transforms, LOD/visibility data,
authored block provenance and structured diagnostics. Collision geometry
remains available through the pre-existing scene API.

## Material selection

TASmania's exported materials have no source paths, so
`src/viewer/material_classifier.*` primarily combines the surface-material ID
with instance provenance: block name, purpose, component identity, component
index, and descriptor path. Explicit semantic overrides cover turbo,
checkpoint, start/finish, road, dirt road, grass, pool/water, signage, and
emissive components. Path keywords remain a secondary hint for scenes that
provide them.

The rules never load game texture pixels. They select photo-scanned CC0 PBR
materials and purpose-built ImageGen textures under `assets/materials`; every
source is listed in `assets/materials/PROVENANCE.md`. Turbo uses right-facing
cyan and yellow directional arrows, checkpoints use colored bands, and
start/finish components use a checker pattern. Concrete is a deliberately flat
gray. Broad, horizontal surface-0 meshes attached to authored blocks are
recognized as grass ground cover instead of inheriting the generic concrete
fallback. This makes gameplay surfaces recognizable even when every source
material path is empty. Dense grass-blade and grass-overlay meshes are removed
before batching; only opaque ground surfaces remain. Asphalt, grass, dirt, and
concrete also ignore source vertex-color tint because those channels encode
game-specific data that can turn replacement textures pale or orange.

Unknown materials use a conspicuous magenta replacement. Missing UV0 receives
a deterministic X/Z projection. Asphalt and ground materials instead use a
dominant-axis world-space projection with one texture tile every four meters.
This replaces the tiny authored UV spans that previously reduced a 32-meter
block to only a few sampled pixels, and keeps road centers, standalone grass,
block borders, and grass clips at the same density. Missing normals are
accumulated from indexed triangles; missing tangents use a stable orthogonal
basis. Water, cube maps, render targets and complex shaders are reduced to
deterministic replacement material parameters. Glass and water use alpha
blending, emissive materials use an emissive factor, and thin classes disable
culling.

## Qt Quick 3D

`src/viewer/visual_scene_pipeline.*` transforms static LOD0 geometry once on
load and batches compatible instances by replacement material, purpose, and
vertex-color mode. Transparent materials are additionally divided into
64-meter spatial cells. Each batch becomes one indexed `QQuick3DGeometry` with
preserved normals, tangents, UV0, UV1, colors, and material boundaries. Exact
duplicate mesh/material/purpose/transform tuples are suppressed.

The QML scene creates one `Model` per batch rather than one per source
instance. Material classes share a `PrincipledMaterial` and two `Texture`
objects. Static geometry is rebuilt only after a successful replay reload;
playback updates car transforms without touching map resources. Map shadows
are disabled by default.

The textured viewport uses Qt Quick 3D's procedural environment map as both a
blue daytime skybox and an image-based light probe. ACES tone mapping keeps the
sunlit concrete and emissive surfaces below clipping, while a warm directional
key and restrained cool fill keep the stadium readable without enabling costly
map shadows. The environment and light levels are covered by the QML smoke
test.

The default scene includes authored blocks, the enclosing stadium
environment, intentional generated stadium objects, and `StadiumGrassClip`
meshes that restore real block-side and ground-cover geometry. Dense blade
layers within those clips are still omitted. Other clips, checkpoint/start
helpers, triggers, and initial-collision geometry stay hidden.
On TASmania, the overlap audit found no exact duplicates, no cross-purpose
coincident transforms, and no coincident conflicting materials.

Camera near/far planes are recalculated from camera position, orbit distance,
and the default visible-scene bounds. There is no fixed 5000-unit minimum. The
near plane also tracks the far plane to maintain a useful depth ratio; the
56-kilometer enclosing stadium sky shell explains TASmania's large reset-view
far plane.

The viewer provides Textured, Neutral, Collision, Wireframe, and High Contrast
modes. Collision and Wireframe continue to use the legacy collision buffers.

## Verification

The runtime data smoke test loads a native replay through the installed pack,
then audits the immutable public render scene rather than internal builders. It
checks indexed visual meshes, authored UVs and normals, material ranges,
instance transforms, provenance, shared mesh reuse, purpose bounds, overlap
conflicts, and separation from the collision triangle stream. Renderer and
viewer tests additionally verify semantic overrides, clip-plane calculation,
default visibility, transformed batching, exact vertex attributes, packaged
replacement images, shared QML material and texture objects, every render mode,
and transactional repeated reloads.

After removing the blade overlays, the pinned TASmania build has 30 debuggable
batches, 260,116 default-visible triangles, and zero map shadows. The
interactive textured view measured 61-63 FPS on the development machine.

[`evidence/tasmania-textured.png`](evidence/tasmania-textured.png) is the
pre-optimization reference. The corrected textured frame is captured in
[`evidence/tasmania-textured-optimized.png`](evidence/tasmania-textured-optimized.png).
Restored grass clips and the corrected ground-cover material are shown in
[`evidence/tasmania-grass-clips.png`](evidence/tasmania-grass-clips.png).
The latter two captures use the final daytime environment and the `7.86s`
TASmania runtime frame.
