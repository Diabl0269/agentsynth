#pragma once

// The shared click detector of the audio tests: finds second-difference spikes, the sample-to-sample
// kinks a listener hears as a tick, relative to how busy the signal is around them.
//
// A smooth signal has a small second difference d2[n] = x[n-1] - 2 x[n] + x[n+1] (a sine's is its
// own amplitude times (2 pi f / fs)^2), and a step, a phase jump or a kink makes one large value. A spike is
// a d2 above `ratio` times the mean |d2| of the samples around it (the nearest +-2 left out so the
// spike does not hide itself) and above an absolute floor taken from the signal's own peak, so a quiet
// stretch cannot raise false alarms. Neighbouring hits within `group` samples are one click.
// tools: the same algorithm, ported to numpy, measures a reference render the same way.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace clickdetector {

struct Click {
    int index = 0;      ///< sample the spike is centred on
    float size = 0.0f;  ///< |second difference| there
    float ratio = 0.0f; ///< size over the local mean |second difference|
};

struct Settings {
    int window = 48;             ///< samples each side that set the local level
    float ratio = 8.0f;          ///< a spike is this many times the local level
    float floorOfPeak = 2.0e-4f; ///< and above this fraction of the signal's peak
    int group = 24;              ///< hits closer than this are one click
};

/** Every click in `x`, in order. */
inline std::vector<Click> findClicks(const std::vector<float>& x, const Settings& s = {}) {
    std::vector<Click> clicks;
    const int n = (int)x.size() - 2;
    if (n < 2 * s.window + 5)
        return clicks;

    float peak = 1.0e-12f;
    for (float v : x)
        peak = std::max(peak, std::abs(v));
    const double floorLevel = (double)s.floorOfPeak * (double)peak;

    std::vector<double> d2((size_t)n), sums((size_t)n + 1, 0.0);
    for (int k = 0; k < n; ++k) {
        d2[(size_t)k] = std::abs((double)x[(size_t)k] - 2.0 * (double)x[(size_t)k + 1] + (double)x[(size_t)k + 2]);
        sums[(size_t)k + 1] = sums[(size_t)k] + d2[(size_t)k];
    }
    const auto clampTo = [n](int v) { return std::clamp(v, 0, n); };

    int last = -1000000;
    for (int k = 0; k < n; ++k) {
        const int lo = clampTo(k - s.window), hi = clampTo(k + s.window + 1);
        const int innerLo = clampTo(k - 2), innerHi = clampTo(k + 3);
        const int count = (hi - lo) - (innerHi - innerLo);
        const double level = (sums[(size_t)hi] - sums[(size_t)lo] - (sums[(size_t)innerHi] - sums[(size_t)innerLo])) /
                             (double)std::max(count, 1);
        const double ratio = d2[(size_t)k] / std::max(level, floorLevel);
        if (ratio > (double)s.ratio && d2[(size_t)k] > floorLevel) {
            if (k - last > s.group)
                clicks.push_back({k + 1, (float)d2[(size_t)k], (float)ratio});
            else if ((float)d2[(size_t)k] > clicks.back().size)
                clicks.back() = {k + 1, (float)d2[(size_t)k], (float)ratio};
            last = k;
        }
    }
    return clicks;
}

/** The click closest to any of `positions` (within `reach` samples after one), or none: for asserting
    where a render may and may not tick. */
inline std::vector<Click> clicksOutside(const std::vector<Click>& found, const std::vector<int>& allowedAt, int reach) {
    std::vector<Click> bad;
    for (const auto& c : found) {
        const bool allowed = std::any_of(allowedAt.begin(), allowedAt.end(),
                                         [&](int p) { return c.index >= p && c.index <= p + reach; });
        if (!allowed)
            bad.push_back(c);
    }
    return bad;
}

} // namespace clickdetector
