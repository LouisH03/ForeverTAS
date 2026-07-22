#ifndef FOREVERTAS_EVALUATORS_MAX_SPEED_EVALUATOR_H
#define FOREVERTAS_EVALUATORS_MAX_SPEED_EVALUATOR_H

#include "evaluators/candidate_evaluator.h"

namespace forevertas {

class MaxSpeedEvaluator final : public CandidateEvaluator {
public:
    double Evaluate(
            const forevervalidator::experimental::PhysicsSandboxStateView
                    &state) const override;
};

}  // namespace forevertas

#endif
