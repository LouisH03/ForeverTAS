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

The search runs on a worker thread and can be cancelled without blocking the
interface. Each attempt applies the configured modifier passes in order,
normalizes the resulting input timeline, evaluates it with the selected target,
and restores the global best state at the end. Built-in targets cover finish
time, cuboid entry time, velocity, point distance, and weighted pose error.
Built-in modifiers cover existing-event perturbation, smooth steering
deformation, input insertion, input deletion, and random steering.

See `docs/SEARCH_COMPONENTS.md` for the registry, persistence, composition, and
extension contracts.
