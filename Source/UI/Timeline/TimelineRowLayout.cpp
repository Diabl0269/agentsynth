#include "UI/Timeline/TimelineRowLayout.h"

#include <algorithm>
#include <cstdlib>

namespace synth::ui {

TimelineRowLayout::TimelineRowLayout(int trackCount, int clipRowHeight, const std::vector<int>& extraBelow)
    : trackCount_(std::max(0, trackCount))
    , rowHeight_(std::max(0, clipRowHeight)) {
    extras_.assign((size_t)trackCount_, 0);
    for (size_t i = 0; i < extras_.size() && i < extraBelow.size(); ++i)
        extras_[i] = std::max(0, extraBelow[i]);
    tops_.reserve((size_t)trackCount_ + 1);
    int top = 0;
    for (int i = 0; i < trackCount_; ++i) {
        tops_.push_back(top);
        top += rowHeight_ + extras_[(size_t)i];
    }
    tops_.push_back(top);
}

int TimelineRowLayout::trackTop(int trackIndex) const noexcept {
    if (trackIndex < 0)
        return trackIndex * rowHeight_;
    if (trackIndex < trackCount_)
        return tops_[(size_t)trackIndex];
    return totalHeight() + (trackIndex - trackCount_) * rowHeight_;
}

int TimelineRowLayout::trackExtraHeight(int trackIndex) const noexcept {
    return juce::isPositiveAndBelow(trackIndex, trackCount_) ? extras_[(size_t)trackIndex] : 0;
}

juce::Range<int> TimelineRowLayout::trackSpan(int trackIndex) const noexcept {
    const int top = trackTop(trackIndex);
    return {top, top + rowHeight_ + trackExtraHeight(trackIndex)};
}

TimelineRowLayout::Hit TimelineRowLayout::hitAtY(int y) const noexcept {
    if (y < 0 || y >= totalHeight() || rowHeight_ <= 0)
        return {};
    // First top strictly greater than y, minus one, is the track whose span holds y.
    const auto it = std::upper_bound(tops_.begin(), tops_.begin() + trackCount_, y);
    const int index = (int)(it - tops_.begin()) - 1;
    return {index, y - tops_[(size_t)index] < rowHeight_};
}

int TimelineRowLayout::trackIndexNearestTop(int fromTrack, int dy) const noexcept {
    if (trackCount_ == 0 || !juce::isPositiveAndBelow(fromTrack, trackCount_))
        return -1;
    if (dy == 0)
        return fromTrack;
    const long target = (long)trackTop(fromTrack) + dy;
    // Candidates include the virtual rows just outside the list so a drag nearer to "off the end"
    // than to the last row reports "none" rather than clamping onto an edge row.
    int best = -1;
    long bestDistance = 0;
    for (int i = -1; i <= trackCount_; ++i) {
        const long distance = std::labs((long)trackTop(i) - target);
        const bool closer = best == -2 || distance < bestDistance;
        // A tie goes further along the drag direction: later rows when dragging down, earlier up.
        const bool tieWins = distance == bestDistance && ((dy > 0) == (i > best));
        if (i == -1 || closer || tieWins) {
            best = i;
            bestDistance = distance;
        }
    }
    return juce::isPositiveAndBelow(best, trackCount_) ? best : -1;
}

} // namespace synth::ui
