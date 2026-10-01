// Concern: TimelineAutomationLanes' geometry -- what the lane rows add to the track layout, where
// each lane header and editor sits, and the playhead value readouts.
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kDefaultLaneRowHeight = 40; // headless fallback for Metrics::timelineAutomationLaneRowHeight
constexpr int kMinLaneRowHeight = 12;
} // namespace

// Scaled by the same vertical zoom as the clip rows, so a lane keeps its proportion to its track.
int TimelineAutomationLanes::laneRowHeight() const {
    int base = kDefaultLaneRowHeight;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&headerParent_.getLookAndFeel()))
        base = lf->getTheme().metrics.timelineAutomationLaneRowHeight;
    return std::max(kMinLaneRowHeight, (int)std::llround((double)base * viewState_.rowHeightScale));
}

std::vector<int> TimelineAutomationLanes::extraHeights() const {
    std::vector<int> extras;
    if (doc_ == nullptr)
        return extras;
    const int rowHeight = laneRowHeight();
    for (const auto& track : doc_->getTracks())
        extras.push_back(isVisibleLane(track) ? (int)track.lanes.size() * rowHeight : 0);
    return extras;
}

// The Automation track is drawn as a short section header, not a clip row: it holds no clips.
std::vector<int> TimelineAutomationLanes::rowHeightOverrides() const {
    std::vector<int> overrides;
    if (doc_ == nullptr)
        return overrides;
    for (const auto& track : doc_->getTracks())
        overrides.push_back(track.kind == synth::TrackKind::Automation ? kSectionRowHeight : 0);
    return overrides;
}

// Called by the panel for every track header it places -- including one mid reorder-drag -- so a
// track's lane headers travel with it rather than with the static layout.
void TimelineAutomationLanes::placeHeadersFor(synth::TrackId track, int firstRowY, int width) {
    const auto* t = doc_ != nullptr ? doc_->getTrack(track) : nullptr;
    if (t == nullptr || !isVisibleLane(*t))
        return;
    const int rowHeight = laneRowHeight();
    int y = firstRowY;
    for (const auto& lane : t->lanes) {
        if (auto* header = headerFor(lane.id))
            header->setBounds(0, y, width, rowHeight);
        y += rowHeight;
    }
}

juce::Rectangle<int> TimelineAutomationLanes::laneRowContentBounds(synth::LaneId lane,
                                                                   const TimelineRowLayout& layout) const {
    if (doc_ == nullptr)
        return {};
    const auto& tracks = doc_->getTracks();
    const int rowHeight = laneRowHeight();
    for (int i = 0; i < (int)tracks.size(); ++i) {
        const auto& track = tracks[(size_t)i];
        if (!isVisibleLane(track))
            continue;
        int y = layout.trackTop(i) + layout.trackRowHeight(i);
        for (const auto& candidate : track.lanes) {
            if (candidate.id == lane)
                return {0, y, bodies_->getWidth(), rowHeight};
            y += rowHeight;
        }
    }
    return {};
}

// Editors are positioned in the lanes region's own coordinates, i.e. content y minus the shared
// vertical scroll -- the same offset the clip lanes subtract -- and clipped by the container.
void TimelineAutomationLanes::placeBodies(const TimelineRowLayout& layout) {
    if (doc_ == nullptr)
        return;
    const int scroll = (int)std::llround(viewState_.trackScrollY);
    for (auto& [id, editor] : editors_) {
        const auto row = laneRowContentBounds(id, layout);
        editor->setBounds(row.translated(0, -scroll));
    }
}

// Rides the panel's existing transport poll: nothing happens while the beat stands still, and only
// headers whose row is on screen re-evaluate (each repaints only if its text changed).
void TimelineAutomationLanes::tickReadouts(double beat, int visibleTop, int visibleBottom) {
    if (beat == lastReadoutBeat_)
        return;
    lastReadoutBeat_ = beat;
    for (auto& [id, header] : headers_) {
        const auto b = header->getBounds();
        if (b.getBottom() > visibleTop && b.getY() < visibleBottom)
            header->setReadoutBeat(beat);
    }
}

} // namespace synth::ui
