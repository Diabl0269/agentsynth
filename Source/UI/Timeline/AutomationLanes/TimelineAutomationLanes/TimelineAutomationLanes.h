#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReorderDrag/ReorderDragSession.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationRow.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/LaneRangeSelection.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorBand.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"
#include "UI/Timeline/EditTool.h"
#include "UI/Timeline/TimelineRowLayout.h"
#include "UI/Timeline/TimelineViewState.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

class AppUndoManager;  // Forward declaration (Source/AppUndoManager.h)
class ShortcutManager; // Forward declaration (Source/ShortcutManager/ShortcutManager.h)

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
    /** The bindings Move Lane Up/Down read; null falls back to Cmd+Alt+Up/Down. */
    void setShortcuts(ShortcutManager* shortcuts) noexcept { shortcuts_ = shortcuts; }

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
    /** True when `track` has a lane block, so its add button is the small "+" in the last lane header's gutter instead
     *  of a row of its own. */
    bool addRowIsCompact(const synth::Track& track) const;
    /** The height the add button adds to `track`'s fold-out: a row's, or 0 when it is the compact "+". */
    int addRowHeightFor(const synth::Track& track) const;

    /** Places the lane headers of `track` with their first row at content y `firstRowY`. */
    void placeHeadersFor(synth::TrackId track, int firstRowY, int width);
    /** Places every visible editor from `layout`, offset by the vertical scroll. */
    void placeBodies(const TimelineRowLayout& layout);
    /** The lane's row in content coordinates (before scroll); empty when it is not visible. */
    juce::Rectangle<int> laneRowContentBounds(synth::LaneId lane, const TimelineRowLayout& layout) const;
    /** The track's "+ Add automation..." row in content coordinates; empty when it has none (or is the compact "+"). */
    juce::Rectangle<int> addRowContentBounds(synth::TrackId track, const TimelineRowLayout& layout) const;

    // ---- Lane access ----
    AutomationLaneEditor* editorFor(synth::LaneId lane) const;
    /** The lane editor that holds keyboard focus, or nullptr; the edit commands (Cmd+A/C/X/V) act on it. */
    AutomationLaneEditor* focusedEditor() const;
    /** The lane whose editor OR header holds keyboard focus, or an invalid id; the lane commands (Duplicate) act on it.
     */
    synth::LaneId focusedLane() const;
    /** Opens the Duplicate picker of `lane`'s header; false when the lane has no header on screen. */
    bool requestDuplicateLane(synth::LaneId lane);
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
    /** Rows still fading out after their routing went. */
    int leavingModulatorCountForTest() const { return (int)leaving_.size(); }
    /** The lane's `index`th modulator row / band, or nullptr. */
    ModulatorRow* modulatorRowFor(synth::LaneId lane, int index) const;
    ModulatorBand* modulatorBandFor(synth::LaneId lane, int index) const;
    /** The lane's `index`th modulator row in content coordinates; empty when it is not visible. */
    juce::Rectangle<int> modulatorRowContentBounds(synth::LaneId lane, int index,
                                                   const TimelineRowLayout& layout) const;
    /**
     * True when `lane` is a modulator's amount lane (ModulatorAmountLane.h), drawn as that modulator row's
     * band instead of as a lane row. It is a lane row again the moment its routing is gone.
     */
    bool isAmountLane(synth::LaneId lane) const;
    /** How many of `track`'s lanes are shown as amount bands rather than as lane rows. */
    int hiddenLaneCount(synth::TrackId track) const;

    // ---- Reordering lanes within their track (TimelineAutomationLanesReorder.cpp) ----
    /** Moves `lane` by `delta` visible lanes (negative = up) within its track as one undo step, gliding the header
     *  like a drop does; false at either end or when the lane is not shown. */
    bool moveLaneBy(synth::LaneId lane, int delta);
    /** True while a lane header is lifted or still settling into its slot. */
    bool isLaneReorderActive() const noexcept { return laneDrag_.isReordering(); }
    /** Esc through the same listener a real key press reaches. */
    bool sendLaneDragEscapeForTest() { return laneDrag_.sendEscapeForTest(); }
    /** How many times the modulator routings were asked of the graph since this was made. */
    int getRoutingDerivationsForTest() const noexcept { return routingDerivations_; }
    /** The lane whose header is drawn lifted, or an invalid id. */
    synth::LaneId liftedLane() const noexcept { return laneDrag_.isReordering() ? liftedLane_ : synth::LaneId(); }

    // ---- Draw shapes and the lane range (TimelineAutomationLanesShapes.cpp) ----
    void setDrawShape(DrawShape shape);
    /** The one lane range across every lane. */
    LaneRangeSelection& getLaneRange() noexcept { return laneRange_; }
    /** True while a lane range with width sits on a lane that is on screen. */
    bool hasLaneRange() const;
    /** Stamps `shape` over the lane range (one undo step); false when nothing changed. */
    bool stampShapeOnLaneRange(DrawShape shape);
    /** Removes the points inside the lane range (one undo step); false when nothing changed. */
    bool deleteLaneRangePoints();
    /** Fired after the lane range changes in any way. */
    std::function<void()> onLaneRangeChanged;

    /** Fired after a fold toggle or a change in which lanes are visible, so the panel relayouts. */
    std::function<void()> onLayoutChanged;
    /** Fired when a track's "+ Add automation..." row is pressed; the panel opens the picker on it. */
    std::function<void(synth::TrackId, juce::Component&)> onAddAutomationRequested;
    /** The keyboard stops directly under `track` while its lanes are open: each lane header, then its modulator rows,
     * in doc order; empty for a folded track. Amount lanes have no row, so none. */
    std::vector<juce::Component*> keyboardStopsFor(const synth::Track& track) const;
    /** A lane header or modulator row (the first argument) asked for focus to move up (-1) or down (+1). */
    std::function<void(juce::Component&, int direction)> onFocusMoveRequested;
    /** A lane header or modulator row took keyboard focus. */
    std::function<void(juce::Component&)> onKeyboardStopFocused;

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
        // A row and its band fade in together when the routing is new (animation.md); declared after both, so the
        // fades are gone before the components they drive.
        std::vector<std::unique_ptr<synth::ui::FadeVisibility>> fades;
    };

    // A row and band whose routing is gone, fading out where they stood; swept once hidden.
    struct LeavingModulator {
        std::unique_ptr<ModulatorRow> row;
        std::unique_ptr<ModulatorBand> band;
        std::unique_ptr<synth::ui::FadeVisibility> fade; // declared last: gone before the components it drives
    };
    std::vector<std::unique_ptr<LeavingModulator>> leaving_;
    void retireModulatorRows(LaneModulators& entry, const std::set<juce::String>& staying);
    void sweepLeavingModulators();

    bool isVisibleLane(const synth::Track& track) const;
    bool deriveRoutings();
    int routingDerivations_ = 0;
    bool syncModulators();
    void rebuildModulators(LaneModulators& entry, synth::LaneId lane, std::vector<ModulatorInfo> infos,
                           const juce::String& parameterName);
    void wireBand(ModulatorBand& band) const;
    void refreshModulatorAmounts(int visibleTop, int visibleBottom);
    int laneBlockHeight(const synth::AutomationLane& lane) const;
    void syncPools();
    void wireHeader(AutomationLaneHeaderComponent& header, synth::LaneId id);
    bool createCustomLfoFromRange(synth::LaneId lane, double startBeat, double endBeat);
    bool handleLaneKey(synth::LaneId lane, const juce::KeyPress& key);
    void beginLaneDrag(synth::LaneId lane, int screenY);
    void dragLane(int screenY);
    bool endLaneDrag();
    void commitLaneDrag();
    void discardLaneDrag();
    void onLaneDragFrame();
    float laneDragPointerY(int screenY) const;
    std::vector<synth::LaneId> shownLanesOf(const synth::Track& track) const;
    std::vector<float> laneStartsFor(const synth::Track& track, const std::vector<synth::LaneId>& ids) const;
    void syncAddRows();
    void refreshPooled();
    void updateSelectedReadout(synth::LaneId lane);
    juce::Colour unassignedColour() const;
    void laneRangeChanged();

    TimelineViewState& viewState_;
    juce::Component& headerParent_;
    std::unique_ptr<Bodies> bodies_;
    synth::TimelineDoc* doc_ = nullptr;
    TrackHeaderHost* host_ = nullptr;
    AppUndoManager* undo_ = nullptr;
    synth::TransportService* transport_ = nullptr;
    EditTool editTool_ = EditTool::Select;
    DrawShape drawShape_ = DrawShape::Free;
    LaneRangeSelection laneRange_;
    LanePointClipboard pointClipboard_; // Cmd+C/X/V across every lane editor

    std::set<synth::TrackId> expanded_;            // MIDI/Audio tracks the user opened
    std::set<synth::TrackId> collapsedUnassigned_; // Automation tracks the user closed
    std::map<synth::LaneId, std::unique_ptr<AutomationLaneEditor>> editors_;
    std::map<synth::LaneId, std::unique_ptr<AutomationLaneHeaderComponent>> headers_;
    std::map<synth::TrackId, std::unique_ptr<AddAutomationRow>> addRows_; // one per track with open lanes
    std::map<synth::LaneId, LaneModulators> modulators_;                  // one per visible lane
    std::map<synth::LaneId, std::vector<ModulatorInfo>> routings_;        // what the graph routes into each open lane
    std::set<synth::LaneId> amountLanes_;                                 // lanes drawn as a modulator's amount band
    double lastReadoutBeat_ = -1.0;
    ShortcutManager* shortcuts_ = nullptr;

    // The lane reorder drag. Slots are the track's lane blocks (a lane row plus its modulator rows) in header-list
    // coordinates; keys are indices into dragLaneIds_, fixed at press time.
    ReorderDragSession laneDrag_;
    std::vector<synth::LaneId> dragLaneIds_;
    synth::TrackId dragTrack_;
    synth::LaneId liftedLane_;
    std::map<synth::TrackId, int> rowOrigin_; // where each open track's first lane row sits, as last placed
    int rowWidth_ = 0;
};

} // namespace synth::ui
