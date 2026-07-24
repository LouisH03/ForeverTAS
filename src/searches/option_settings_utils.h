#ifndef FOREVERTAS_SEARCHES_OPTION_SETTINGS_UTILS_H
#define FOREVERTAS_SEARCHES_OPTION_SETTINGS_UTILS_H

#include "searches/option_configuration.h"

#include <cstdint>
#include <cmath>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string>

namespace forevertas {

inline std::optional<std::string> ValidateOptionSettingKeys(
        const OptionSettings &settings,
        const OptionSettings &defaults) {
    for (const auto &[key, value] : defaults) {
        static_cast<void>(value);
        if (settings.find(key) == settings.end()) {
            return "missing setting: " + key;
        }
    }
    for (const auto &[key, value] : settings) {
        static_cast<void>(value);
        if (defaults.find(key) == defaults.end()) {
            return "unknown setting: " + key;
        }
    }
    return std::nullopt;
}

inline bool IsDecimal(const std::string &value) {
    if (value.empty()) {
        return false;
    }
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return false;
        }
    }
    return true;
}

inline std::optional<std::int64_t> ParseSignedDecimal(
        const std::string &value) {
    if (!IsDecimal(value)) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0u;
        const long long parsed = std::stoll(value, &consumed, 10);
        if (consumed != value.size()) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<std::uint64_t> ParseUnsignedDecimal64(
        const std::string &value) {
    if (!IsDecimal(value)) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0u;
        const unsigned long long parsed = std::stoull(value, &consumed, 10);
        if (consumed != value.size()) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<std::uint32_t> ParseUnsignedDecimal32(
        const std::string &value) {
    const std::optional<std::uint64_t> parsed =
            ParseUnsignedDecimal64(value);
    if (!parsed || *parsed > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*parsed);
}

inline std::optional<double> ParseFiniteDouble(const std::string &value) {
    if (value.empty()) {
        return std::nullopt;
    }

    std::istringstream stream(value);
    stream.imbue(std::locale::classic());
    stream >> std::noskipws;

    double parsed = 0.0;
    if (!(stream >> parsed) || !std::isfinite(parsed)) {
        return std::nullopt;
    }

    char trailing = '\0';
    if (stream >> trailing || !stream.eof()) {
        return std::nullopt;
    }
    return parsed;
}

inline std::optional<bool> ParseBoolean(const std::string &value) {
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return std::nullopt;
}

inline std::optional<std::string> ValidateTimeWindow(
        std::int64_t minimum,
        std::int64_t maximum,
        std::uint32_t tickDurationMs,
        const std::string &name) {
    if (tickDurationMs == 0u) return "tick duration must be greater than zero";
    if (minimum < static_cast<std::int64_t>(tickDurationMs)) {
        return name + " minimum must be at least one tick";
    }
    if (maximum < minimum) return name + " maximum must not precede minimum";
    const std::int64_t tick = static_cast<std::int64_t>(tickDurationMs);
    if (minimum % tick != 0 || maximum % tick != 0) {
        return name + " times must align to whole ticks";
    }
    return std::nullopt;
}

}  // namespace forevertas

#endif
