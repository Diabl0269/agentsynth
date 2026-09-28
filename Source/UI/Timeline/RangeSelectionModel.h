#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace synth::ui {

/**
 * @brief The Range tool's time-range selection: a beat span across a contiguous run of track rows.
 *
 * Deliberately independent of ClipSelectionModel. A range is not a set of clips — it can cover
 * parts of clips and empty space across several tracks — so it is stored as the two corners the
 * user dragged between: an ANCHOR (where the press landed) and an EXTENT (where the pointer is
 * now), each a (track, beat) pair. Shift-extending moves only the extent, which is what keeps the
 * far corner fixed while the user reaches for the near one.
 *
 * Corners hold TrackIds, not row indices, so a range survives a track being reordered and dies
 * cleanly (coveredTracks() comes back empty) when either corner's track is removed. The covered
 * rows are always read against the doc's CURRENT order.
 *
 * Pure state, no Component and no mutation of the doc — owned by TimelineClipLaneArea, which
 * paints it and routes the range verbs, and read by TimelinePanelComponent for Copy/Cut.
 */
class RangeSelectionModel {
public:
    /** Starts a fresh range with both corners at (track, beat). */
    void begin(synth::TrackId track, double beat) noexcept {
        anchorTrack_ = extentTrack_ = track;
        anchorBeat_ = extentBeat_ = beat;
    }

    /** Moves the extent corner, keeping the anchor. A no-op on an inactive range. */
    void extendTo(synth::TrackId track, double beat) noexcept {
        if (!isActive())
            return;
        extentTrack_ = track;
        extentBeat_ = beat;
    }

    void clear() noexcept { *this = RangeSelectionModel{}; }

    /** True once begin() has run and clear() has not — whatever its width. */
    bool isActive() const noexcept { return anchorTrack_.isValid() && extentTrack_.isValid(); }

    double getStartBeat() const noexcept { return std::min(anchorBeat_, extentBeat_); }
    double getEndBeat() const noexcept { return std::max(anchorBeat_, extentBeat_); }
    synth::TrackId getAnchorTrack() const noexcept { return anchorTrack_; }
    synth::TrackId getExtentTrack() const noexcept { return extentTrack_; }
    double getAnchorBeat() const noexcept { return anchorBeat_; }
    double getExtentBeat() const noexcept { return extentBeat_; }

    /** True when the span has width — a click that never dragged is active but empty. */
    bool hasWidth() const noexcept { return isActive() && getEndBeat() > getStartBeat(); }

    /** The [first, last] row indices the range covers in `doc`'s current order, or nullopt when
     *  inactive or when either corner's track is gone. */
    std::optional<std::pair<int, int>> coveredRows(const synth::TimelineDoc& doc) const {
        if (!isActive())
            return std::nullopt;
        int anchorRow = -1, extentRow = -1;
        const auto& tracks = doc.getTracks();
        for (int i = 0; i < (int)tracks.size(); ++i) {
            if (tracks[(std::size_t)i].id == anchorTrack_)
                anchorRow = i;
            if (tracks[(std::size_t)i].id == extentTrack_)
                extentRow = i;
        }
        if (anchorRow < 0 || extentRow < 0)
            return std::nullopt;
        return std::make_pair(std::min(anchorRow, extentRow), std::max(anchorRow, extentRow));
    }

    /** The covered tracks in `doc`'s row order (see coveredRows); empty when there are none. */
    std::vector<synth::TrackId> coveredTracks(const synth::TimelineDoc& doc) const {
        std::vector<synth::TrackId> covered;
        if (const auto rows = coveredRows(doc))
            for (int i = rows->first; i <= rows->second; ++i)
                covered.push_back(doc.getTracks()[(std::size_t)i].id);
        return covered;
    }

    /** Whether (row, beat) falls inside the range: a covered row and a beat in [start, end). */
    bool contains(const synth::TimelineDoc& doc, int row, double beat) const {
        const auto rows = coveredRows(doc);
        return rows && hasWidth() && row >= rows->first && row <= rows->second && beat >= getStartBeat() &&
               beat < getEndBeat();
    }

private:
    synth::TrackId anchorTrack_;
    synth::TrackId extentTrack_;
    double anchorBeat_ = 0.0;
    double extentBeat_ = 0.0;
};

} // namespace synth::ui
