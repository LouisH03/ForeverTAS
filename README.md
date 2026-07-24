# ForeverTAS

ForeverTAS is the TAS client for
[ForeverValidator](https://github.com/Skycrafter-dev/ForeverValidator).
ForeverValidator remains the source of truth for deterministic physics
and replay validation, used through the PhysicsSandbox API.

```text
ForeverTAS -> PhysicsSandbox -> ForeverValidator physics
```

## Dependency

CMake `FetchContent` pins ForeverValidator to the exact commit
`0a287aa7a0748faf5fffe9b8955505b8b500a5ce`. The embedded build disables the
ForeverValidator CLI and tests and links its native asset adapter and core
simulation library.

## Build

The pinned preset provides a reproducible build from the exact public
ForeverValidator commit:

```sh
cmake --preset pinned
cmake --build --preset pinned
ctest --preset pinned
```

For day-to-day development, create `CMakeUserPresets.json` from
`CMakeUserPresets.json.example` once and set
`FETCHCONTENT_SOURCE_DIR_FOREVERVALIDATOR` to the local ForeverValidator
checkout. This file is ignored because its path is machine-specific. The local
preset has its own build tree and always uses the current Validator worktree,
including uncommitted changes:

```sh
cmake --preset local-validator
cmake --build --preset local-validator
ctest --preset local-validator
```

The committed dependency hash only needs to change when ForeverTAS deliberately
adopts a tested ForeverValidator revision. Use the pinned preset as the final
pre-push check.

## Desktop application

Build and launch the Qt 6 Quick application:

```sh
./build/local/forevertas
```

Select an installed TMUF `Packs` directory and a replay, choose an evaluation
target, assemble an ordered list of input modifier passes, then start the basic
search. The application persists paths, selections, pass order, and every
option-owned configuration with the platform-native Qt settings store. Search,
replay loading, validation, and physics stay in C++; QML presents the controls
and Race Viewer.

The search runs indefinitely on a worker thread after Start is pressed. Each
iteration applies the configured modifier passes in order, preserves the replay
input prefix before the mutation branch exactly, normalizes only the mutable
suffix, and evaluates it with the selected target. Whenever a new global best
is found, its copy-ready input script is shown immediately.
Iteration count, iterations per second, elapsed time, and time since the last
improvement continue refreshing while the search runs. Pressing Stop
finishes the current
iteration, restores the global best, then performs one fresh full-replay
simulation and records one viewer sample per physics tick. The completed Best
run is added to the Race Viewer only after that Stop-triggered sampling pass.
Analog replay and iteration inputs use the exact signed integer state range
`[-65536, 65536]`; normalized decimal UI settings are quantized once when
parsed.
Built-in targets cover finish
time, cuboid entry time, velocity, point distance, and weighted pose error.
Built-in modifiers cover existing-event perturbation, smooth steering
deformation, input insertion, input deletion, and random steering.

The complete visible settings pane owns vertical wheel scrolling, including
areas occupied by sliders, dropdowns, and the best-input preview. Nested
controls do not capture wheel input from the pane.

Replay loads are serialized and transactional. The currently rendered scene
stays attached while a replacement replay is sampled, then track, run, and car
shape data are swapped only after the new scene is complete. This avoids
tearing down nested Qt Quick 3D repeaters between loads and keeps the car visible
across repeated replay and vehicle-family changes in one application instance.

The Race Viewer stores named runs. A centered selector switches the active
timeline and camera focus between `Baseline`, `Best`, and future run types,
while every run remains visible as a separate car in the 3D preview. Car colors
are baked into separate flat-shaded vertex-color meshes: Baseline preserves the
original orange palette exactly and Best uses the equivalent blue palette.

See `docs/SEARCH_COMPONENTS.md` for the registry, persistence, composition, and
extension contracts.
