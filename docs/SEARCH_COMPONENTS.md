# Search Components Architecture

This document describes how ForeverTAS organizes configurable search
algorithms, mutation algorithms, and evaluation targets. It is intended as a
reference for contributors adding or maintaining search features.

## Feature Model

A search run is composed from three independently selected components:

1. **Search algorithm** — controls the search loop, branching strategy,
   evaluation schedule, acceptance rules, and result selection.
2. **Mutation algorithm** — transforms the baseline input timeline into a
   candidate input timeline for an attempt.
3. **Evaluation target** — scores a simulated state so the search algorithm can
   compare candidates.

Each component owns:

- A stable persisted ID.
- A user-facing display name.
- Its settings defaults.
- Its settings validation and typed parsing.
- Its runtime factory.
- Its QML settings component.
- Optional legacy persistence-key mappings.

The registry is the only place that connects all of those pieces.

## Directory Organization

```text
ForeverTAS/
├── src/
│   ├── searches/
│   │   ├── algorithm_registry.h
│   │   ├── algorithm_registry.cpp
│   │   ├── option_configuration.h
│   │   ├── option_settings_utils.h
│   │   ├── search_algorithm.h
│   │   ├── search_runner.h
│   │   ├── search_runner.cpp
│   │   ├── basic_brute_force_search.h
│   │   └── basic_brute_force_search.cpp
│   │
│   ├── mutations/
│   │   ├── input_mutator.h
│   │   ├── random_steering_mutator.h
│   │   └── random_steering_mutator.cpp
│   │
│   ├── evaluators/
│   │   ├── candidate_evaluator.h
│   │   ├── max_speed_evaluator.h
│   │   └── max_speed_evaluator.cpp
│   │
│   └── app/
│       ├── search_controller.h
│       ├── search_controller.cpp
│       ├── search_worker.h
│       └── search_worker.cpp
│
├── qml/
│   ├── Main.qml
│   └── settings/
│       ├── AlgorithmSelector.qml
│       ├── BasicBruteForceSearchSettings.qml
│       ├── RandomSteeringMutationSettings.qml
│       └── MaximumSpeedEvaluationSettings.qml
│
├── tests/
│   ├── strategy_tests.cpp
│   ├── controller_tests.cpp
│   ├── search_smoke.cpp
│   └── viewer_qml_smoke.cpp
│
└── CMakeLists.txt
```

## Core Contracts

### Option configuration

`src/searches/option_configuration.h` defines the transport format used between
the controller, worker, runner, and registry:

```cpp
using OptionSettings = std::map<std::string, std::string>;

struct OptionConfiguration {
    std::string id;
    OptionSettings settings;
};
```

Settings are transported as strings because they originate from editable UI
fields and must be persisted without coupling the controller to a specific
option. Each implementation parses its map into a typed settings structure
before construction.

The generic `SearchRequest` contains one `OptionConfiguration` for each
category:

```text
SearchRequest
├── Packs directory
├── Replay path
├── Search algorithm configuration
├── Mutation algorithm configuration
└── Evaluation target configuration
```

### Search algorithm interface

`SearchAlgorithm` receives a `SearchExecutionContext` containing:

- The loaded physics sandbox.
- The physics tick duration.
- The selected `InputMutator` instance.
- The selected `CandidateEvaluator` instance.
- Progress and cancellation callbacks.

The search algorithm owns the search lifecycle and returns a `SearchResult`.

### Mutation interface

`InputMutator::Mutate` receives attempt context shared by mutation algorithms:

- Baseline inputs.
- Mutation-window bounds selected by the search algorithm.
- Attempt index.

Algorithm-specific values such as a random seed, distribution, probability, or
step size belong in the concrete mutator instance. They are parsed from the
selected mutation configuration when the registry factory constructs it.

### Evaluation interface

`CandidateEvaluator::Evaluate` receives a physics-sandbox state and returns a
numeric score. Higher scores are currently treated as better by the basic
brute-force implementation.

Evaluation-specific configuration belongs in the concrete evaluator instance.

## Registry

`src/searches/algorithm_registry.*` is the central catalog for all selectable
components.

There are separate registration types for:

- Search algorithms.
- Mutation algorithms.
- Evaluation targets.

Every registration contains:

```text
id
displayName
settingsComponent
defaultSettings
legacyPersistenceKeys
validateSettings callback
create callback
```

### Stable IDs

IDs are persisted in user settings and transported in search requests. Treat
them as stable public identifiers.

Use lowercase, hyphen-separated IDs:

```text
basic-brute-force
random-steering
maximum-speed
```

Changing a display name is safe. Changing a released ID requires an explicit
selection migration.

### Default settings

Every configurable field must be present in `defaultSettings`.

The default map serves three purposes:

1. Defines the initial value for a new installation.
2. Defines the allowed setting keys.
3. Supplies the complete configuration passed to validation and construction.

Unknown keys are rejected by implementation validation and ignored by the
controller's generic setting-update methods.

### Legacy persistence keys

`legacyPersistenceKeys` maps a current option-owned key to an older global
QSettings key. It allows the controller to load existing user values after a
settings-ownership refactor.

Example:

```text
seed -> search/mutationSeed
```

New features should normally leave this map empty. Add entries when renaming or
moving settings that have already been released.

## Runtime Flow

The execution path is:

```text
QML option component
    ↓ generic category setting method
SearchController
    ↓ validates through registry callbacks
SearchRequest
    ↓ copied to worker thread
SearchWorker
    ↓
RunSearch
    ↓ registry lookup by stable IDs
Search algorithm factory
Mutation algorithm factory
Evaluation target factory
    ↓
SearchAlgorithm::Run
    ↓
SearchResult
    ↓
formatted UI summary
```

`RunSearch` does not switch on option IDs. It resolves registrations and invokes
their callbacks.

## Controller Responsibilities

`SearchController` owns only category-neutral state:

- Selected search, mutation, and evaluation IDs.
- One `QVariantMap` of settings per selected category.
- Generic update methods for a key/value pair in each category.
- Namespaced persistence.
- Registry-driven validation.
- Construction of the generic `SearchRequest`.

It must not gain properties or setters named after a particular algorithm's
settings.

The public QML-facing maps are:

```text
searchAlgorithmSettings
mutationAlgorithmSettings
evaluationTargetSettings
```

The corresponding update methods are:

```text
setSearchAlgorithmSetting(key, value)
setMutationAlgorithmSetting(key, value)
setEvaluationTargetSetting(key, value)
```

## Persistence

Selected IDs are stored separately:

```text
selection/searchAlgorithm
selection/mutationAlgorithm
selection/evaluationTarget
```

Option settings are namespaced by category and stable ID:

```text
configuration/search/<option-id>/<setting-key>
configuration/mutation/<option-id>/<setting-key>
configuration/evaluation/<option-id>/<setting-key>
```

Examples:

```text
configuration/search/basic-brute-force/attemptCount
configuration/mutation/random-steering/seed
```

This preserves each option's settings while the user switches between options
and prevents unrelated options from sharing keys accidentally.

## QML Ownership

`Main.qml` places three generic `AlgorithmSelector` instances. It does not know
which options exist and contains no option-specific settings fields.

`AlgorithmSelector.qml` receives:

- The category title.
- Registry metadata exposed by the controller.
- The selected ID.
- The controller object.

It renders the dropdown and loads the selected registration's
`settingsComponent` using a `Loader`.

An option-owned QML component:

- Declares `property var controller`.
- Reads only its category's settings map.
- Writes through the matching generic category update method.
- Contains every control specific to that option.

## Adding a Mutation Algorithm

The following example assumes a new mutation algorithm named **Steering
Jitter**, with stable ID `steering-jitter`.

### 1. Add the implementation files

Create:

```text
src/mutations/steering_jitter_mutator.h
src/mutations/steering_jitter_mutator.cpp
```

Implement `InputMutator`.

Define a typed settings structure in the mutation header, for example:

```text
SteeringJitterSettings
├── amplitude
├── probability
└── seed
```

The concrete mutator constructor should receive this typed structure and retain
it for `Mutate` calls.

### 2. Define option-owned configuration callbacks

Alongside the implementation, provide functions matching the mutation registry
contract:

```text
DefaultSteeringJitterOptionSettings()
ValidateSteeringJitterOptionSettings(settings)
CreateSteeringJitterMutator(settings)
```

The validation/factory path should:

1. Verify the exact allowed key set.
2. Parse string values into the typed settings structure.
3. Validate ranges and relationships.
4. Return an actionable error for invalid values.
5. Construct the mutator only from valid typed settings.

Reusable decimal parsing and exact-key validation helpers are available in
`src/searches/option_settings_utils.h`.

### 3. Register the mutation

Add a stable ID constant and a `MutationAlgorithmRegistration` entry in
`algorithm_registry.*`:

```text
ID: steering-jitter
Display name: Steering jitter
Settings component: SteeringJitterMutationSettings.qml
Defaults: all supported setting keys and initial values
Legacy keys: empty unless migrating released settings
Validator: ValidateSteeringJitterOptionSettings
Factory: CreateSteeringJitterMutator
```

Once registered, the option automatically appears in the mutation dropdown and
is available to `RunSearch`.

### 4. Add the owned QML settings component

Create:

```text
qml/settings/SteeringJitterMutationSettings.qml
```

Read values from:

```qml
controller.mutationAlgorithmSettings["amplitude"]
```

Write values through:

```qml
controller.setMutationAlgorithmSetting("amplitude", text)
```

Do not add Steering Jitter fields to `Main.qml` or `SearchController`.

Add the QML file to the `qt_add_qml_module` file list in `CMakeLists.txt`.

### 5. Add source files to CMake

Add the new mutation source and header to `forevertas_core`.

### 6. Add tests

Add coverage for:

- Default settings validation.
- Invalid value and unknown-key rejection.
- Registry lookup and factory construction.
- Deterministic output for identical settings and attempt indices.
- Expected variation across attempts.
- Respect for mutation-window boundaries.
- Accurate `mutationCount`.
- Persistence under the mutation option's namespace.
- Dropdown availability and loading of the owned QML component.

## Adding a Search Algorithm

Create implementation files under `src/searches/` and implement
`SearchAlgorithm`.

The implementation should own:

- A typed settings structure.
- Default option settings.
- String-map parsing and validation.
- A factory accepting the generic settings map and tick duration.
- Search-loop progress reporting and cancellation checks.

Register it with a `SearchAlgorithmRegistration`, add its QML component under
`qml/settings/`, and add all files to CMake.

Search-specific fields such as attempt count, beam width, population size,
branch time, or acceptance thresholds belong to this option's settings map and
typed settings structure.

## Adding an Evaluation Target

Create implementation files under `src/evaluators/` and implement
`CandidateEvaluator`.

Provide:

- Default option settings.
- Validation and typed parsing.
- A factory accepting the generic settings map.
- An owned QML settings component, even if it is currently an empty `Item`.

Register it with an `EvaluationTargetRegistration` and add its files to CMake.

Evaluation-specific fields such as target position, checkpoint index, score
weights, or time penalties belong to the evaluator's configuration and
instance.

## Testing Checklist

Before committing a new selectable component:

1. Build with the project's strict warning flags.
2. Run all CTest targets.
3. Run the Wayland/GPU QML viewer smoke test when available.
4. Run `git diff --check`.
5. Confirm the stable ID appears only in registry code and tests.
6. Confirm `Main.qml` contains no option-specific branch or settings control.
7. Confirm `SearchController` contains no option-specific property or setter.
8. Confirm the option's settings survive switching away and restarting.
9. Confirm invalid persisted values produce a clear validation message.
10. Confirm the registry factory accepts defaults and rejects invalid maps.

## Current Built-In Components

### Search algorithms

- `basic-brute-force` — evaluates the baseline and independently mutated
  attempts over a configurable time range.

### Mutation algorithms

- `random-steering` — replaces eligible analog steering events with
  deterministic pseudo-random values derived from its configured seed and the
  attempt index.

### Evaluation targets

- `maximum-speed` — scores the magnitude of the car's linear velocity.

