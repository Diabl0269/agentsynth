#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationRow.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"
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
    /** The "+ Add automation..." row closing an open track's lanes (zoom-scaled like a lane row), px. */
    int addRowHeight() const;

    /** Places the lane headers of `track` with their first row at content y `firstRowY`. */
    void placeHeadersFor(synth::TrackId track, int firstRowY, int width);
    /** Places every visible editor from `layout`, offset by the vertical scroll. */
    void placeBodies(const TimelineRowLayout& layout);
    /** The lane's row in content coordinates (before scroll); empty when it is not visible. */
    juce::Rectangle<int> laneRowContentBounds(synth::LaneId lane, const TimelineRowLayout& layout) const;
    /** The track's "+ Add automation..." row in content coordinates; empty when it has none. */
    juce::Rectangle<int> addRowContentBounds(synth::TrackId track, const TimelineRowLayout& layout) const;

    // ---- Lane access ----
    AutomationLaneEditor* editorFor(synth::LaneId lane) const;
    AutomationLaneHeaderComponent* headerFor(synth::LaneId lane) const;
    /** The track's "+ Add automation..." row while its lanes are open, else nullptr. */
    AddAutomationRow* addRowFor(synth::TrackId track) const;
    void repaintEditors();

    /** Re-evaluates readouts of headers inside content rows [visibleTop, visibleBottom) at `beat`. */
    void tickReadouts(double beat, int visibleTop, int visibleBottom);

    // ---- Modulator rows (derived from the graph through the host; TimelineAutomationLanesModulators.cpp) ----
    /** Re-derives every visible lane's modulator rows. Call after any graph change. */
    void refreshModulators();
    /** A modulator row's height at the current zoom, px. */
    int modulatorRowHeight() const;
    /** Re-reads the live values of modulator rows inside content rows [visibleTop, visibleBottom). */
    void tickModulatorValues(int visibleTop, int visibleBottom);
    int modulatorCount(synth::LaneId lane) const;
    /** The lane's `index`th modulator row / band, or nullptr. */
    ModulatorRow* modulatorRowFor(synth::LaneId lane, int index) const;
    ModulatorBand* modulatorBandFor(synth::LaneId lane, int index) const;
    /** The lane's `index`th modulator row in content coordinates; empty when it is not visible. */
    juce::Rectangle<int> modulatorRowContentBounds(synth::LaneId lane, int index,
                                                   const TimelineRowLayout& layout) const;

    /** Fired after a fold toggle or a change in which lanes are visible, so the panel relayouts. */
    std::function<void()> onLayoutChanged;
    /** Fired when a track's "+ Add automation..." row is pressed; the panel opens the picker on it. */
    std::function<void(synth::TrackId, juce::Component&)> onAddAutomationRequested;
    /** Fired when a lane editor takes keyboard focus. */
    std::function<void(synth::LaneId)> onLaneFocused;

private:
    struct Bodies : juce::Component {
        Bodies();
    };

    // One lane's modulator rows: the routings they were built from, a row in the header column and a
    // band in the lanes region per routing, all in graph order.
    struct LaneModulators {
        std::vector<ModulatorInfo> infos;
        std::vector<std::unique_ptr<ModulatorRow>> rows;
        std::vector<std::unique_ptr<ModulatorBand>> bands;
    };

    bool isVisibleLane(const synth::Track& track) const;
    bool syncModulators();
    void rebuildModulators(LaneModulators& entry, std::vector<ModulatorInfo> infos, const juce::String& parameterName);
    int laneBlockHeight(const synth::AutomationLane& lane) const;
    void syncPools();
    void syncAddRows();
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
    std::map<synth::TrackId, std::unique_ptr<AddAutomationRow>> addRows_; // one per track with open lanes
    std::map<synth::LaneId, LaneModulators> modulators_;                  // one per visible lane
    double lastReadoutBeat_ = -1.0;
};

} // namespace synth::ui
