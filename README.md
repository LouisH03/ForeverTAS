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
`84c6e49615cf545b69c1b94a7f49e089dc862991`. The embedded build disables the
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

The pinned dependency is extended by
`cmake/patches/forevervalidator-race-viewer.patch`. The patch is produced from
a worktree at the pinned revision and adds the immutable visual render-scene
API without requiring a local Validator checkout at build time.

## Desktop application

Build and launch the Qt 6 Quick application:

```sh
./build/local/bin/ForeverTAS
```

Select an installed TMUF `Packs` directory and a replay, enter a base input
script, choose an evaluation target, assemble an ordered list of input modifier
passes, then start the basic search. The replay supplies the map and scenario;
only the editable script supplies the player-control baseline. **Extract inputs
to script** imports the replay controls when that is the desired starting
point. The application persists paths, the script draft, selections, pass
order, and every option-owned configuration with the platform-native Qt
settings store. Search, replay loading, validation, and physics stay in C++;
QML presents the controls and Race Viewer.

The search runs indefinitely on a worker thread after Start is pressed. Each
iteration applies the configured modifier passes in order, preserves the
script-derived input prefix before the mutation branch exactly, normalizes only
the mutable suffix, and evaluates it with the selected target. Whenever a new
global best is found, its copy-ready input script is shown immediately.
Iteration count, iterations per second, elapsed time, and time since the last
improvement continue refreshing while the search runs. Pressing Stop
finishes the current
iteration, restores the global best, then performs one fresh full-replay
simulation and records one viewer sample per physics tick. The completed Best
run is added to the Race Viewer only after that Stop-triggered sampling pass.
Analog script and iteration inputs use the exact signed integer state range
`[-65536, 65536]`; normalized decimal UI settings are quantized once when
parsed. User-facing input timeline settings are zero-based: `0 ms` selects the
first actionable input, which is simulation time `10 ms` at 100 Hz. Absolute
setting keys ending in `TimeMs` are translated by one physics tick exactly once
when a registry creates a simulation component; stored values and relative
durations remain user-facing.
Built-in targets cover precise finish time, cuboid entry time, velocity,
point distance, and weighted pose error. Precise finish search ranks the
inclusive upper bound of ForeverValidator's one-nanosecond transition bracket
and displays all nine fractional digits.
Built-in modifiers cover existing-event perturbation, smooth steering
deformation, input insertion, input deletion, and random steering.

The complete visible settings pane owns vertical wheel scrolling, including
areas occupied by sliders, dropdowns, and the best-input preview. Nested
controls do not capture wheel input from the pane.

Map loads are serialized and transactional. The currently rendered scene stays
attached while a replacement replay's geometry and car shape are read, then the
scene is swapped only after the new map is complete. Loading a map does not
advance or publish the replay timeline. Timeline controls remain disabled until
a completed search adds the `Best` run.

The Race Viewer stores named search-result runs. A header selector switches the
active timeline and camera focus between `Best` and future run types, while
every run remains visible as a separate car in the 3D preview. Car colors are
baked into separate flat-shaded vertex-color meshes.

After a map is loaded, **Drive** starts a live 100 Hz physics run in the viewer.
Arrow keys and QWERTY `WASD` control full acceleration, braking, and steering;
`ZQSD` provides the equivalent bindings on AZERTY layouts. Simultaneous
digital inputs retain ForeverValidator's in-game priority rules, including
left steering over right. Losing keyboard focus releases held controls, and a
completed manual session remains available as the `Manual` viewer run.

**Copy current race** in the base-input section replaces the search input with
the selected viewer run through its current timeline position. Events after
that position are deliberately excluded, so a partial manual or scripted run
can become the exact starting point for the next search.

The default viewport is the textured Qt Quick 3D renderer. On Qt 6.7 or newer
with ShaderTools, the `Textured (RT)` render mode enables the real-time QRhi
compute renderer with GPU BVH traversal, ray-traced shadows and reflections,
and immediate noise-free output. Qt 6.5 and 6.6 keep the full raster renderer
and omit only that optional mode.

## Portable bundles

ForeverTAS can be packaged natively as a Linux AppImage, Windows portable ZIP,
or macOS application DMG. All three artifacts use the same CMake installation
definition and include the required Qt and QML runtime files.

See [docs/PACKAGING.md](docs/PACKAGING.md) for local packaging commands, CI
behavior, artifact layouts, signing notes, and clean-machine release checks.

See `docs/SEARCH_COMPONENTS.md` for the registry, persistence, composition, and
extension contracts.

See `docs/RENDERER.md` for visual-scene extraction, replacement materials,
fallbacks, caching, render modes, and asset ownership.
