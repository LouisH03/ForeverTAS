#include "searches/algorithm_registry.h"

#include "evaluators/max_speed_evaluator.h"
#include "mutations/random_steering_mutator.h"
#include "searches/serial_brute_force_search.h"

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
                return registration.id == id;
            });
    return found == registrations.end() ? nullptr : &*found;
}

}  // namespace

const std::vector<SearchAlgorithmRegistration> &SearchAlgorithmRegistry() {
    static const std::vector<SearchAlgorithmRegistration> registrations{
            {kSerialBruteForceSearchId,
             "Serial brute force",
             "SerialBruteForceSearchSettings.qml",
             DefaultSerialBruteForceOptionSettings(),
             {{"minMutateMs", "search/minMutateMs"},
              {"maxMutateMs", "search/maxMutateMs"},
              {"minEvalTimeMs", "search/minEvalTimeMs"},
              {"maxEvalTimeMs", "search/maxEvalTimeMs"},
              {"attemptCount", "search/attemptCount"}},
             &ValidateSerialBruteForceOptionSettings,
             &CreateSerialBruteForceSearch}};
    return registrations;
}

const std::vector<MutationAlgorithmRegistration> &MutationAlgorithmRegistry() {
    static const std::vector<MutationAlgorithmRegistration> registrations{
            {kRandomSteeringMutationId,
             "Random steering",
             "RandomSteeringMutationSettings.qml",
             DefaultRandomSteeringOptionSettings(),
             {{"seed", "search/mutationSeed"}},
             &ValidateRandomSteeringOptionSettings,
             &CreateRandomSteeringMutator}};
    return registrations;
}

const std::vector<EvaluationTargetRegistration> &EvaluationTargetRegistry() {
    static const std::vector<EvaluationTargetRegistration> registrations{
            {kMaximumSpeedEvaluationId,
             "Maximum speed",
             "MaximumSpeedEvaluationSettings.qml",
             DefaultMaxSpeedOptionSettings(),
             {},
             &ValidateMaxSpeedOptionSettings,
             &CreateMaxSpeedEvaluator}};
    return registrations;
}

const SearchAlgorithmRegistration *FindSearchAlgorithm(
        const std::string &id) {
    return FindRegistration(SearchAlgorithmRegistry(), id);
}

const MutationAlgorithmRegistration *FindMutationAlgorithm(
        const std::string &id) {
    return FindRegistration(MutationAlgorithmRegistry(), id);
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

OptionConfiguration DefaultMutationAlgorithmConfiguration() {
    const MutationAlgorithmRegistration &registration =
            MutationAlgorithmRegistry().front();
    return {registration.id, registration.defaultSettings};
}

OptionConfiguration DefaultEvaluationTargetConfiguration() {
    const EvaluationTargetRegistration &registration =
            EvaluationTargetRegistry().front();
    return {registration.id, registration.defaultSettings};
}

}  // namespace forevertas
