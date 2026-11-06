#include "PointScale.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace synth::ui {

namespace {
constexpr double kStep = 0.10;     // a press changes the spread by a tenth
constexpr double kFineStep = 0.02; // Shift: a fiftieth
constexpr double kTiny = 1e-12;
} // namespace

double pointScaleFactor(int side, bool fine) {
    const double grown = 1.0 + (fine ? kFineStep : kStep);
    return side > 0 ? grown : 1.0 / grown;
}

std::vector<double> scaleValuesAbout(const std::vector<double>& values, const std::vector<ScaleLimits>& limits,
                                     double factor) {
    std::vector<double> scaled = values;
    if (values.size() < 2 || limits.size() != values.size())
        return scaled;
    const auto [lowest, highest] = std::minmax_element(values.begin(), values.end());
    const double centre = (*lowest + *highest) * 0.5;
    for (std::size_t i = 0; i < values.size(); ++i)
        scaled[i] = std::clamp(centre + (values[i] - centre) * factor, std::min(limits[i].lo, limits[i].hi),
                               std::max(limits[i].lo, limits[i].hi));
    return scaled;
}

std::vector<double> scalePositionsAbout(const std::vector<double>& positions, const std::vector<ScaleLimits>& limits,
                                        double minGap, double factor) {
    if (positions.size() < 2 || limits.size() != positions.size() || factor <= 0.0)
        return positions;
    const double centre = (positions.front() + positions.back()) * 0.5;
    constexpr double kInf = std::numeric_limits<double>::infinity();
    double lowest = -kInf; // the factor may not go below / above these, from the strictest dot
    double highest = kInf;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const double d = positions[i] - centre;
        if (std::abs(d) < kTiny)
            continue;
        const double a = (limits[i].lo - centre) / d;
        const double b = (limits[i].hi - centre) / d;
        lowest = std::max(lowest, std::min(a, b));
        highest = std::min(highest, std::max(a, b));
        if (i + 1 < positions.size()) {
            const double apart = positions[i + 1] - positions[i];
            if (apart > kTiny)
                lowest = std::max(lowest, minGap / apart);
        }
    }
    // A dot already past a limit never makes the block jump back: 1 always stays allowed.
    const double clamped = std::clamp(factor, std::min(lowest, 1.0), std::max(highest, 1.0));
    std::vector<double> scaled = positions;
    for (std::size_t i = 0; i < positions.size(); ++i)
        scaled[i] = centre + (positions[i] - centre) * clamped;
    return scaled;
}

} // namespace synth::ui
