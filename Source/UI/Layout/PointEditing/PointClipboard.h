// PointClipboard.h -- what Cmd+C, Cmd+V and Cmd+D carry in every editor with ordered dots: one entry per copied dot,
// as its distance from the earliest copied dot, its value and whatever else the editor's dots carry (`Extra`: a lane
// point's tension and curve, a curve point's bend). Where Cmd+D lands the copies is PointEditorDuplicate.h.
#pragma once

#include <algorithm>
#include <vector>

namespace synth::ui {

template <typename Extra>
struct PointClip {
    struct Entry {
        double offset = 0.0;
        double value = 0.0;
        Extra extra{};
    };
    std::vector<Entry> entries;
    bool isEmpty() const noexcept { return entries.empty(); }
};

/** The three lists are parallel, one item per copied dot, in dot order. */
template <typename Extra>
PointClip<Extra> makePointClip(const std::vector<double>& positions, const std::vector<double>& values,
                               const std::vector<Extra>& extras) {
    PointClip<Extra> clip;
    if (positions.empty())
        return clip;
    const double earliest = *std::min_element(positions.begin(), positions.end());
    for (size_t i = 0; i < positions.size(); ++i)
        clip.entries.push_back({positions[i] - earliest, values[i], extras[i]});
    return clip;
}

/** Where each entry lands with the earliest one at `anchor`. */
template <typename Extra>
std::vector<double> clipPositions(const PointClip<Extra>& clip, double anchor) {
    std::vector<double> out;
    out.reserve(clip.entries.size());
    for (const auto& e : clip.entries)
        out.push_back(anchor + e.offset);
    return out;
}

} // namespace synth::ui
