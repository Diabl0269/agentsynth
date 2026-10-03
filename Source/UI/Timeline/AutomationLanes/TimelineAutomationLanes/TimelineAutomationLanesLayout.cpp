// Concern: TimelineAutomationLanes' geometry -- what the lane rows add to the track layout, where
// each lane header and editor sits, and the value readouts (the playhead's, and the selected point's).
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include <algorithm>
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

int TimelineAutomationLanes::addRowHeight() const {
    return std::max(kMinLaneRowHeight,
                    (int)std::llround((double)AddAutomationRow::kBaseHeight * viewState_.rowHeightScale));
}

// A lane's block is its own row plus its modulator rows directly under it. Every geometry function
// below walks lanes through this one helper, so the header column, the lanes region and the layout's
// extra height can never disagree about where a row is.
// A modulator's amount lane has no block of its own: it is the band of that modulator's row.
int TimelineAutomationLanes::laneBlockHeight(const synth::AutomationLane& lane) const {
    if (isAmountLane(lane.id))
        return 0;
    return laneRowHeight() + modulatorCount(lane.id) * modulatorRowHeight();
}

// A track with lanes gets its "+" in the gutter of its last lane header; only a track with none needs a whole row.
bool TimelineAutomationLanes::addRowIsCompact(const synth::Track& track) const {
    for (const auto& lane : track.lanes)
        if (!isAmountLane(lane.id))
            return true;
    return false;
}

int TimelineAutomationLanes::addRowHeightFor(const synth::Track& track) const {
    return addRowIsCompact(track) ? 0 : addRowHeight();
}

std::vector<int> TimelineAutomationLanes::extraHeights() const {
    std::vector<int> extras;
    if (doc_ == nullptr)
        return extras;
    for (const auto& track : doc_->getTracks()) {
        int extra = 0;
        if (isVisibleLane(track)) {
            extra = addRowHeightFor(track);
            for (const auto& lane : track.lanes)
                extra += laneBlockHeight(lane);
        }
        extras.push_back(extra);
    }
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
    const int modHeight = modulatorRowHeight();
    int y = firstRowY;
    int lastLaneY = -1;
    for (const auto& lane : t->lanes) {
        if (auto* header = headerFor(lane.id)) {
            header->setBounds(0, y, width, rowHeight);
            lastLaneY = y;
        }
        for (int i = 0; i < modulatorCount(lane.id); ++i)
            modulatorRowFor(lane.id, i)->setBounds(0, y + rowHeight + i * modHeight, width, modHeight);
        y += laneBlockHeight(lane);
    }
    if (auto* row = addRowFor(track)) {
        const bool compact = addRowIsCompact(*t) && lastLaneY >= 0;
        row->setCompact(compact);
        if (compact) {
            // The empty gutter left of the last lane's colour stripe, centred on that lane's own row.
            const int size = std::min(AddAutomationRow::kCompactSize, rowHeight);
            row->setBounds(AutomationLaneHeaderComponent::kIndent - size - 1, lastLaneY + (rowHeight - size) / 2, size,
                           size);
            row->toFront(false);
        } else {
            row->setBounds(0, y, width, addRowHeight());
        }
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
                return isAmountLane(lane) ? juce::Rectangle<int>()
                                          : juce::Rectangle<int>(0, y, bodies_->getWidth(), rowHeight);
            y += laneBlockHeight(candidate);
        }
    }
    return {};
}

juce::Rectangle<int> TimelineAutomationLanes::modulatorRowContentBounds(synth::LaneId lane, int index,
                                                                        const TimelineRowLayout& layout) const {
    if (index < 0 || index >= modulatorCount(lane))
        return {};
    const auto row = laneRowContentBounds(lane, layout);
    if (row.isEmpty())
        return {};
    const int height = modulatorRowHeight();
    return {0, row.getBottom() + index * height, row.getWidth(), height};
}

juce::Rectangle<int> TimelineAutomationLanes::addRowContentBounds(synth::TrackId track,
                                                                  const TimelineRowLayout& layout) const {
    if (doc_ == nullptr)
        return {};
    const auto& tracks = doc_->getTracks();
    for (int i = 0; i < (int)tracks.size(); ++i) {
        const auto& candidate = tracks[(size_t)i];
        if (candidate.id != track || !isVisibleLane(candidate))
            continue;
        if (addRowIsCompact(candidate))
            return {};
        int y = layout.trackTop(i) + layout.trackRowHeight(i);
        for (const auto& lane : candidate.lanes)
            y += laneBlockHeight(lane);
        return {0, y, bodies_->getWidth(), addRowHeight()};
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
    for (auto& [id, entry] : modulators_)
        for (int i = 0; i < (int)entry.bands.size(); ++i)
            entry.bands[(size_t)i]->setBounds(modulatorRowContentBounds(id, i, layout).translated(0, -scroll));
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

// The header's slot shows the keyboard-cursor point's value when that point is selected, else the
// first selected point's. Read from the doc (a drag commits on release), when the selection or the
// doc changed; a selection that empties and refills within one doc notification repaints nothing.
void TimelineAutomationLanes::updateSelectedReadout(synth::LaneId lane) {
    auto* header = headerFor(lane);
    if (header == nullptr)
        return;
    std::optional<juce::String> text;
    const auto* editor = editorFor(lane);
    const auto* data = doc_ != nullptr ? doc_->getLane(lane) : nullptr;
    if (editor != nullptr && data != nullptr) {
        const auto& selection = editor->getPointSelection();
        const auto cursor = selection.getCursor();
        const auto chosen =
            cursor.has_value() && selection.contains(*cursor)
                ? cursor
                : (selection.isEmpty() ? std::nullopt : std::optional<double>(selection.getSelected().front()));
        if (chosen.has_value())
            for (const auto& p : data->points)
                if (p.beat == *chosen)
                    text = laneValueText(*data, p.value, host_);
    }
    header->setSelectedPointValue(text);
}

} // namespace synth::ui
