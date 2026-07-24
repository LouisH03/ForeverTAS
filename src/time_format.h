#ifndef FOREVERTAS_TIME_FORMAT_H
#define FOREVERTAS_TIME_FORMAT_H

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace forevertas {

inline std::string FormatHumanDurationMilliseconds(double milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
        milliseconds = 0.0;
    }
    const auto rounded = static_cast<std::int64_t>(std::llround(milliseconds));
    const std::int64_t totalHours = rounded / 3600000;
    const std::int64_t minutes = (rounded / 60000) % 60;
    const std::int64_t seconds = (rounded / 1000) % 60;
    const std::int64_t millis = rounded % 1000;

    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(2) << totalHours << ':'
           << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
    if (millis != 0) {
        std::ostringstream fraction;
        fraction << std::setfill('0') << std::setw(3) << millis;
        std::string digits = fraction.str();
        while (!digits.empty() && digits.back() == '0') {
            digits.pop_back();
        }
        stream << '.' << digits;
    }
    return stream.str();
}

template<typename Rep, typename Period>
std::string FormatHumanDuration(
        const std::chrono::duration<Rep, Period> &duration) {
    return FormatHumanDurationMilliseconds(
            std::chrono::duration<double, std::milli>(duration).count());
}

}  // namespace forevertas

#endif
