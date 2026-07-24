# Search Components Architecture

This document describes how ForeverTAS organizes configurable search
algorithms, ordered input modifiers, and evaluation targets. It is the primary
reference for adding search features without coupling the controller or main
QML file to individual implementations.

## Feature Model

A search request contains four parts:

1. The Packs directory and replay path.
2. One selected search algorithm.
3. An ordered list of configured modifier passes.
4. One selected evaluation target.

The application currently requires at least one modifier pass before a search
can start.

Each selectable implementation owns:

- A stable ID and display name.
- Its complete default settings map.
- Typed parsing and validation.
- Its runtime factory.
- Its QML settings component.
- Optional aliases and legacy persistence mappings.

The registry is the only source that connects these pieces. `Main.qml`,
`SearchController`, and `RunSearch` do not switch on implementation IDs.

## Directory Organization

```text
ForeverTAS/
├── src/
│   ├── searches/
│   │   ├── algorithm_registry.h/.cpp
│   │   ├── option_configuration.h
│   │   ├── option_settings_utils.h
│   │   ├── search_algorithm.h
│   │   ├── search_runner.h/.cpp
│   │   └── basic_brute_force_search.h/.cpp
│   │
│   ├── mutations/
│   │   ├── input_mutator.h
│   │   ├── input_event_utils.h/.cpp
│   │   ├── modifier_utils.h
│   │   ├── composite_input_mutator.h/.cpp
│   │   ├── random_steering_mutator.h/.cpp
│   │   ├── existing_event_perturbation_mutator.h/.cpp
│   │   ├── smooth_steering_mutator.h/.cpp
│   │   ├── input_insertion_mutator.h/.cpp
│   │   └── input_deletion_mutator.h/.cpp
│   │
│   ├── evaluators/
│   │   ├── candidate_evaluator.h
│   │   ├── evaluator_utils.h
│   │   ├── finish_time_evaluator.h/.cpp
│   │   ├── volume_entry_evaluator.h/.cpp
│   │   ├── velocity_evaluator.h/.cpp
│   │   ├── point_target_evaluator.h/.cpp
│   │   └── pose_target_evaluator.h/.cpp
│   │
│   └── app/
│       ├── search_configuration_model.h/.cpp
│       ├── search_controller.h/.cpp
│       └── search_worker.h/.cpp
│
├── qml/
│   ├── Main.qml
│   └── settings/
│       ├── AlgorithmSelector.qml
│       ├── ModifierComposition.qml
│       ├── SettingTextField.qml
│       ├── SettingSwitch.qml
│       ├── SettingCombo.qml
│       ├── TimeWindowSettings.qml
│       ├── Vector3Settings.qml
│       └── one owned component per selectable implementation
│
├── tests/
│   ├── search_component_tests.cpp
│   ├── search_controller_tests.cpp
│   ├── search_smoke.cpp
│   ├── viewer_smoke.cpp
│   └── viewer_qml_smoke.cpp
│
└── CMakeLists.txt
```

## Generic Configuration Transport

`src/searches/option_configuration.h` defines the category-neutral transport
format:

```cpp
using OptionSettings = std::map<std::string, std::string>;

struct OptionConfiguration {
    std::string id;
    OptionSettings settings;
};
```

Values remain strings while they are edited and persisted. The implementation
that owns the option parses them into a typed structure during validation and
construction.

`SearchRequest` contains:

```text
SearchRequest
├── packDirectory
├── replayPath
├── searchAlgorithm: OptionConfiguration
├── modifiers: vector<OptionConfiguration>
└── evaluationTarget: OptionConfiguration
```

Repeated modifier IDs are allowed. Each vector entry is an independent pass
with its own settings.

## Registry Contract

`src/searches/algorithm_registry.*` contains three registries:

- `SearchAlgorithmRegistry()`
- `ModifierRegistry()`
- `EvaluationTargetRegistry()`

Every registration contains:

```text
id
legacyIds
displayName
settingsComponent
defaultSettings
legacyPersistenceKeys
validateSettings callback
create callback
```

Stable IDs use lowercase hyphen-separated names. Released IDs must not be
silently reused for a different behavior. Renames require an alias in
`legacyIds`; the controller canonicalizes persisted aliases back to the current
ID.

`defaultSettings` is also the allowed key set. The controller ignores update
requests for unknown keys, while implementation validation rejects incomplete
or extra maps received outside the controller.

## Search Algorithm Contract

`SearchAlgorithm` receives a `SearchExecutionContext` containing:

- A loaded `PhysicsSandbox`.
- The physics tick duration.
- The composed `InputMutator` pipeline.
- The selected `CandidateEvaluator`.
- Progress and cancellation callbacks.

The Basic bruteforce implementation owns only attempt scheduling and global
winner selection. It does not own modifier windows, seeds, evaluation windows,
or comparison direction.

It asks:

- The modifier pipeline for its earliest affected input time.
- The evaluation target for its observation plan.
- The evaluation target whether a sample is better than the incumbent.

This keeps search orchestration independent from every target and modifier ID.

## Modifier Contract

`InputMutator::Mutate` receives:

- The current input timeline entering that pass.
- The attempt index.
- The pass index.
- The physics tick duration.

Each modifier instance owns its own active window, seed, channel selection,
and modification parameters. `EarliestMutationTimeMs()` reports the earliest
input tick that the pass may change. `CompositeInputMutator` uses the minimum
across all passes.

### Composition

Modifier passes run in displayed order. Each pass receives the previous pass's
output. After the final pass, the composite mutator performs one normalization
step:

- Clamp steering to `[-1, 1]`.
- Align event times to whole simulation ticks.
- Sort events chronologically with stable ordering.
- For multiple events with the same action and tick, keep the last pass value.
- Count effective differences from the original baseline.

If no effective change remains after normalization, the attempt is skipped.

Deterministic random streams are derived from:

```text
configured seed + attempt index + pass index
```

This keeps repeated runs reproducible while allowing repeated instances of the
same modifier to produce independent changes.

## Evaluation Target Contract

Evaluation is timeline-based rather than a single stateless score function.

`CandidateEvaluator` owns:

- `Plan(...)`: the closed observation window for a replay and modifier branch.
- `CreateSession()`: per-candidate timeline state.
- `IsBetter(...)`: maximize or minimize semantics.

Each observed result is an `EvaluationSample`:

```text
score
timeMs
description
```

The description is displayed directly in the result summary, so targets own
their metric wording.

Timeline sessions receive the previous and current sandbox states. This lets
transition targets, such as entering a volume, interpolate crossing time
between ticks without adding target-specific logic to the search algorithm.

## Controller Responsibilities

`SearchConfigurationModel` owns the generic component configuration state:

- Selected search ID and search settings map.
- Ordered modifier-pass list.
- Selected evaluation ID and evaluation settings map.
- Generic add/remove/move/type/setting methods for modifier passes.
- Registry-driven validation.
- Generic persistence and request construction.

`SearchController` owns application coordination:

- Worker-thread lifecycle, paths, status, and progress.
- QML properties and change notifications that delegate to the configuration
  model.

Neither class may gain a field, property, or method named after a concrete
target or modifier.

The QML-facing composition API is:

```text
modifierOptions
modifierPasses
addModifierPass(id)
removeModifierPass(index)
moveModifierPass(fromIndex, toIndex)
setModifierPassId(index, id)
setModifierPassSetting(index, key, value)
```

## Persistence

Search and evaluation selections use:

```text
selection/searchAlgorithm
selection/evaluationTarget
```

Their settings remain namespaced by category and option ID:

```text
configuration/search/<id>/<key>
configuration/evaluation/<id>/<key>
```

The ordered modifier composition is stored as compact JSON under:

```text
composition/modifiers
```

Each JSON entry contains its modifier ID and complete settings object. This
preserves order, repeated modifier types, and independent values.

Older single-modifier settings are migrated only when no composition JSON is
present. Legacy mappings stay in registry metadata or narrowly named migration
constants; they must not appear as selectable options.

## QML Ownership

`Main.qml` places one generic search selector, one generic modifier composition
editor, and one generic evaluation selector.

`AlgorithmSelector` loads the selected option's `settingsComponent` directly
from registry metadata.

`ModifierComposition` renders the persisted pass order, provides add/remove/
move controls, and loads each pass's settings component. It supplies a pass
component with:

```qml
property var settings
property var updateSetting
property bool running
```

A modifier QML file reads only its provided settings map and writes only via
`updateSetting(key, value)`.

Search and evaluation components receive `property var controller` and use only
their corresponding generic settings map and update method.

Reusable field/layout components belong in `qml/settings/`; implementation
logic and implementation-specific field lists belong in the owned component.

## Adding a Modifier

Assume a new modifier named **Steering Jitter** with ID `steering-jitter`.

1. Create `src/mutations/steering_jitter_mutator.h/.cpp`.
2. Implement `InputMutator`, including `EarliestMutationTimeMs()`.
3. Define a typed settings structure owned by the modifier.
4. Provide defaults, validation, and a factory matching
   `ModifierRegistration`.
5. Reject unknown keys and parse every default key.
6. Use the shared deterministic RNG helpers when randomness is involved.
7. Add one `ModifierRegistration` entry.
8. Create `qml/settings/SteeringJitterSettings.qml` using `settings`,
   `updateSetting`, and `running`.
9. Add source and QML files to CMake.
10. Test validation, deterministic output, boundaries, normalization,
    registry construction, persistence, and QML loading.

No change to `Main.qml`, `SearchController`, `SearchRequest`, or
`BasicBruteForceSearch` should be needed.

## Adding an Evaluation Target

1. Create target files under `src/evaluators/`.
2. Implement `CandidateEvaluator` and a per-candidate session.
3. Define the target's observation plan and comparison direction.
4. Provide defaults, validation, and a factory.
5. Return a clear target-owned `EvaluationSample::description`.
6. Add one `EvaluationTargetRegistration` entry.
7. Create and register the owned QML component.
8. Test the metric with synthetic state sequences, including transition and
   interpolation cases where relevant.

No search-loop or controller branch should be added for the target.

## Adding a Search Algorithm

1. Implement `SearchAlgorithm` under `src/searches/`.
2. Keep only search-policy settings in its typed structure.
3. Consume the generic mutator and evaluator contracts.
4. Report progress and check cancellation regularly.
5. Provide defaults, validation, factory, registry entry, and QML component.
6. Test default construction and algorithm-specific scheduling behavior.

## Current Built-In Components

### Search algorithms

- `basic-brute-force`: baseline plus independent deterministic attempts.

### Modifiers

- `random-steering`: replaces existing steering values in a window.
- `existing-event-perturbation`: perturbs selected existing event values and
  times.
- `smooth-steering`: adds raised-cosine steering deformations.
- `input-insertion`: inserts steering, accelerate, or brake segments.
- `input-deletion`: deletes eligible events per channel.

### Evaluation targets

- `velocity`: total or projected velocity with optional alignment threshold.
- `finish-time`: minimizes the recorded race finish time.
- `volume-entry-time`: minimizes interpolated entry time into a cuboid.
- `point-target`: minimizes distance to a target point over a window.
- `pose-target`: minimizes weighted position and orientation error.

## Testing Checklist

Before submitting a new component:

1. Build with the strict warning flags.
2. Run all CTest targets.
3. Run the real Wayland/GPU viewer smoke test when available.
4. Run a real replay search smoke test.
5. Run `git diff --check`.
6. Confirm IDs appear only in registry code, migration tests, and registry
   assertions.
7. Confirm no ID switch or option-specific controller field was introduced.
8. Confirm every option owns defaults, validation, factory, persistence
   metadata, and QML.
9. Confirm repeated modifier instances preserve independent settings and order.
10. Confirm invalid settings produce actionable messages.
