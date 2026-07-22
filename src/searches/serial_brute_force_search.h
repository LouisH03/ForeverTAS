#ifndef FOREVERTAS_SEARCHES_SERIAL_BRUTE_FORCE_SEARCH_H
#define FOREVERTAS_SEARCHES_SERIAL_BRUTE_FORCE_SEARCH_H

#include "searches/search_algorithm.h"

#include <cstdint>
#include <optional>
#include <string>

namespace forevertas {

struct SerialBruteForceSettings {
    std::int64_t minMutateMs = 0;
    std::int64_t maxMutateMs = 0;
    std::int64_t minEvalTimeMs = 0;
    std::int64_t maxEvalTimeMs = 0;
    std::uint64_t attemptCount = 0u;
    std::uint32_t mutationSeed = 0u;
};

std::optional<std::string> ValidateSerialBruteForceSettings(
        const SerialBruteForceSettings &settings,
        std::uint32_t tickDurationMs);

class SerialBruteForceSearch final : public SearchAlgorithm {
public:
    explicit SerialBruteForceSearch(SerialBruteForceSettings settings);

    SearchResult Run(const SearchExecutionContext &context) const override;

private:
    SerialBruteForceSettings settings_;
};

}  // namespace forevertas

#endif
