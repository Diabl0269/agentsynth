// CustomFilterPeakMorph.cpp -- peak detection for a rendered shape and the peak-moving morph blend.
#include "CustomFilterPeakMorph.h"
#include <algorithm>
#include <array>

namespace synth::customfilter {

namespace {

constexpr int kMaxPeaks = CustomFilterEngine::ShapeTable::kMaxPeaks;
constexpr int kGridSize = CustomFilterEngine::kGridSize;
constexpr int kLast = kGridSize - 1;
// The most a warp may squeeze a stretch of a shape: to this fraction of its own width (see untangle).
constexpr float kMinStretch = 0.25f;

struct Candidate {
    float pos = 0.0f, prominence = 0.0f;
};

/** Topographic prominence of the plateau [first, last] at height h: h minus the higher of the two lowest points
    met walking out to terrain that is higher (or to the grid's end). Ties: walking left an equal height counts as
    lower ground and walking right as higher, so of two equal summits only the right one gets the full prominence and
    a twin hump with a shallow dip between is one peak, not two. */
float prominence(const float* db, int n, int first, int last) noexcept {
    const float h = db[first];
    float lowL = h, lowR = h;
    for (int k = first - 1; k >= 0 && db[k] <= h; --k)
        lowL = std::min(lowL, db[k]);
    for (int k = last + 1; k < n && db[k] < h; ++k)
        lowR = std::min(lowR, db[k]);
    return h - std::max(lowL, lowR);
}

/** Keeps the kMaxPeaks most prominent candidates seen so far in `best` (unsorted). */
void keepMostProminent(std::array<Candidate, kMaxPeaks>& best, int& count, Candidate c) noexcept {
    if (count < kMaxPeaks) {
        best[(size_t)count++] = c;
        return;
    }
    auto weakest = std::min_element(best.begin(), best.end(),
                                    [](const Candidate& a, const Candidate& b) { return a.prominence < b.prominence; });
    if (c.prominence > weakest->prominence)
        *weakest = c;
}

/** Linear interpolation of `db` at grid position `pos` (clamped to the grid). */
float sampleAt(const float* db, float pos) noexcept {
    pos = juce::jlimit(0.0f, (float)kLast, pos);
    const int i0 = std::min((int)pos, kLast - 1);
    const float frac = pos - (float)i0;
    return db[i0] + (db[i0 + 1] - db[i0]) * frac;
}

/** Keeps the warp from squeezing any stretch of a shape: every gap between neighbouring targets (and between the
    first and last target and the grid's ends) stays at least kMinStretch of the same gap between the shape's own
    peaks. Matching by order can fold: a heavy shape whose only peak is high pulls every shape's first peak up past
    where its second peak is going. Pushing the second peak ahead by a fixed bin would squeeze the whole first-to-second
    stretch (valley included) into that bin, and each grid point would then sweep through the valley's depth while
    the anchor moved one bin: continuous, but a click in practice. Bounding the squeeze bounds how fast any point's
    dB can change with the cursor. A forward running max and a backward running min are continuous in their inputs;
    the gaps sum to kMinStretch of the grid, so both passes always fit; and an unfolded set of targets that already
    keeps the gaps is left alone. */
void untangle(float* t, const float* p, int n) noexcept {
    const auto minGap = [&](int k) { // the gap below target k (k == n: below the grid's end)
        const float hi = k == n ? (float)kLast : p[k];
        const float lo = k == 0 ? 0.0f : p[k - 1];
        return kMinStretch * (hi - lo);
    };
    for (int k = 0; k < n; ++k)
        t[k] = std::max(t[k], (k == 0 ? 0.0f : t[k - 1]) + minGap(k));
    for (int k = n - 1; k >= 0; --k)
        t[k] = std::min(t[k], (k == n - 1 ? (float)kLast : t[k + 1]) - minGap(k + 1));
}

/** Adds `w` times the shape `db` warped piecewise-linearly in x so its peaks `p` land on `t` (n anchors, both
    strictly increasing, the grid ends fixed): output point u reads the shape at the inverse-mapped position. Only
    x moves; every value read is an interpolation of the shape's own dB, so no point rises above its peak. */
void addWarped(const float* db, const float* p, const float* t, int n, float w, float* out) noexcept {
    std::array<float, kMaxPeaks + 2> src{}, dst{};
    src[0] = dst[0] = 0.0f;
    for (int k = 0; k < n; ++k) {
        src[(size_t)k + 1] = p[k];
        dst[(size_t)k + 1] = t[k];
    }
    src[(size_t)n + 1] = dst[(size_t)n + 1] = (float)kLast;
    int seg = 0;
    float slope = (src[1] - src[0]) / (dst[1] - dst[0]);
    for (int u = 0; u < kGridSize; ++u) {
        while (seg < n && (float)u > dst[(size_t)seg + 1]) {
            ++seg;
            slope = (src[(size_t)seg + 1] - src[(size_t)seg]) / (dst[(size_t)seg + 1] - dst[(size_t)seg]);
        }
        const float at = src[(size_t)seg] + ((float)u - dst[(size_t)seg]) * slope;
        out[u] += w * sampleAt(db, at);
    }
}

} // namespace

/** Local maxima (a flat top counts once, at its middle) away from the grid's two end points, kept when their
    prominence reaches kMinPeakProminenceDb. Prominence rather than "height above the nearest dip" so a ripple on a
    formant's skirt never counts as a formant. More than kMaxPeaks: the most prominent are kept. */
int findPeaks(const float* db, int n, float* out) noexcept {
    std::array<Candidate, kMaxPeaks> best{};
    int count = 0;
    for (int i = 1; i + 1 < n;) {
        if (!(db[i] > db[i - 1])) {
            ++i;
            continue;
        }
        int j = i;
        while (j + 1 < n && db[j + 1] == db[i])
            ++j;
        if (j + 1 < n && db[j + 1] < db[i]) {
            const float prom = prominence(db, n, i, j);
            if (prom >= kMinPeakProminenceDb)
                keepMostProminent(best, count, {0.5f * (float)(i + j), prom});
        }
        i = j + 1;
    }
    std::sort(best.begin(), best.begin() + count, [](const Candidate& a, const Candidate& b) { return a.pos < b.pos; });
    for (int k = 0; k < count; ++k)
        out[k] = best[(size_t)k].pos;
    return count;
}

/** Peaks are matched by order (every shape's first peak with every other's first, and so on), and shape s's k-th
    peak is moved to
        t = p_s + sum over shapes s' that have a k-th peak of w_s' * (p_s',k - p_s).
    When every shape has a k-th peak this is the weighted mean position (in log frequency), the same target for all
    of them, so the peaks meet and glide instead of cross-fading. A shape with no k-th peak does not pull: its share
    of the weight leaves the peak where it is, so a peak only some shapes have fades in and out as before, and how
    far it moves grows smoothly with the weight of the shapes that share it. There is no participation threshold
    and no division by a weight sum, so the targets are continuous in the weights everywhere, including where a
    weight reaches 0 (a shape with fewer peaks joining the blend never un-anchors anyone's other peaks). With one
    shape at weight 1 and the rest 0 every t equals p exactly and that shape is copied unchanged. */
void blendShapes(const CustomFilterEngine::ShapeTable& table, const float* weights, float* out) noexcept {
    std::fill(out, out + kGridSize, 0.0f);
    std::array<float, kMaxPeaks> sumWP{}, sumW{};
    for (int s = 0; s < table.count; ++s) {
        const float w = weights[s];
        if (!(w > 0.0f))
            continue;
        for (int k = 0; k < table.peakCount[(size_t)s]; ++k) {
            sumWP[(size_t)k] += w * table.peaks[(size_t)(s * kMaxPeaks + k)];
            sumW[(size_t)k] += w;
        }
    }
    for (int s = 0; s < table.count; ++s) {
        const float w = weights[s];
        if (!(w > 0.0f))
            continue;
        const float* db = table.db.data() + s * kGridSize;
        const float* p = table.peaks.data() + s * kMaxPeaks;
        const int n = table.peakCount[(size_t)s];
        std::array<float, kMaxPeaks> t{};
        bool moved = false;
        for (int k = 0; k < n; ++k) {
            t[(size_t)k] = p[k] + (sumWP[(size_t)k] - sumW[(size_t)k] * p[k]);
            moved = moved || t[(size_t)k] != p[k];
        }
        if (!moved) {
            juce::FloatVectorOperations::addWithMultiply(out, db, w, kGridSize);
            continue;
        }
        untangle(t.data(), p, n);
        addWarped(db, p, t.data(), n, w, out);
    }
}

} // namespace synth::customfilter
