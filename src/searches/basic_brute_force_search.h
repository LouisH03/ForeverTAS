#ifndef FOREVERTAS_SEARCHES_BASIC_BRUTE_FORCE_SEARCH_H
#define FOREVERTAS_SEARCHES_BASIC_BRUTE_FORCE_SEARCH_H

#include "searches/option_configuration.h"
#include "searches/search_algorithm.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace forevertas {

OptionSettings DefaultBasicBruteForceOptionSettings();
std::optional<std::string> ValidateBasicBruteForceOptionSettings(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs);
std::unique_ptr<SearchAlgorithm> CreateBasicBruteForceSearch(
        const OptionSettings &settings,
        std::uint32_t tickDurationMs);

class BasicBruteForceSearch final : public SearchAlgorithm {
public:
    SearchResult Run(const SearchExecutionContext &context) const override;
};

}  // namespace forevertas

#endif
