# Textured map renderer

ForeverTAS reads ForeverValidator's immutable experimental render scene after a
replay load. That scene contains shared visual meshes, final material
references, lightweight instances, composed transforms, LOD/visibility data,
authored block provenance and structured diagnostics. Collision geometry
remains available through the pre-existing scene API.

## Material selection

`src/viewer/material_classifier.*` lowercases and combines the final material,
model, shader, sampler and bitmap paths. Ordered keyword rules select asphalt,
concrete, dirt, grass, metal, painted metal, plastic, rubber, glass, signage,
emissive, water, neutral or unknown. The rules never load game texture pixels.
They select the original PNGs under `assets/materials`. When exported paths do
not identify a surface, the authored surface-material id provides a stable
fallback category instead of collapsing the map into one unknown material.

Unknown materials use a conspicuous magenta replacement. Missing UV0 receives
a deterministic X/Z projection; missing normals are accumulated from indexed
triangles; missing tangents use a stable orthogonal basis. Water, cube maps,
render targets and complex shaders are reduced to deterministic replacement
material parameters. Glass and water use alpha blending, emissive materials
use an emissive factor, and thin classes disable culling.

## Qt Quick 3D

Each Validator mesh becomes one indexed `QQuick3DGeometry` with position,
normal, tangent, UV0, optional UV1, color and subset declarations. Instances
reuse those geometry objects and apply their own transform and material.
Static geometry is rebuilt only after a successful replay reload; playback
updates car transforms without touching map resources.

The viewer provides textured, neutral, collision, wireframe and material-debug
modes. Collision and wireframe continue to use the legacy collision buffers.
