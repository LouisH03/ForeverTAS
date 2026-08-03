#include "app/compact_number_format.h"

#include <array>
#include <cmath>

namespace forevertas::app {
namespace {

QString TrimFraction(QString value) {
    while (value.endsWith(QLatin1Char('0'))) value.chop(1);
    if (value.endsWith(QLatin1Char('.'))) value.chop(1);
    return value;
}

}  // namespace

QString FormatCompactNumber(double value) {
    if (!std::isfinite(value)) {
        return QString::number(value);
    }

    constexpr std::array<const char *, 6> suffixes{
            "", "k", "M", "B", "T", "Q"};
    const double absolute = std::abs(value);
    std::size_t suffixIndex = 0u;
    double scale = 1.0;
    while (suffixIndex + 1u < suffixes.size() &&
           absolute >= scale * 1000.0) {
        ++suffixIndex;
        scale *= 1000.0;
    }

    double scaled = value / scale;
    double rounded = std::round(scaled * 100.0) / 100.0;
    if (suffixIndex + 1u < suffixes.size() &&
        std::abs(rounded) >= 1000.0) {
        ++suffixIndex;
        scale *= 1000.0;
        scaled = value / scale;
        rounded = std::round(scaled * 100.0) / 100.0;
    }

    return TrimFraction(QString::number(rounded, 'f', 2)) +
            QString::fromLatin1(suffixes[suffixIndex]);
}

}  // namespace forevertas::app
