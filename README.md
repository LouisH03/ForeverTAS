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
`f45744128b6274df65c9be586ae46a1010d2cf8c`. The embedded build disables the
ForeverValidator CLI and tests and links its native asset adapter and core
simulation library.

## Build

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The pinned ForeverValidator commit must be available from its public remote.
Validator developers can exercise an unpublished local commit without changing
the pin:

```sh
cmake -S . -B build \
  -DFETCHCONTENT_SOURCE_DIR_FOREVERVALIDATOR=/path/to/ForeverValidator
```

## Sandbox smoke exercise before full TAS functaionality is integrated.

Pass an installed TMUF `Packs` directory and a replay. No replay or game asset
is copied into this repository.

```sh
./build/forevertas \
  "/path/to/TmUnitedForever/Packs" \
  "/path/to/run.Replay.Gbx"
```

The executable creates a Reference sandbox, loads the replay-backed scenario,
reads its input timeline and initial state, advances several ticks, snapshots,
restores, repeats the same advance, and requires exact state equality.