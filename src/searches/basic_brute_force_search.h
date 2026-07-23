#ifndef FOREVERTAS_SEARCHES_BASIC_BRUTE_FORCE_SEARCH_H
#define FOREVERTAS_SEARCHES_BASIC_BRUTE_FORCE_SEARCH_H

#include "searches/option_configuration.h"
#include "searches/search_algorithm.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace forevertas {

struct BasicBruteForceSettings {
    std::int64_t minMutateMs = 0;
    std::int64_t maxMutateMs = 0;
    std::int64_t minEvalTimeMs = 0;
    std::int64_t maxEvalTimeMs = 0;
    std::uint64_t attemptCount = 0u;
};

BasicBruteForceSettings DefaultBasicBruteForceSettings();
OptionSettings DefaultBasicBruteForceOptionSettings();

std::optional<std::string> ValidateBasicBruteForceSettings(
        const BasicBruteForceSettings &settings,
        std::uint32_t tickDurationMs);
std::optional<std::string> ValidateBasicBruteForceOptionSettings(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs);
std::unique_ptr<SearchAlgorithm> CreateBasicBruteForceSearch(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs);

class BasicBruteForceSearch final : public SearchAlgorithm {
public:
    explicit BasicBruteForceSearch(BasicBruteForceSettings settings);

    SearchResult Run(const SearchExecutionContext &context) const override;

private:
    BasicBruteForceSettings settings_;
};

}  // namespace forevertas

#endif
