#include "evaluators/max_speed_evaluator.h"

#include <cmath>

namespace forevertas {

double MaxSpeedEvaluator::Evaluate(
        const forevervalidator::experimental::PhysicsSandboxStateView &state)
        const {
    const auto &speed = state.car.linearSpeed;
    return std::hypot(static_cast<double>(speed.x),
                      static_cast<double>(speed.y),
                      static_cast<double>(speed.z));
}

}  // namespace forevertas
