#ifndef FOREVERTAS_INPUT_TIMELINE_TIME_H
#define FOREVERTAS_INPUT_TIMELINE_TIME_H

#include "searches/option_configuration.h"
#include "searches/option_settings_utils.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace forevertas {

inline constexpr std::uint32_t kInputTimelineTickDurationMs = 10u;

// The simulation records the first actionable input one physics tick after the
// user-visible timeline origin. Absolute option keys ending in "TimeMs" are
// user timeline values at the public component-registry boundary and are
// translated here exactly once before simulation-native code receives them.
inline constexpr std::string_view kUserTimelineTimeSettingSuffix = "TimeMs";

inline bool IsUserTimelineTimeSetting(std::string_view key) {
    return key.size() >= kUserTimelineTimeSettingSuffix.size() &&
            key.compare(key.size() - kUserTimelineTimeSettingSuffix.size(),
                        kUserTimelineTimeSettingSuffix.size(),
                        kUserTimelineTimeSettingSuffix) == 0;
}

inline std::optional<std::int64_t> SimulationTimelineTimeFromUserTime(
        std::int64_t userTimeMs,
        std::uint32_t tickDurationMs,
        std::int64_t simulationOriginMs = 0) {
    if (tickDurationMs == 0u || userTimeMs < 0 || simulationOriginMs < 0) {
        return std::nullopt;
    }
    const std::int64_t offset = static_cast<std::int64_t>(tickDurationMs);
    if (simulationOriginMs >
                std::numeric_limits<std::int64_t>::max() - offset ||
        userTimeMs > std::numeric_limits<std::int64_t>::max() -
                simulationOriginMs - offset) {
        return std::nullopt;
    }
    return simulationOriginMs + offset + userTimeMs;
}

inline std::int64_t UserTimelineTimeFromSimulationTime(
        std::int64_t simulationTimeMs,
        std::uint32_t tickDurationMs,
        std::int64_t simulationOriginMs = 0) {
    if (tickDurationMs == 0u) return 0;
    const std::int64_t firstInputTime =
            SimulationTimelineTimeFromUserTime(
                    0, tickDurationMs, simulationOriginMs)
                    .value_or(std::numeric_limits<std::int64_t>::max());
    return std::max<std::int64_t>(0, simulationTimeMs - firstInputTime);
}

inline std::optional<OptionSettings> SimulationSettingsFromUserTimeline(
        const OptionSettings &userSettings,
        std::uint32_t tickDurationMs) {
    if (tickDurationMs == 0u) return std::nullopt;
    OptionSettings simulationSettings = userSettings;
    for (auto &[key, value] : simulationSettings) {
        if (!IsUserTimelineTimeSetting(key)) continue;
        const std::optional<std::int64_t> userTime = ParseSignedDecimal(value);
        if (!userTime) continue;
        const std::optional<std::int64_t> simulationTime =
                SimulationTimelineTimeFromUserTime(*userTime, tickDurationMs);
        if (!simulationTime) return std::nullopt;
        value = std::to_string(*simulationTime);
    }
    return simulationSettings;
}

}  // namespace forevertas

#endif
