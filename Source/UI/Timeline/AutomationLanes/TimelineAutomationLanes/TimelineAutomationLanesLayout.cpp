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

// Scaled by the same vertical zoom as the clip rows, so a lane keeps its proportion to its track, and by the lane's
// own height scale (AutomationLane::heightScale, or the drag preview).
int TimelineAutomationLanes::laneRowHeight(synth::LaneId lane) const {
    int base = kDefaultLaneRowHeight;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&headerParent_.getLookAndFeel()))
        base = lf->getTheme().metrics.timelineAutomationLaneRowHeight;
    return std::max(kMinLaneRowHeight,
                    (int)std::llround((double)base * viewState_.rowHeightScale * laneHeightScale(lane)));
}

int TimelineAutomationLanes::addRowHeight() const {
    return std::max(kMinLaneRowHeight,
                    (int)std::llround((double)AddAutomationRow::kBaseHeight * viewState_.rowHeightScale));
}

// A lane's block is its own row plus its modulator rows directly under it. Every geometry function
// below walks lanes through this one helper, so the header column, the lanes region and the layout's
// extra height can never disagree about where a row is.
// A modulator's amount lane has no block of its own: it is the band of that modulator's row. A block that is growing in
// counts only its share so far (TimelineAutomationLanesBlockMotion.cpp).
int TimelineAutomationLanes::laneBlockHeight(const synth::AutomationLane& lane) const {
    if (isAmountLane(lane.id))
        return 0;
    const int full = laneRowHeight(lane.id) + modulatorOffset(lane.id, modulatorCount(lane.id));
    const auto arriving = arriving_.find(laneMotionKey(lane.id));
    if (arriving == arriving_.end())
        return full;
    return std::max(0, (int)std::lround((double)full * (double)arriving->second.now));
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
            for (const auto& block : blocksOf(track))
                extra += block.height;
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
// track's lane headers travel with it rather than with the static layout. While a lane of THIS track is
// being reordered its blocks take their y from the animator (matched by lane id, so the placement survives the
// doc change a drop makes); `staticY` keeps the static layout, which is what the compact "+" and the add row follow.
void TimelineAutomationLanes::placeHeadersFor(synth::TrackId track, int firstRowY, int width) {
    const auto* t = doc_ != nullptr ? doc_->getTrack(track) : nullptr;
    if (t == nullptr || !isVisibleLane(*t))
        return;
    rowOrigin_[track] = firstRowY;
    rowWidth_ = width;
    const bool reordering = laneDrag_.isReordering() && track == dragTrack_;
    int staticY = firstRowY;
    int lastLaneY = -1;
    int lastRowHeight = 0;
    // The track's own resize strip sits on the bottom edge of its last lane block (the Automation track has none).
    synth::LaneId lastShown;
    if (t->kind != synth::TrackKind::Automation)
        for (const auto& lane : t->lanes)
            if (!isAmountLane(lane.id))
                lastShown = lane.id;
    for (const auto& block : blocksOf(*t)) {
        if (block.leaving != nullptr) {
            placeLeavingLaneHeader(*block.leaving, staticY, width);
            staticY += block.height;
            continue;
        }
        const auto& lane = *block.lane;
        const int rowHeight = laneRowHeight(lane.id);
        int y = staticY;
        float lift = 0.0f;
        const auto found = std::find(dragLaneIds_.begin(), dragLaneIds_.end(), lane.id);
        const int key = reordering && found != dragLaneIds_.end() ? static_cast<int>(found - dragLaneIds_.begin()) : -1;
        if (key >= 0) {
            const auto& animator = laneDrag_.animator();
            const bool dragged = key == animator.getDraggedKey();
            y = static_cast<int>(std::lround(dragged ? animator.getDraggedStart() : animator.getLayoutStart(key)));
            lift = dragged ? animator.getLift() : 0.0f;
        }
        // A block growing in shows what fits so far: everything in it is cut at its bottom edge.
        const bool growing = arriving_.count(laneMotionKey(lane.id)) > 0;
        const int blockBottom = y + block.height;
        const auto fit = [growing, blockBottom](int top, int height) {
            return growing ? std::clamp(blockBottom - top, 0, height) : height;
        };
        if (auto* header = headerFor(lane.id)) {
            header->setBounds(0, y, width, fit(y, rowHeight));
            header->setLift(lift);
            lastLaneY = staticY;
            lastRowHeight = rowHeight;
            placedBlockHeights_[lane.id] = block.height;
        }
        int modY = y + rowHeight + trayExtentOf(lane.id);
        for (int i = 0; i < modulatorCount(lane.id); ++i) {
            modY += placeLeavingRows(lane.id, i, modY, width);
            auto* row = modulatorRowFor(lane.id, i);
            const int height = modulatorRowHeightFor(lane.id, i);
            row->setTitleLineHeight(modulatorRowHeight(lane.id, i) / 3);
            const auto pose = modulatorDragPose(lane.id, i, modY);
            const int rowY = modY + modulatorGlideOffset(row->getInfo().key()) + pose.shift;
            row->setBounds(0, rowY, width, fit(rowY, height));
            row->setLift(pose.lift);
            if (pose.lift > 0.0f)
                row->toFront(false);
            modY += height + trayExtentOf(lane.id, i);
        }
        placeLeavingRows(lane.id, modulatorCount(lane.id), modY, width);
        if (headerFor(lane.id) != nullptr)
            placeLaneHeightHandles(lane, y, lane.id == lastShown,
                                   !laneDrag_.isReordering() && !growing && !modDrag_.isReordering(), width);
        if (lift > 0.0f) {
            if (auto* header = headerFor(lane.id))
                header->toFront(false);
            for (int i = 0; i < modulatorCount(lane.id); ++i)
                modulatorRowFor(lane.id, i)->toFront(false);
        }
        staticY += block.height;
    }
    if (auto* row = addRowFor(track)) {
        const bool compact = addRowIsCompact(*t) && lastLaneY >= 0;
        row->setCompact(compact);
        if (compact) {
            // The empty gutter left of the last lane's colour stripe, centred on that lane's own row.
            const int size = std::min(AddAutomationRow::kCompactSize, lastRowHeight);
            row->setBounds(AutomationLaneHeaderComponent::kIndent - size - 1, lastLaneY + (lastRowHeight - size) / 2,
                           size, size);
            row->toFront(false);
        } else {
            row->setBounds(0, staticY, width, addRowHeight());
        }
    }
}

juce::Rectangle<int> TimelineAutomationLanes::laneRowContentBounds(synth::LaneId lane,
                                                                   const TimelineRowLayout& layout) const {
    if (doc_ == nullptr)
        return {};
    const auto& tracks = doc_->getTracks();
    for (int i = 0; i < (int)tracks.size(); ++i) {
        const auto& track = tracks[(size_t)i];
        if (!isVisibleLane(track))
            continue;
        int y = layout.trackTop(i) + layout.trackRowHeight(i);
        for (const auto& block : blocksOf(track)) {
            if (block.lane != nullptr && block.lane->id == lane) {
                if (isAmountLane(lane))
                    return {};
                const int rowHeight = laneRowHeight(lane);
                const bool growing = arriving_.count(laneMotionKey(lane)) > 0;
                return {0, y, bodies_->getWidth(), growing ? std::min(rowHeight, block.height) : rowHeight};
            }
            y += block.height;
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
    const int top = row.getY() + laneRowHeight(lane) + modulatorOffset(lane, index);
    int height = modulatorRowHeightFor(lane, index);
    if (arriving_.count(laneMotionKey(lane)) > 0) // a block growing in: cut at its bottom edge
        if (const auto* l = doc_->getLane(lane))
            height = std::clamp(row.getY() + laneBlockHeight(*l) - top, 0, height);
    return {0, top, row.getWidth(), height};
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
        for (const auto& block : blocksOf(candidate))
            y += block.height;
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
        for (int i = 0; i < (int)entry.bands.size(); ++i) {
            const auto rowBounds = modulatorRowContentBounds(id, i, layout);
            const auto pose = modulatorDragPose(id, i, rowBounds.getY());
            entry.bands[(size_t)i]->setBounds(
                rowBounds.translated(0, -scroll + modulatorGlideOffset(entry.infos[(size_t)i].key()) + pose.shift));
            if (pose.lift > 0.0f)
                entry.bands[(size_t)i]->toFront(false);
        }
    placeLeavingBands(layout, scroll);
    placeLeavingLaneBodies(layout);
    repositionOverlays();
}

// An open "+" row floats beside its header over the lanes region, so it follows the header when the list scrolls.
void TimelineAutomationLanes::repositionOverlays() {
    for (auto& [id, header] : headers_)
        header->getControlStrip().repositionRow();
    for (auto& [id, entry] : modulators_)
        for (auto& row : entry.rows)
            row->getControlStrip().repositionRow();
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
