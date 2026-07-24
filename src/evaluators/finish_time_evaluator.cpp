#include "evaluators/finish_time_evaluator.h"

#include "evaluators/evaluator_utils.h"
#include "searches/option_settings_utils.h"

#include <stdexcept>

namespace forevertas {
namespace {

class FinishTimeSession final : public CandidateEvaluationSession {
public:
    std::optional<EvaluationSample> Observe(
            const std::optional<
                    forevervalidator::experimental::PhysicsSandboxStateView>
                    &previous,
            const forevervalidator::experimental::PhysicsSandboxStateView
                    &current) override {
        static_cast<void>(previous);
        if (reported_ || !current.raceCompleted) {
            return std::nullopt;
        }
        reported_ = true;
        const double timeMs = current.finishTimeMs
                ? static_cast<double>(*current.finishTimeMs)
                : static_cast<double>(current.timeMs);
        return EvaluationSample{
                timeMs,
                timeMs,
                MetricDescription("Finish time", timeMs, "ms", timeMs)};
    }

private:
    bool reported_ = false;
};

}  // namespace

OptionSettings DefaultFinishTimeOptionSettings() {
    return {};
}

std::optional<std::string> ValidateFinishTimeOptionSettings(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs) {
    static_cast<void>(tickDurationMs);
    return ValidateOptionSettingKeys(settings, DefaultFinishTimeOptionSettings());
}

std::unique_ptr<CandidateEvaluator> CreateFinishTimeEvaluator(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs) {
    if (const auto error =
                ValidateFinishTimeOptionSettings(settings, tickDurationMs)) {
        throw std::invalid_argument(*error);
    }
    return std::make_unique<FinishTimeEvaluator>();
}

EvaluationPlan FinishTimeEvaluator::Plan(
        std::int64_t replayDurationMs,
        std::int64_t earliestMutationTimeMs,
        std::uint32_t tickDurationMs) const {
    static_cast<void>(tickDurationMs);
    return ClampPlan(earliestMutationTimeMs,
                     replayDurationMs,
                     replayDurationMs,
                     earliestMutationTimeMs);
}

std::unique_ptr<CandidateEvaluationSession>
FinishTimeEvaluator::CreateSession() const {
    return std::make_unique<FinishTimeSession>();
}

bool FinishTimeEvaluator::IsBetter(const EvaluationSample &candidate,
                                   const EvaluationSample &incumbent) const {
    return candidate.score < incumbent.score;
}

}  // namespace forevertas
