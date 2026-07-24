#ifndef FOREVERTAS_SEARCHES_ALGORITHM_REGISTRY_H
#define FOREVERTAS_SEARCHES_ALGORITHM_REGISTRY_H

#include "evaluators/iteration_evaluator.h"
#include "mutations/input_mutator.h"
#include "searches/option_configuration.h"
#include "searches/search_algorithm.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace forevertas {

inline constexpr char kBasicBruteForceSearchId[] = "basic-brute-force";
inline constexpr char kRandomSteeringModifierId[] = "random-steering";
inline constexpr char kExistingEventPerturbationModifierId[] =
        "existing-event-perturbation";
inline constexpr char kSmoothSteeringModifierId[] = "smooth-steering";
inline constexpr char kInputInsertionModifierId[] = "input-insertion";
inline constexpr char kInputDeletionModifierId[] = "input-deletion";
inline constexpr char kVelocityEvaluationId[] = "velocity";
inline constexpr char kFinishTimeEvaluationId[] = "finish-time";
inline constexpr char kVolumeEntryEvaluationId[] = "volume-entry-time";
inline constexpr char kPointTargetEvaluationId[] = "point-target";
inline constexpr char kPoseTargetEvaluationId[] = "pose-target";

struct SearchAlgorithmRegistration {
    std::string id;
    std::vector<std::string> legacyIds;
    std::string displayName;
    std::string settingsComponent;
    OptionSettings defaultSettings;
    OptionSettings legacyPersistenceKeys;
    std::optional<std::string> (*validateSettings)(
            const OptionSettings &, std::uint32_t);
    std::unique_ptr<SearchAlgorithm> (*create)(
            const OptionSettings &, std::uint32_t);
};

struct ModifierRegistration {
    std::string id;
    std::vector<std::string> legacyIds;
    std::string displayName;
    std::string settingsComponent;
    OptionSettings defaultSettings;
    OptionSettings legacyPersistenceKeys;
    std::optional<std::string> (*validateSettings)(
            const OptionSettings &, std::uint32_t);
    std::unique_ptr<InputMutator> (*create)(
            const OptionSettings &, std::uint32_t);
};

struct EvaluationTargetRegistration {
    std::string id;
    std::vector<std::string> legacyIds;
    std::string displayName;
    std::string settingsComponent;
    OptionSettings defaultSettings;
    OptionSettings legacyPersistenceKeys;
    std::optional<std::string> (*validateSettings)(
            const OptionSettings &, std::uint32_t);
    std::unique_ptr<IterationEvaluator> (*create)(
            const OptionSettings &, std::uint32_t);
};

const std::vector<SearchAlgorithmRegistration> &SearchAlgorithmRegistry();
const std::vector<ModifierRegistration> &ModifierRegistry();
const std::vector<EvaluationTargetRegistration> &EvaluationTargetRegistry();

const SearchAlgorithmRegistration *FindSearchAlgorithm(
        const std::string &id);
const ModifierRegistration *FindModifier(
        const std::string &id);
const EvaluationTargetRegistration *FindEvaluationTarget(
        const std::string &id);

OptionConfiguration DefaultSearchAlgorithmConfiguration();
std::vector<OptionConfiguration> DefaultModifierConfigurations();
OptionConfiguration DefaultEvaluationTargetConfiguration();

}  // namespace forevertas

#endif
