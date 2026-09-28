#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <functional>
#include <optional>
#include <vector>

// TrackRowLayout -- the ONE mapping between the arrangement's vertical rows and (track, optional
// automation lane), in CONTENT coordinates (y = 0 is the top of the first track row, before
// TimelineViewState::trackScrollY is subtracted). Pure and headless: no Component, no LookAndFeel.
//
// Every vertical geometry question in the timeline -- the header column's row bounds, the clip
// lanes' row rects and hit tests, the drag row delta, drop-insertion boundaries, scroll-into-view,
// the maximum scroll -- is answered here, so the header column and the clip lanes can never drift.
// docs/timeline/track-automation.md#row-geometry.
namespace synth::ui {

class TrackRowLayout {
public:
    struct Row {
        synth::TrackId track;
        int trackIndex = -1; // index into TimelineDoc::getTracks()
        synth::LaneId lane;  // invalid for a track row
        int laneIndex = -1;  // index into Track::lanes; -1 for a track row
        int top = 0;         // content px
        int height = 0;      // px
        bool isLaneRow() const noexcept { return lane.isValid(); }
        int bottom() const noexcept { return top + height; }
    };

    /** A lane row's height for a given track row height (px). */
    static int laneRowHeightFor(int trackRowHeight) noexcept;

    /** Rebuilds from `doc`'s track order. `isExpanded` may be empty (nothing expanded). */
    void rebuild(const synth::TimelineDoc& doc, const std::function<bool(synth::TrackId)>& isExpanded,
                 int trackRowHeight);

    const std::vector<Row>& getRows() const noexcept { return rows_; }
    int getTotalHeight() const noexcept { return totalHeight_; }
    int getTrackCount() const noexcept { return (int)trackRows_.size(); }
    int getTrackRowHeight() const noexcept { return trackRowHeight_; }
    int getLaneRowHeight() const noexcept { return laneRowHeight_; }
    int getLaneRowCount() const noexcept { return (int)rows_.size() - getTrackCount(); }

    /** Top of track `trackIndex`'s own row; extrapolated at the track row height outside [0, count). */
    int trackRowTop(int trackIndex) const noexcept;
    /** Bottom of track `trackIndex`'s block (its row plus any expanded lane rows); extrapolated too. */
    int trackBlockBottom(int trackIndex) const noexcept;

    /** The row under `contentY`, or nullptr above the first / below the last row. */
    const Row* rowAt(int contentY) const noexcept;
    /** The TRACK row under `contentY`; nullopt on a lane row or outside every row. */
    std::optional<int> trackIndexAt(int contentY) const noexcept;
    /** The track a clip drag at `contentY` targets: lane rows count as their parent track, and
     *  y outside the rows extrapolates (negative above, >= count below). */
    int trackIndexForDrag(int contentY) const noexcept;
    /** Nearest "insert before track N" boundary in [0, count] (lane rows travel with their track). */
    int dropBoundaryAt(int contentY) const noexcept;
    /** Content y of boundary `boundary` in [0, count]. */
    int boundaryY(int boundary) const noexcept;
    /** The lane's row, or nullptr when that lane is not shown (collapsed, global, or gone). */
    const Row* rowForLane(synth::LaneId lane) const noexcept;

private:
    std::vector<Row> rows_;
    std::vector<int> trackRows_; // rows_ index of each track's own row, in track order
    int totalHeight_ = 0;
    int trackRowHeight_ = 0;
    int laneRowHeight_ = 0;
};

} // namespace synth::ui
