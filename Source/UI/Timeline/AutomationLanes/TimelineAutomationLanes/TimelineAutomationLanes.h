#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/EdgeResizeHandle.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReorderDrag/ReorderDragSession.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationRow.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControls.h"
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
    /** The chips every lane header and modulator row shows, and the lane in edit mode (see LaneControlSet). */
    LaneControlSet& getLaneControls() noexcept { return laneControls_; }
    /** Where the chip sets are saved (the user settings); null keeps them in memory. Must outlive this. */
    void setApplicationProperties(juce::ApplicationProperties* props) { laneControls_.setProperties(props); }
    /** The component the "+" row floats on (the timeline panel). */
    void setOverlayParent(juce::Component* parent) noexcept { laneControls_.setOverlayParent(parent); }

    // ---- Fold state ----
    bool isExpanded(synth::TrackId track) const;
    void setExpanded(synth::TrackId track, bool expanded);
    /** Several tracks at once: one sync and one layout change, so their lanes animate together. */
    void setExpanded(const std::vector<synth::TrackId>& tracks, bool expanded);

    // ---- Doc sync and geometry ----
    /** Re-derives the pools and the row geometry from the doc. Call on every doc notification. */
    void sync();
    /** Per-track extra height (expanded lanes) in doc track order, px. */
    std::vector<int> extraHeights() const;
    /** Per-track clip-row height overrides in doc track order (0 = default), px. */
    std::vector<int> rowHeightOverrides() const;
    /** The lane's row at the current zoom and its own height scale, px. */
    int laneRowHeight(synth::LaneId lane) const;
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
    /** The lane whose editor or header is `focused` or holds it, or an invalid id (focusedLane() for any component). */
    synth::LaneId laneOwning(const juce::Component* focused) const;
    /** The source module (sourceUuid) of the modulator row that is `focused` or holds it, or empty; Replace Module acts
     * on it. */
    juce::String modulatorSourceOwning(const juce::Component* focused) const;
    /** The modulator row that is `focused` or holds it, or nullptr. */
    ModulatorRow* modulatorRowOwning(const juce::Component* focused) const;
    /** Replace Module's chord with `focused` inside the timeline: a focused modulator row opens its Change source
     *  picker, a focused lane (header or editor) its Change parameter picker. The two are false when the picker could
     *  not open (a source that cannot be swapped, no free parameter) or when `focused` is in neither. */
    bool canOpenReplacePicker(const juce::Component* focused) const;
    bool openReplacePicker(const juce::Component* focused);
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
    /** While one lives, graph and doc changes do not re-derive the modulator rows; the last to go re-derives them once,
     *  so an edit that replaces routings in several steps reads as one change. An undo or redo step holds them too. */
    class ModulatorHold {
    public:
        explicit ModulatorHold(TimelineAutomationLanes& lanes);
        ~ModulatorHold();

    private:
        TimelineAutomationLanes& lanes_;
        JUCE_DECLARE_NON_COPYABLE(ModulatorHold)
    };
    /** The lane's `index`th modulator row's full (three line) height at the current zoom and its own height scale, px.
     */
    int modulatorRowHeight(synth::LaneId lane, int index) const;
    /** The height of the lane's `index`th modulator row now: full, one line when folded, between mid-tween. */
    int modulatorRowHeightFor(synth::LaneId lane, int index) const;
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

    // ---- Folding and ordering modulator rows (TimelineAutomationLanesModulatorFold.cpp) ----
    /** Fold state of a routing's row, by ModulatorInfo::key(). Runtime-only, never saved; it follows a routing replaced
     *  in place (Change source, Change parameter, their undo) to the routing that took its slot. */
    bool isModulatorCollapsed(const juce::String& key) const;
    bool isModulatorCollapsed(synth::LaneId lane, int index) const;
    /** Folds the row to its title line, or opens it, tweening the height unless the panel is not showing or Reduce
     *  Motion is on (then it lands at once). */
    void setModulatorCollapsed(const juce::String& key, bool collapsed);
    /** Steps the fold tweens in flight by hand (t is the eased progress 0..1; 1 lands them): no VBlank reaches an
     *  off-screen component. */
    void stepModulatorFoldForTest(float t);
    bool isModulatorFoldAnimating() const noexcept { return !foldTweens_.empty(); }
    /** Moves the lane's modulator row stored under `orderKey` (modulatorOrderKey) to `newIndex` among the lane's rows
     *  as one undo step; the one write the keyboard move (and later a drag) goes through. False when it does not
     *  change the order. */
    bool moveModulatorInOrder(synth::LaneId lane, const juce::String& orderKey, int newIndex);
    /** Slides the lane's rows (and their bands) from the y each stood at in `oldYs` (header-list coordinates, by
     *  ModulatorInfo::key()) to where the layout now puts them, 160 ms ease-out; lands at once when the timeline is not
     *  showing or under Reduce Motion. Call it after the layout moved the rows. */
    void glideModulatorRows(synth::LaneId lane, const std::map<juce::String, int>& oldYs);
    bool isModulatorGlideAnimating() const noexcept { return !glideTweens_.empty(); }
    /** Steps the glide in flight by hand (t is the eased progress 0..1; 1 lands it). */
    void stepModulatorGlideForTest(float t);
    /** Moves it by `delta` rows (negative = up), the moved row and the one it swaps with gliding; false at either end.
     */
    bool moveModulatorBy(synth::LaneId lane, const juce::String& orderKey, int delta);

    // ---- Dragging a modulator row (TimelineAutomationLanesModulatorDrag.cpp) ----
    /** Where a dragged modulator row would land if released now. `valid` is false over a lane that can't take it (no CV
     *  jack, or the jack already has the source) or over no lane at all: no slot is shown and the drop changes
     *  nothing. `index` is the row's place among the destination lane's rows after the drop. */
    struct ModulatorDrop {
        synth::LaneId lane;
        int index = -1;
        bool valid = false;
    };
    /** True while a modulator row is lifted or still settling into its slot. */
    bool isModulatorDragActive() const noexcept { return modDrag_.isReordering(); }
    ModulatorDrop modulatorDrop() const noexcept { return modDrop_; }
    /** The line that marks the slot the dragged row would take; visible only over a valid drop. */
    juce::Component& modulatorDropIndicator() noexcept { return *dropIndicator_; }
    /** Esc through the same listener a real key press reaches. */
    bool sendModulatorDragEscapeForTest() { return modDrag_.sendEscapeForTest(); }

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

    // ---- Lane blocks and modulator rows coming and going (TimelineAutomationLanesBlockMotion.cpp) ----
    /** True while a lane block or a modulator row is growing in or shrinking away. */
    bool isBlockMotionAnimating() const noexcept { return blockDriverRunning_; }
    /** Steps the grow and shrink tweens in flight by hand (t is the eased progress 0..1; 1 lands them). */
    void stepBlockMotionForTest(float t);
    /** Lane blocks still shrinking away after their lane left an open track. */
    int leavingLaneCountForTest() const noexcept { return (int)leavingLanes_.size(); }
    /** Pins the lane glide's clock (ms) so a test can step it; empty restores the real clock. */
    void setLaneGlideClockForTest(std::function<double()> clock) { laneDrag_.animator().setClock(std::move(clock)); }
    /** Re-places the gliding lanes from the clock now, as a frame would. */
    void stepLaneGlideForTest();

    // ---- Each row's own height (TimelineAutomationLanesHeight.cpp) ----
    /** Each step of Taller / Shorter multiplies (or divides) the row's height scale by this. */
    static constexpr double kLaneHeightStepFactor = 1.25;
    /** The lane row's height scale now: the drag preview while one runs, else AutomationLane::heightScale. */
    double laneHeightScale(synth::LaneId lane) const;
    /** The height scale of the lane's modulator row stored under `orderKey` (modulatorOrderKey), preview included. */
    double modulatorHeightScale(synth::LaneId lane, const juce::String& orderKey) const;
    /** The strip on the bottom edge of the lane's own row that resizes it, or nullptr when the lane has none. */
    EdgeResizeHandle* heightHandleFor(synth::LaneId lane) const;
    /** The strip on the bottom edge of the lane's `index`th modulator row, or nullptr. */
    EdgeResizeHandle* heightHandleFor(synth::LaneId lane, int modulatorIndex) const;
    /** Taller (+1), shorter (-1) or default (0) as one undo step; false when nothing changed or the lane is gone. */
    bool stepLaneHeight(synth::LaneId lane, int direction);
    /** The same for the lane's modulator row stored under `orderKey`. */
    bool stepModulatorHeight(synth::LaneId lane, const juce::String& orderKey, int direction);
    /** True while a lane's height handle is being dragged. */
    bool isLaneHeightDragActive() const noexcept { return heightDragLane_.isValid(); }

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

    struct FoldTween {
        float from = 1.0f;
        float to = 1.0f;
        float now = 1.0f;
    };
    // A row and band whose routing is gone, fading out in a gap that shrinks from its height to nothing in its old
    // place (before the lane's `slot`th remaining row); swept once both have finished.
    struct LeavingModulator {
        synth::LaneId lane;
        int slot = 0;
        int height = 0;
        FoldTween presence{1.0f, 0.0f, 1.0f};
        std::unique_ptr<ModulatorRow> row;
        std::unique_ptr<ModulatorBand> band;
        std::unique_ptr<synth::ui::FadeVisibility> fade; // declared last: gone before the components it drives
    };
    std::vector<std::unique_ptr<LeavingModulator>> leaving_;

    // A picture of a lane block that left, drawn in its shrinking gap: it never reads the doc its lane is gone from.
    class BlockPicture : public juce::Component {
    public:
        BlockPicture(juce::Image image, float scale);
        void paint(juce::Graphics& g) override;

    private:
        juce::Image image_;
        float scale_ = 1.0f;
    };
    // A lane block that left an open track (deleted, moved to another track, undone), shrinking away after `after` (the
    // lane above it that stayed; invalid for the top).
    struct LeavingLane {
        synth::LaneId lane;
        synth::TrackId track;
        synth::LaneId after;
        int height = 0;
        FoldTween presence{1.0f, 0.0f, 1.0f};
        std::unique_ptr<BlockPicture> header; // in the header column
        std::unique_ptr<BlockPicture> body;   // in the lanes region
    };
    // One block of a track's fold-out in order: a lane (`lane`) or a leaving block (`leaving`), and its height now.
    struct BlockRef {
        const synth::AutomationLane* lane = nullptr;
        const LeavingLane* leaving = nullptr;
        int height = 0;
    };
    std::vector<BlockRef> blocksOf(const synth::Track& track) const;
    bool canAnimateBlocks() const;
    float arrivingPresence(const juce::String& key) const;
    static juce::String laneMotionKey(synth::LaneId lane);
    static juce::String rowMotionKey(synth::LaneId lane, const juce::String& routingKey);
    void growIn(const juce::String& key);
    void startBlockMotion();
    void frameBlockMotion(float t);
    void landBlockMotion();
    void noteLaneBlockChanges(const std::map<synth::TrackId, std::vector<synth::LaneId>>& shown);
    void retireLaneBlock(synth::LaneId lane, synth::TrackId track, synth::LaneId after);
    void placeLeavingLaneHeader(const LeavingLane& leaving, int y, int width);
    void placeLeavingLaneBodies(const TimelineRowLayout& layout);
    int leavingRowsHeightAt(synth::LaneId lane, int slot) const;
    int leavingRowHeight(const LeavingModulator& leaving) const;
    int leavingRowOffset(const LeavingModulator& leaving) const;
    int placeLeavingRows(synth::LaneId lane, int slot, int y, int width);
    void placeLeavingBands(const TimelineRowLayout& layout, int scroll);
    std::map<juce::String, FoldTween> arriving_; // lane blocks (laneMotionKey) and rows (rowMotionKey) growing in
    std::vector<LeavingLane> leavingLanes_;
    std::map<synth::TrackId, std::vector<synth::LaneId>> shownBlocks_; // each open track's lane blocks, last synced
    std::map<synth::LaneId, int> placedBlockHeights_;                  // each lane block's height as last placed
    void glideLanesReorderedByRestore(const std::map<synth::TrackId, std::vector<synth::LaneId>>& before);
    void glideModulatorsReorderedByRestore();
    std::map<synth::LaneId, std::map<juce::String, int>> restoreGlides_; // rows' old y per lane, for an undo's glide
    bool restoredWhileHeld_ = false;  // the held refresh comes from an undo or redo step
    bool glideRestoredOrder_ = false; // set while that refresh runs: a lane whose rows only changed order glides them
    bool blockDriverRunning_ = false;
    std::int64_t replaceCountSeen_ = -1; // the doc's getReplaceCount() at the last pool sync
    int modulatorGlideOffset(const juce::String& key) const;
    void frameModulatorGlide(float t);
    void landModulatorGlide();
    float modulatorOpenness(const juce::String& key) const;
    int modulatorOffset(synth::LaneId lane, int count) const;
    void applyModulatorFold();
    void frameModulatorFold(float t);
    void landModulatorFold();
    bool handleModulatorKey(synth::LaneId lane, const ModulatorInfo& info, const juce::KeyPress& key);
    int laneMoveDirection(const juce::KeyPress& key) const;
    void permuteModulators(LaneModulators& entry, const std::vector<ModulatorInfo>& infos);
    void wireModulatorDrag(ModulatorRow& row, synth::LaneId lane);
    void beginModulatorDrag(synth::LaneId lane, const ModulatorInfo& info, int screenY);
    bool dragModulator(int screenY);
    void endModulatorDrag();
    void commitModulatorDrag();
    void discardModulatorDrag(bool relayout = true);
    void onModulatorDragFrame();
    void updateDropIndicator();
    ModulatorDrop modulatorDropAt(float pointerY) const;
    ModulatorRow* draggedModulatorRow() const;
    float laneBlockStartOf(synth::LaneId lane) const;
    float modulatorSlotStart(synth::LaneId lane, int index) const;
    std::vector<ReorderDragAnimator::Slot> modulatorSlotsFor(synth::LaneId lane) const;
    struct ModulatorDragPose {
        int shift = 0;
        float lift = 0.0f;
    };
    ModulatorDragPose modulatorDragPose(synth::LaneId lane, int index, int restY) const;
    void retireModulatorRows(LaneModulators& entry, synth::LaneId lane, const std::set<juce::String>& staying);
    void sweepLeavingModulators();

    // A row's height handle and drag are keyed by its lane plus its modulatorOrderKey; an empty key is the lane's row.
    using RowKey = std::pair<synth::LaneId, juce::String>;
    bool handleLaneHeightKey(synth::LaneId lane, const juce::KeyPress& key);
    bool handleRowHeightKey(const RowKey& row, const juce::KeyPress& key);
    bool stepRowHeight(const RowKey& row, int direction);
    double rowHeightScale(const RowKey& row) const;
    int rowFullHeight(const RowKey& row) const;
    void placeHeightHandle(const RowKey& row, const juce::String& name, int rowBottom, int width, bool visible);
    void placeLaneHeightHandles(const synth::AutomationLane& lane, int laneY, bool lastBlock, bool visible, int width);
    void pruneHeightHandles(synth::LaneId lane);
    void beginRowHeightDrag(const RowKey& row);
    void dragRowHeight(const RowKey& row, int deltaPx);
    void endRowHeightDrag(const RowKey& row);
    juce::String heightKeysText() const;
    bool isVisibleLane(const synth::Track& track) const;
    int trayExtentOf(synth::LaneId lane) const;
    int trayExtentOf(synth::LaneId lane, int modulatorIndex) const;
    void repositionOverlays();
    bool deriveRoutings();
    void noteReplacedRoutings();
    bool modulatorsHeld() const;
    void releaseHeldModulators();
    int routingDerivations_ = 0;
    bool syncModulators();
    void rekeyModulators(LaneModulators& entry, synth::LaneId lane, const std::vector<ModulatorInfo>& infos,
                         const juce::String& parameterName);
    void rebuildModulators(LaneModulators& entry, synth::LaneId lane, std::vector<ModulatorInfo> infos,
                           const juce::String& parameterName, bool growNewRows);
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

    LaneControlSet laneControls_;                  // outlives the headers and rows whose strips listen to it
    std::set<synth::TrackId> expanded_;            // MIDI/Audio tracks the user opened
    std::set<synth::TrackId> collapsedUnassigned_; // Automation tracks the user closed
    std::map<synth::LaneId, std::unique_ptr<AutomationLaneEditor>> editors_;
    std::map<synth::LaneId, std::unique_ptr<AutomationLaneHeaderComponent>> headers_;
    std::map<RowKey, std::unique_ptr<EdgeResizeHandle>> heightHandles_;   // bottom edge of each lane and modulator row
    std::map<synth::TrackId, std::unique_ptr<AddAutomationRow>> addRows_; // one per track with open lanes
    std::map<synth::LaneId, LaneModulators> modulators_;                  // one per visible lane
    std::set<juce::String> collapsedMods_;                                // routing keys of the folded modulator rows
    std::map<juce::String, FoldTween> glideTweens_; // rows sliding into a new slot: y offset left to travel, by key
    std::map<juce::String, FoldTween> foldTweens_;  // folds in flight, by routing key
    std::map<synth::LaneId, std::vector<ModulatorInfo>> routings_; // what the graph routes into each open lane
    std::map<synth::LaneId, std::vector<ModulatorInfo>>
        shownRoutings_;                            // each lane's routings in row order, last derived
    std::map<juce::String, juce::String> rekeyed_; // routings replaced in place by the last change: old key -> new key
    int modulatorHolds_ = 0;                       // live ModulatorHolds
    bool modulatorsPending_ = false;               // a refresh came while held
    std::shared_ptr<void> restoreHook_;            // undo_'s restore-settled hook, live while held
    std::set<synth::LaneId> amountLanes_;          // lanes drawn as a modulator's amount band
    double lastReadoutBeat_ = -1.0;
    ShortcutManager* shortcuts_ = nullptr;
    juce::VBlankAnimatorUpdater foldUpdater_; // drives the fold tween; declared before the driver that uses it
    AnimationDriver foldDriver_;
    AnimationDriver glideDriver_;
    AnimationDriver blockDriver_; // the grow / shrink of lane blocks and modulator rows

    // The lane reorder drag. Slots are the track's lane blocks (a lane row plus its modulator rows) in header-list
    // coordinates; keys are indices into dragLaneIds_, fixed at press time.
    ReorderDragSession laneDrag_;
    std::vector<synth::LaneId> dragLaneIds_;
    synth::TrackId dragTrack_;
    synth::LaneId liftedLane_;
    std::map<synth::TrackId, int> rowOrigin_; // where each open track's first lane row sits, as last placed

    // The modulator row drag: slots are the source lane's rows (header-list coordinates), keys are indices into
    // dragModKeys_ (ModulatorInfo::key() in row order), fixed at press time. The drop may be on any lane.
    class DropIndicator : public juce::Component {
    public:
        DropIndicator();
        void paint(juce::Graphics& g) override;
    };
    ReorderDragSession modDrag_;
    synth::LaneId dragModLane_;
    ModulatorInfo dragModInfo_;
    std::vector<juce::String> dragModKeys_;
    float dragModGrab_ = 0.0f;
    float dragModPointer_ = 0.0f;
    ModulatorDrop modDrop_;
    juce::String droppedRoutingKey_; // the routing a drop is moving to another lane, while the host moves it
    std::unique_ptr<DropIndicator> dropIndicator_;
    std::unique_ptr<FadeVisibility> dropFade_; // declared after the component it drives
    AnimationDriver dropDriver_;
    float dropFromY_ = 0.0f;
    float dropToY_ = 0.0f;
    int rowWidth_ = 0;

    // The height drag: the row (its lane and modulatorOrderKey, empty for the lane's own row), its full height and
    // scale at press, and the scale shown while it runs. The doc is written once, on release.
    synth::LaneId heightDragLane_;
    juce::String heightDragKey_;
    double heightDragStartScale_ = 1.0;
    int heightDragStartPx_ = 1;
    double heightDragScale_ = 1.0;
};

} // namespace synth::ui
