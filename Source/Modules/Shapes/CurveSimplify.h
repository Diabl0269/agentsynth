// CurveSimplify.h -- picks the few samples of a sampled curve that a polyline through them reproduces.
#pragma once

#include <cmath>
#include <set>
#include <vector>

namespace synth::shapes {

/** Grows `chosen` (indices into `values`, holding at least both ends) by the sample farthest from the straight line
 *  between its neighbours, again and again, until no sample is off by more than `tolerance` or `maxPoints` are chosen.
 *  A flat or straight run keeps only its ends. */
inline void addPointsWhereLinesMiss(std::set<int>& chosen, const std::vector<float>& values, int maxPoints,
                                    float tolerance) {
    while ((int)chosen.size() < maxPoints) {
        int worstAt = -1;
        float worst = tolerance;
        for (auto it = chosen.begin(), next = std::next(it); next != chosen.end(); ++it, ++next) {
            const int lo = *it, hi = *next;
            for (int k = lo + 1; k < hi; ++k) {
                const float line = values[(size_t)lo] +
                                   (values[(size_t)hi] - values[(size_t)lo]) * ((float)(k - lo) / (float)(hi - lo));
                const float miss = std::abs(values[(size_t)k] - line);
                if (miss > worst) {
                    worst = miss;
                    worstAt = k;
                }
            }
        }
        if (worstAt < 0)
            return;
        chosen.insert(worstAt);
    }
}

} // namespace synth::shapes
