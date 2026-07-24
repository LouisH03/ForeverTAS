#include "searches/algorithm_registry.h"

#include "evaluators/finish_time_evaluator.h"
#include "evaluators/point_target_evaluator.h"
#include "evaluators/pose_target_evaluator.h"
#include "evaluators/velocity_evaluator.h"
#include "evaluators/volume_entry_evaluator.h"
#include "mutations/existing_event_perturbation_mutator.h"
#include "mutations/input_deletion_mutator.h"
#include "mutations/input_insertion_mutator.h"
#include "mutations/random_steering_mutator.h"
#include "mutations/smooth_steering_mutator.h"
#include "searches/basic_brute_force_search.h"

#include <algorithm>

namespace forevertas {
namespace {

template<typename Registration>
const Registration *FindRegistration(
        const std::vector<Registration> &registrations,
        const std::string &id) {
    const auto found = std::find_if(
            registrations.begin(),
            registrations.end(),
            [&id](const Registration &registration) {
                return registration.id == id ||
                        std::find(registration.legacyIds.begin(),
                                  registration.legacyIds.end(),
                                  id) != registration.legacyIds.end();
            });
    return found == registrations.end() ? nullptr : &*found;
}

}  // namespace

const std::vector<SearchAlgorithmRegistration> &SearchAlgorithmRegistry() {
    static const std::vector<SearchAlgorithmRegistration> registrations{
            {kBasicBruteForceSearchId,
             {std::string("seri" "al-brute-force")},
             "Basic bruteforce",
             "BasicBruteForceSearchSettings.qml",
             DefaultBasicBruteForceOptionSettings(),
             {},
             &ValidateBasicBruteForceOptionSettings,
             &CreateBasicBruteForceSearch}};
    return registrations;
}

const std::vector<ModifierRegistration> &ModifierRegistry() {
    static const std::vector<ModifierRegistration> registrations{
            {kRandomSteeringModifierId,
             {},
             "Random steering",
             "RandomSteeringMutationSettings.qml",
             DefaultRandomSteeringOptionSettings(),
             {{"minTimeMs", "search/minMutateMs"},
              {"maxTimeMs", "search/maxMutateMs"},
              {"seed", "search/mutationSeed"}},
             &ValidateRandomSteeringOptionSettings,
             &CreateRandomSteeringMutator},
            {kExistingEventPerturbationModifierId,
             {},
             "Existing-event perturbation",
             "ExistingEventPerturbationSettings.qml",
             DefaultExistingEventPerturbationSettings(),
             {},
             &ValidateExistingEventPerturbationSettings,
             &CreateExistingEventPerturbationMutator},
            {kSmoothSteeringModifierId,
             {},
             "Smooth steering deformation",
             "SmoothSteeringSettings.qml",
             DefaultSmoothSteeringSettings(),
             {},
             &ValidateSmoothSteeringSettings,
             &CreateSmoothSteeringMutator},
            {kInputInsertionModifierId,
             {},
             "Input insertion",
             "InputInsertionSettings.qml",
             DefaultInputInsertionSettings(),
             {},
             &ValidateInputInsertionSettings,
             &CreateInputInsertionMutator},
            {kInputDeletionModifierId,
             {},
             "Input deletion",
             "InputDeletionSettings.qml",
             DefaultInputDeletionSettings(),
             {},
             &ValidateInputDeletionSettings,
             &CreateInputDeletionMutator}};
    return registrations;
}

const std::vector<EvaluationTargetRegistration> &EvaluationTargetRegistry() {
    static const std::vector<EvaluationTargetRegistration> registrations{
            {kVelocityEvaluationId,
             {"maximum-speed"},
             "Velocity",
             "VelocityEvaluationSettings.qml",
             DefaultVelocityOptionSettings(),
             {},
             &ValidateVelocityOptionSettings,
             &CreateVelocityEvaluator},
            {kFinishTimeEvaluationId,
             {},
             "Finish time",
             "FinishTimeEvaluationSettings.qml",
             DefaultFinishTimeOptionSettings(),
             {},
             &ValidateFinishTimeOptionSettings,
             &CreateFinishTimeEvaluator},
            {kVolumeEntryEvaluationId,
             {},
             "Volume entry time",
             "VolumeEntryEvaluationSettings.qml",
             DefaultVolumeEntryOptionSettings(),
             {},
             &ValidateVolumeEntryOptionSettings,
             &CreateVolumeEntryEvaluator},
            {kPointTargetEvaluationId,
             {},
             "Point target",
             "PointTargetEvaluationSettings.qml",
             DefaultPointTargetOptionSettings(),
             {},
             &ValidatePointTargetOptionSettings,
             &CreatePointTargetEvaluator},
            {kPoseTargetEvaluationId,
             {},
             "Pose target",
             "PoseTargetEvaluationSettings.qml",
             DefaultPoseTargetOptionSettings(),
             {},
             &ValidatePoseTargetOptionSettings,
             &CreatePoseTargetEvaluator}};
    return registrations;
}

const SearchAlgorithmRegistration *FindSearchAlgorithm(
        const std::string &id) {
    return FindRegistration(SearchAlgorithmRegistry(), id);
}

const ModifierRegistration *FindModifier(
        const std::string &id) {
    return FindRegistration(ModifierRegistry(), id);
}

const EvaluationTargetRegistration *FindEvaluationTarget(
        const std::string &id) {
    return FindRegistration(EvaluationTargetRegistry(), id);
}

OptionConfiguration DefaultSearchAlgorithmConfiguration() {
    const SearchAlgorithmRegistration &registration =
            SearchAlgorithmRegistry().front();
    return {registration.id, registration.defaultSettings};
}

std::vector<OptionConfiguration> DefaultModifierConfigurations() {
    const ModifierRegistration &registration = ModifierRegistry().front();
    return {{registration.id, registration.defaultSettings}};
}

OptionConfiguration DefaultEvaluationTargetConfiguration() {
    const EvaluationTargetRegistration &registration =
            EvaluationTargetRegistry().front();
    return {registration.id, registration.defaultSettings};
}

}  // namespace forevertas
