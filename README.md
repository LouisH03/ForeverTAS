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

## Serial brute-force exercise

Pass an installed TMUF `Packs` directory and a replay. No replay or game asset
is copied into this repository.

```sh
./build/local/forevertas \
  "/path/to/TmUnitedForever/Packs" \
  "/path/to/run.Replay.Gbx"
```

The executable creates a Reference sandbox and runs the first serial search
implementation. The search captures one state immediately before its configured
closed mutation window, establishes the unmodified replay as its baseline, then
tests each steering mutation serially. It evaluates maximum speed on every tick
of a separately configured closed evaluation window and restores the best state
at the end.

Mutation bounds, evaluation bounds, attempt count and deterministic seed are
explicit `SerialBruteForceSettings` in `src/app/main.cpp`. The initial executable
keeps concrete strategy selection equally direct: `RandomSteeringMutator`,
`MaxSpeedEvaluator`, and `SerialBruteForceSearch`. It does not yet provide a TAS
action model, scoring registry, pruning, or parallel search.
