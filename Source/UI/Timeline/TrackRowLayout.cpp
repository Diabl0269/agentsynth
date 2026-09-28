// TrackRowLayout.cpp
//
// Concern: building the row list and answering every y -> row / row -> y question the header
// column and the clip lanes ask (docs/timeline/track-automation.md#row-geometry).

#include "TrackRowLayout.h"

#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// Three quarters of a track row reads as "belongs to the row above" without costing a full row of
// height per lane; the floor keeps the lane header's name + record-mode combo legible at the
// smallest vertical zoom (TimelineViewState::kMinRowHeightScale).
constexpr double kLaneRowHeightFraction = 0.75;
constexpr int kMinLaneRowHeight = 18;
} // namespace

int TrackRowLayout::laneRowHeightFor(int trackRowHeight) noexcept {
    return std::max(kMinLaneRowHeight, (int)std::lround((double)trackRowHeight * kLaneRowHeightFraction));
}

// Automation-kind tracks never expand: their lanes are the GLOBAL ones, edited in the bottom strip
// (docs/timeline/track-automation.md#global-lanes-stay-in-the-strip), so a stray id in the expanded
// set for one of them is ignored rather than trusted. Ids in the set that name no track are ignored
// the same way, which is why nothing ever has to prune the set when a track is deleted.
void TrackRowLayout::rebuild(const synth::TimelineDoc& doc, const std::function<bool(synth::TrackId)>& isExpanded,
                             int trackRowHeight) {
    rows_.clear();
    trackRows_.clear();
    trackRowHeight_ = std::max(1, trackRowHeight);
    laneRowHeight_ = laneRowHeightFor(trackRowHeight_);

    int y = 0;
    const auto& tracks = doc.getTracks();
    for (int t = 0; t < (int)tracks.size(); ++t) {
        const auto& track = tracks[(std::size_t)t];
        trackRows_.push_back((int)rows_.size());
        rows_.push_back({track.id, t, {}, -1, y, trackRowHeight_});
        y += trackRowHeight_;

        const bool expanded = track.kind != synth::TrackKind::Automation && isExpanded && isExpanded(track.id);
        if (!expanded)
            continue;
        for (int l = 0; l < (int)track.lanes.size(); ++l) {
            rows_.push_back({track.id, t, track.lanes[(std::size_t)l].id, l, y, laneRowHeight_});
            y += laneRowHeight_;
        }
    }
    totalHeight_ = y;
}

int TrackRowLayout::trackRowTop(int trackIndex) const noexcept {
    const int count = getTrackCount();
    if (trackIndex < 0)
        return trackIndex * trackRowHeight_;
    if (trackIndex >= count)
        return totalHeight_ + (trackIndex - count) * trackRowHeight_;
    return rows_[(std::size_t)trackRows_[(std::size_t)trackIndex]].top;
}

int TrackRowLayout::trackBlockBottom(int trackIndex) const noexcept {
    const int count = getTrackCount();
    if (trackIndex < 0 || trackIndex >= count)
        return trackRowTop(trackIndex) + trackRowHeight_;
    return trackIndex + 1 < count ? trackRowTop(trackIndex + 1) : totalHeight_;
}

// Binary search over the sorted, contiguous row tops.
const TrackRowLayout::Row* TrackRowLayout::rowAt(int contentY) const noexcept {
    if (contentY < 0 || contentY >= totalHeight_ || rows_.empty())
        return nullptr;
    const auto it =
        std::upper_bound(rows_.begin(), rows_.end(), contentY, [](int y, const Row& row) { return y < row.top; });
    return it == rows_.begin() ? nullptr : &*std::prev(it);
}

std::optional<int> TrackRowLayout::trackIndexAt(int contentY) const noexcept {
    const auto* row = rowAt(contentY);
    if (row == nullptr || row->isLaneRow())
        return std::nullopt;
    return row->trackIndex;
}

// Lane rows map to their PARENT track rather than to "no track": a clip dragged down across an
// expanded track's lane rows is still over that track, and it can only ever land on a track row
// (never on a lane row) because a clip has nowhere else to live. Outside the rows the index is
// extrapolated at the track row height, so a drag past the last row yields an index >= count --
// which the drag's own "destination row must exist" check then refuses, exactly as before lanes.
int TrackRowLayout::trackIndexForDrag(int contentY) const noexcept {
    if (const auto* row = rowAt(contentY))
        return row->trackIndex;
    if (contentY < 0)
        return (int)std::floor((double)contentY / (double)trackRowHeight_);
    return getTrackCount() + (contentY - totalHeight_) / trackRowHeight_;
}

// Rounds to the nearest BLOCK boundary: a track and its expanded lane rows move as one unit, so a
// boundary between a track row and its own first lane row would be meaningless. A tie goes to the
// LOWER boundary, matching the uniform-row `(y + h / 2) / h` rounding this replaced.
int TrackRowLayout::dropBoundaryAt(int contentY) const noexcept {
    const int count = getTrackCount();
    int best = 0;
    int bestDistance = std::abs(contentY - boundaryY(0));
    for (int b = 1; b <= count; ++b) {
        const int distance = std::abs(contentY - boundaryY(b));
        if (distance <= bestDistance) {
            best = b;
            bestDistance = distance;
        }
    }
    return best;
}

int TrackRowLayout::boundaryY(int boundary) const noexcept {
    const int count = getTrackCount();
    if (boundary <= 0)
        return 0;
    if (boundary >= count)
        return totalHeight_;
    return trackRowTop(boundary);
}

const TrackRowLayout::Row* TrackRowLayout::rowForLane(synth::LaneId lane) const noexcept {
    if (!lane.isValid())
        return nullptr;
    for (const auto& row : rows_)
        if (row.lane == lane)
            return &row;
    return nullptr;
}

} // namespace synth::ui
