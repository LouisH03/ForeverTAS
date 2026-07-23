#ifndef FOREVERTAS_SEARCHES_SERIAL_SEARCH_RUNNER_H
#define FOREVERTAS_SEARCHES_SERIAL_SEARCH_RUNNER_H

#include "searches/search_algorithm.h"
#include "searches/serial_brute_force_search.h"

#include <string>

namespace forevertas {

inline constexpr std::uint32_t kSearchTickDurationMs = 10u;

struct SerialSearchRequest {
    std::string packDirectory;
    std::string replayPath;
    SerialBruteForceSettings settings;
};

SerialBruteForceSettings DefaultSerialBruteForceSettings();

SearchResult RunSerialSearch(
        const SerialSearchRequest &request,
        const SearchRunControl *control = nullptr);

}  // namespace forevertas

#endif
