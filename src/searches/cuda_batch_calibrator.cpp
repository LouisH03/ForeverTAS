#include "searches/cuda_batch_calibrator.h"

#include <algorithm>
#include <limits>

namespace forevertas {
namespace {

constexpr std::size_t kSamplesPerBatchSize = 5u;
constexpr std::uint32_t kMaximumRefinementRounds = 8u;

std::uint32_t Midpoint(std::uint32_t left, std::uint32_t right) {
    return left + (right - left) / 2u;
}

std::uint32_t FractionPoint(
        std::uint32_t left,
        std::uint32_t right,
        std::uint32_t numerator,
        std::uint32_t denominator) {
    const std::uint64_t span =
            static_cast<std::uint64_t>(right) - left;
    return static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(left) +
            span * numerator / denominator);
}

}  // namespace

std::uint32_t CudaBatchCalibrator::CurrentBatchSize() const noexcept {
    return currentBatchSize_;
}

std::uint32_t CudaBatchCalibrator::BestBatchSize() const noexcept {
    return bestBatchSize_;
}

bool CudaBatchCalibrator::Complete() const noexcept {
    return phase_ == Phase::Complete;
}

void CudaBatchCalibrator::Observe(
        std::uint32_t candidateCount,
        std::chrono::steady_clock::duration elapsed) {
    if (Complete() || candidateCount != currentBatchSize_) {
        return;
    }
    const double seconds =
            std::chrono::duration<double>(elapsed).count();
    if (seconds <= 0.0) {
        return;
    }
    samples_.push_back(
            static_cast<double>(candidateCount) / seconds);
    if (samples_.size() < kSamplesPerBatchSize) {
        return;
    }
    std::sort(samples_.begin(), samples_.end());
    FinishMeasurement(samples_[samples_.size() / 2u]);
}

void CudaBatchCalibrator::CapacityUnavailable() {
    if (!Complete()) {
        if (!unavailableBatchSize_ ||
            currentBatchSize_ < *unavailableBatchSize_) {
            unavailableBatchSize_ = currentBatchSize_;
        }
        BeginRefinement();
    }
}

void CudaBatchCalibrator::FinishMeasurement(double throughput) {
    measurements_.push_back({currentBatchSize_, throughput});
    if (bestThroughput_ == 0.0 ||
        throughput > bestThroughput_ * 1.005 ||
        (throughput >= bestThroughput_ * 0.995 &&
         currentBatchSize_ < bestBatchSize_)) {
        bestThroughput_ = throughput;
        bestBatchSize_ = currentBatchSize_;
    }

    if (phase_ == Phase::Seed) {
        previousGrowthThroughput_ = throughput;
        BeginGrowth();
        return;
    }
    if (phase_ == Phase::Refinement) {
        AdvanceRefinement();
        return;
    }
    if (phase_ != Phase::Growth) {
        return;
    }

    ++growthSteps_;
    const bool plateau =
            throughput <= previousGrowthThroughput_ * 1.01;
    plateauSteps_ = plateau ? plateauSteps_ + 1u : 0u;
    const bool belowBest =
            currentBatchSize_ != bestBatchSize_ &&
            throughput < bestThroughput_ * 0.98;
    declineSteps_ = belowBest ? declineSteps_ + 1u : 0u;
    previousGrowthThroughput_ = throughput;
    if ((growthSteps_ >= 2u &&
         (plateauSteps_ >= 2u || declineSteps_ >= 2u)) ||
        currentBatchSize_ >
                std::numeric_limits<std::uint32_t>::max() / 2u) {
        BeginRefinement();
        return;
    }
    SetCurrent(currentBatchSize_ * 2u);
}

void CudaBatchCalibrator::BeginGrowth() {
    phase_ = Phase::Growth;
    SetCurrent(2u);
}

void CudaBatchCalibrator::BeginRefinement() {
    refinementRound_ = 0u;
    BeginRefinementRound();
}

void CudaBatchCalibrator::BeginRefinementRound() {
    refinementQueue_.clear();
    refinementIndex_ = 0u;
    if (refinementRound_ >= kMaximumRefinementRounds) {
        phase_ = Phase::Complete;
        SetCurrent(bestBatchSize_);
        return;
    }
    std::sort(
            measurements_.begin(),
            measurements_.end(),
            [](const Measurement &left, const Measurement &right) {
                return left.batchSize < right.batchSize;
            });
    const auto best = std::find_if(
            measurements_.begin(),
            measurements_.end(),
            [this](const Measurement &measurement) {
                return measurement.batchSize == bestBatchSize_;
            });
    if (best != measurements_.end()) {
        const std::uint32_t lower =
                best == measurements_.begin()
                ? best->batchSize
                : (best - 1)->batchSize;
        std::uint32_t upper =
                best + 1 == measurements_.end()
                ? best->batchSize
                : (best + 1)->batchSize;
        const bool hasUnavailableUpperBound =
                best + 1 == measurements_.end() &&
                unavailableBatchSize_ &&
                *unavailableBatchSize_ > best->batchSize;
        if (hasUnavailableUpperBound) {
            upper = *unavailableBatchSize_;
        }
        const std::uint32_t span = upper - lower;
        const std::uint32_t minimumUsefulSpan =
                std::max<std::uint32_t>(
                        2u, bestBatchSize_ / 50u);
        if (span <= minimumUsefulSpan) {
            phase_ = Phase::Complete;
            SetCurrent(bestBatchSize_);
            return;
        }

        const auto enqueue = [this](std::uint32_t candidate) {
            if (candidate != 0u && !Measured(candidate) &&
                std::find(
                        refinementQueue_.begin(),
                        refinementQueue_.end(),
                        candidate) == refinementQueue_.end()) {
                refinementQueue_.push_back(candidate);
            }
        };
        if (hasUnavailableUpperBound) {
            enqueue(Midpoint(best->batchSize, upper));
            enqueue(Midpoint(lower, best->batchSize));
        } else if (best == measurements_.begin() ||
                   best + 1 == measurements_.end()) {
            enqueue(FractionPoint(lower, upper, 1u, 4u));
            enqueue(Midpoint(lower, upper));
            enqueue(FractionPoint(lower, upper, 3u, 4u));
        } else {
            enqueue(Midpoint(lower, best->batchSize));
            enqueue(Midpoint(best->batchSize, upper));
        }
    }
    if (refinementQueue_.empty()) {
        phase_ = Phase::Complete;
        SetCurrent(bestBatchSize_);
        return;
    }
    phase_ = Phase::Refinement;
    SetCurrent(refinementQueue_.front());
}

void CudaBatchCalibrator::AdvanceRefinement() {
    ++refinementIndex_;
    if (refinementIndex_ >= refinementQueue_.size()) {
        ++refinementRound_;
        BeginRefinementRound();
        return;
    }
    SetCurrent(refinementQueue_[refinementIndex_]);
}

void CudaBatchCalibrator::SetCurrent(std::uint32_t batchSize) {
    currentBatchSize_ = std::max(batchSize, 1u);
    samples_.clear();
}

bool CudaBatchCalibrator::Measured(std::uint32_t batchSize) const {
    return std::any_of(
            measurements_.begin(),
            measurements_.end(),
            [batchSize](const Measurement &measurement) {
                return measurement.batchSize == batchSize;
            });
}

}  // namespace forevertas
