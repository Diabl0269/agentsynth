#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace synth::ui {

/**
 * @brief The ONE place the timeline's vertical row geometry comes from.
 *
 * Every track owns a clip row of `trackRowHeight(i)` pixels (the shared zoom-scaled height unless
 * that track overrides it -- the Automation track's short section row), followed by an optional
 * "extra" area (its expanded automation lanes) whose height is per track. The clip lane
 * (painting, hit tests, edit tools, range selection), the track-header column, the track reorder
 * drag, the scroll limit and the playhead overlay all read tops and spans from here, so none of them
 * can assume `index * rowHeight` and drift from another.
 *
 * Pure value type: built from numbers only (no LookAndFeel, no doc), cheap to rebuild, and every
 * y is in CONTENT coordinates (before the vertical scroll offset, which callers still apply).
 */
class TimelineRowLayout {
public:
    TimelineRowLayout() = default;

    /** @param trackCount number of tracks
     *  @param clipRowHeight the (already zoom-scaled) default height of a track's clip row
     *  @param extraBelow per-track extra height under the clip row; missing/negative entries are 0
     *  @param rowHeightOverride per-track clip-row height, px; missing/non-positive = clipRowHeight */
    TimelineRowLayout(int trackCount, int clipRowHeight, const std::vector<int>& extraBelow = {},
                      const std::vector<int>& rowHeightOverride = {});

    struct Hit {
        int trackIndex = -1;    ///< -1 when y lies outside every track
        bool inClipRow = false; ///< false when y is in the track's extra area
    };

    int trackCount() const noexcept { return trackCount_; }

    /** The default clip-row height (what a track without an override gets). */
    int trackRowHeight() const noexcept { return rowHeight_; }
    /** The clip-row height of one track (the default for an out-of-range index). */
    int trackRowHeight(int trackIndex) const noexcept;

    /** Top of the track's clip row. Indices past the last track continue at the uniform pitch, so a
     *  header list that briefly outnumbers the doc still lays out sanely. */
    int trackTop(int trackIndex) const noexcept;

    /** Height of the track's extra area (0 for none or an out-of-range index). */
    int trackExtraHeight(int trackIndex) const noexcept;

    /** Clip row plus extra area: the track's whole vertical slot. */
    juce::Range<int> trackSpan(int trackIndex) const noexcept;

    /** The track whose span contains `y`, and whether `y` is in its clip row. */
    Hit hitAtY(int y) const noexcept;
    int trackIndexAtY(int y) const noexcept { return hitAtY(y).trackIndex; }
    bool isInTrackRow(int y) const noexcept { return hitAtY(y).inClipRow; }

    /** Sum of every track's span. */
    int totalHeight() const noexcept { return tops_.empty() ? 0 : tops_.back(); }

    /** The track whose clip-row top is nearest `trackTop(fromTrack) + dy` — what "dragged dy pixels
     *  down from this row" lands on. Exact ties go to the row further in the drag's direction.
     *  -1 when the pointer is nearer the virtual row above the first or below the last track. */
    int trackIndexNearestTop(int fromTrack, int dy) const noexcept;

    /** Where content y `y` of `from` lands in `to` (same tracks, rows resized): the same track at
     *  the same fraction of its clip row or extra area. What keeps a zoom anchor still. */
    static double mapContentY(const TimelineRowLayout& from, const TimelineRowLayout& to, double y) noexcept;

private:
    int trackCount_ = 0;
    int rowHeight_ = 0;
    std::vector<int> tops_; // trackCount_ + 1 prefix sums of the spans: tops_[i] = top of track i
    std::vector<int> extras_;
    std::vector<int> heights_; // per-track clip-row height
};

} // namespace synth::ui
