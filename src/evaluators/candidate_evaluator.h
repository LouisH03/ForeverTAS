#ifndef FOREVERTAS_EVALUATORS_CANDIDATE_EVALUATOR_H
#define FOREVERTAS_EVALUATORS_CANDIDATE_EVALUATOR_H

#include <forevervalidator/experimental/physics_sandbox.h>

namespace forevertas {

class CandidateEvaluator {
public:
    virtual ~CandidateEvaluator() = default;
    virtual double Evaluate(
            const forevervalidator::experimental::PhysicsSandboxStateView
                    &state) const = 0;
};

}  // namespace forevertas

#endif
