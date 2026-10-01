#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"
#include "UI/Timeline/EditTool.h"
#include "UI/Timeline/TimelineRowLayout.h"
#include "UI/Timeline/TimelineViewState.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth {
class TransportService;
}

namespace synth::ui {

struct TrackHeaderHost;

// The automation lanes that fold out under their tracks in the timeline panel: which tracks are
// open, the row geometry that adds to the track layout, and one lane header (header column) plus
// one curve editor (lanes region) per visible lane, pooled by LaneId across doc changes.
//
// The panel owns one, adds getBodies() over its lanes region and hands it the header list the
// lane headers live in. Fold state is runtime-only (never saved). Message thread only.
class TimelineAutomationLanes {
public:
    // The Automation track's section-header row, px (not zoom-scaled).
    static constexpr int kSectionRowHeight = 26;

    /** `headerParent` holds the lane headers and must outlive this object. */
    TimelineAutomationLanes(TimelineViewState& viewState, juce::Component& headerParent);
    ~TimelineAutomationLanes();

    // ---- Wiring (all non-owning, may be null) ----
    void setTimelineDoc(synth::TimelineDoc* doc);
    void setHost(TrackHeaderHost* host);
    void setUndoManager(AppUndoManager* undo);
    void setTransport(synth::TransportService* transport);
    void setEditTool(EditTool tool);

    /** The container the lane editors live in; the panel sizes it to its lanes region. */
    juce::Component& getBodies() noexcept;

    // ---- Fold state ----
    bool isExpanded(synth::TrackId track) const;
    void setExpanded(synth::TrackId track, bool expanded);

    // ---- Doc sync and geometry ----
    /** Re-derives the pools and the row geometry from the doc. Call on every doc notification. */
    void sync();
    /** Per-track extra height (expanded lanes) in doc track order, px. */
    std::vector<int> extraHeights() const;
    /** Per-track clip-row height overrides in doc track order (0 = default), px. */
    std::vector<int> rowHeightOverrides() const;
    int laneRowHeight() const;

    /** Places the lane headers of `track` with their first row at content y `firstRowY`. */
    void placeHeadersFor(synth::TrackId track, int firstRowY, int width);
    /** Places every visible editor from `layout`, offset by the vertical scroll. */
    void placeBodies(const TimelineRowLayout& layout);
    /** The lane's row in content coordinates (before scroll); empty when it is not visible. */
    juce::Rectangle<int> laneRowContentBounds(synth::LaneId lane, const TimelineRowLayout& layout) const;

    // ---- Lane access ----
    AutomationLaneEditor* editorFor(synth::LaneId lane) const;
    AutomationLaneHeaderComponent* headerFor(synth::LaneId lane) const;
    void repaintEditors();

    /** Re-evaluates readouts of headers inside content rows [visibleTop, visibleBottom) at `beat`. */
    void tickReadouts(double beat, int visibleTop, int visibleBottom);

    /** Fired after a fold toggle or a change in which lanes are visible, so the panel relayouts. */
    std::function<void()> onLayoutChanged;
    /** Fired when a lane editor takes keyboard focus. */
    std::function<void(synth::LaneId)> onLaneFocused;

private:
    struct Bodies : juce::Component {
        Bodies();
    };

    bool isVisibleLane(const synth::Track& track) const;
    void syncPools();
    void refreshPooled();
    juce::Colour unassignedColour() const;

    TimelineViewState& viewState_;
    juce::Component& headerParent_;
    std::unique_ptr<Bodies> bodies_;
    synth::TimelineDoc* doc_ = nullptr;
    TrackHeaderHost* host_ = nullptr;
    AppUndoManager* undo_ = nullptr;
    synth::TransportService* transport_ = nullptr;
    EditTool editTool_ = EditTool::Select;

    std::set<synth::TrackId> expanded_;            // MIDI/Audio tracks the user opened
    std::set<synth::TrackId> collapsedUnassigned_; // Automation tracks the user closed
    std::map<synth::LaneId, std::unique_ptr<AutomationLaneEditor>> editors_;
    std::map<synth::LaneId, std::unique_ptr<AutomationLaneHeaderComponent>> headers_;
    double lastReadoutBeat_ = -1.0;
};

} // namespace synth::ui
