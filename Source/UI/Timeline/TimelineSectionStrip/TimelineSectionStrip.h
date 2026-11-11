#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Layout/KeyGlide/RestoreWatch.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Timeline/TimelineSectionStrip/SectionDropPlan.h"
#include "UI/Timeline/TimelineSectionStrip/SectionStripGeometry.h"
#include "UI/Timeline/TimelineSectionStrip/SectionStripMotion.h"
#include "UI/Timeline/TimelineViewState.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace synth {
class TransportService;
}

class AppUndoManager;
class ShortcutManager;

// The Sections strip: a 24 px row across the top of the lanes, directly above the ruler and sharing its x origin
// (beat -> x through the panel's TimelineViewState). It draws every placement of every section as a block (the
// section's colour, its name, and "2/3" when the section plays more than once) and is where sections are made,
// renamed, moved, placed again and deleted. docs/timeline/sections.md ("What the user does") is the user-facing
// description; the model it edits is synth::TimelineDoc's "-- Sections --" block.
//
// Like the ruler it owns nothing and never listens to the doc: TimelinePanelComponent::timelineChanged calls
// refresh(), and an edit made here refreshes itself too. No timer, no polling.
//
// Split by concern: TimelineSectionStrip.cpp (state, painting, blocks), ...Mouse.cpp (make by drag, block drag and the
// other pointer gestures), ...Menu.cpp (block and strip menus and their headless seams), ...Rename.cpp (the in-place
// name editor), ...Accessibility.cpp (one proxy per block, tooltips, the test-mode probe), ...Keys.cpp (keyboard),
// ...Motion.cpp (what animates), ...Variations.cpp (the letter chip's menu and its edits), ...Selection.cpp (which
// blocks are selected and the verbs on the whole set).
namespace synth::ui {

template <typename T>
class KeyedTween;
using KeyedPointTween = KeyedTween<juce::Point<float>>;
template <typename Id>
class ListSelectionModel;

class TimelineSectionStrip
    : public juce::Component
    , public juce::TooltipClient
    , public KeyboardContextMenuProvider {
public:
    explicit TimelineSectionStrip(TimelineViewState& viewState);
    ~TimelineSectionStrip() override;

    // ---- Wiring (all non-owning, all may stay null: the strip then draws and does nothing) ----
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    void setTimelineDoc(synth::TimelineDoc* doc);
    void setUndoManager(AppUndoManager* undoManager) noexcept { undoManager_ = undoManager; }
    // The live binding of the Make Section action, for the tooltip. The panel clears it before the manager goes.
    void setShortcutManager(const ShortcutManager* shortcuts) noexcept { shortcuts_ = shortcuts; }
    // Where the colour picker's favourites persist, and where the "placing covers the music" setting is read from.
    void setPropertiesFile(juce::PropertiesFile* props) noexcept { propertiesFile_ = props; }
    // "Select its clips" hands the ids of every clip that starts inside the placement here; the panel puts them in the
    // clip lanes' selection (and clears a live range, which never coexists with one).
    std::function<void(const std::vector<synth::ClipId>&)> onSelectClips;
    // A drag held at the strip's left or right edge scrolled the shared view; the panel repaints what reads it.
    std::function<void()> onViewScrolledByDrag;
    // What the Make Section key would make a section from right now ("the selected bars", "the loop"), or empty when
    // it can do nothing; the empty-strip tooltip names the key only when this is not empty.
    std::function<juce::String()> makeSectionKeySource;

    // Re-reads the doc: drops a selection whose placement is gone, rebuilds the accessibility proxies when the blocks
    // changed, repaints. Called by the panel on every doc notification.
    void refresh();

    // ---- Blocks (paint and hit-testing walk this one list) ----
    struct Block {
        synth::PlacementId id;
        synth::SectionId section;
        juce::String name;
        juce::Colour colour;
        double startBeat = 0.0;
        double lengthBeats = 0.0;
        int ordinal = 1;        // 1-based position among the section's placements
        int count = 1;          // how many times the section plays
        int variation = 0;      // the letter this placement plays
        int variationCount = 1; // how many variations the section has
        juce::Rectangle<float> bounds;
    };
    // Every placement whose block intersects the visible strip, in doc order.
    std::vector<Block> buildBlocks() const;
    synth::PlacementId blockAt(juce::Point<float> pos) const;
    // The block under `pos` when `pos` is on its variation chip, else an invalid id.
    synth::PlacementId chipAt(juce::Point<float> pos) const;

    // ---- Selection (TimelineSectionStripSelection.cpp): any set of placements, Finder rules ----
    // The block the keys and the one-block verbs act on: the anchor while it is selected, else the first selected.
    synth::PlacementId getSelectedPlacement() const;
    // Every selected placement, in doc order.
    std::vector<synth::PlacementId> getSelectedPlacements() const;
    bool isPlacementSelected(synth::PlacementId id) const;
    // Selects just `id` (an invalid id clears).
    void selectPlacement(synth::PlacementId id);
    void togglePlacementSelected(synth::PlacementId id);    // Cmd-click
    void extendPlacementSelectionTo(synth::PlacementId id); // Shift-click: the run from the anchor to `id`
    void selectAllPlacements();                             // Cmd+A
    void selectPlacements(const std::vector<synth::PlacementId>& ids, synth::PlacementId anchor);

    // ---- Creating ----
    /** One undo step: makes a section from [startBeat, endBeat) named "Section N" in the next palette colour, selects
     * it and opens its name editor. Returns the new placement, or an invalid id when the model refuses (nothing
     * changes). */
    synth::PlacementId makeSectionFromRange(double startBeat, double endBeat);

    // ---- Menus ----
    enum class SectionMenuChoice {
        Rename,
        ChangeColour,
        PlaceAgainAfter,
        PlaceAgainAtEnd,
        MakeUnique,
        SelectClips,
        RemoveKeepMusic,
        DeleteWithBars
    };
    enum class StripMenuChoice { MakeFromMarkers };

    /** Headless seam for the block menu: applies one item to the placement that was right-clicked. Each doc edit is one
     *  undo step. False when the choice did nothing (an unknown placement, Make unique on a section that plays once).
     */
    bool applySectionMenuChoice(synth::PlacementId id, SectionMenuChoice choice);
    bool applyStripMenuChoice(StripMenuChoice choice);
    // Whether the menu offers `choice` for `id` (Make unique needs a section that plays more than once).
    bool isSectionMenuChoiceEnabled(synth::PlacementId id, SectionMenuChoice choice) const;

    // ---- Variations (TimelineSectionStripVariations.cpp) ----
    enum class VariationChoice { Switch, NewVariation, PlayEverywhere, RemoveVariation };
    /** Headless seam for the variation menu. `letter` is the target of Switch and is ignored by the others, which act
     *  on the letter `id` plays now. Each applied choice is one undo step and outlines the rewritten placements. False
     *  when it did nothing. */
    bool applyVariationMenuChoice(synth::PlacementId id, VariationChoice choice, int letter = 0);
    bool isVariationChoiceEnabled(synth::PlacementId id, VariationChoice choice) const;
    /** The radio row for variation `letter` of `id`'s section: "B (Lead)" with the names of the tracks it owns, "B"
     *  for A or a full copy. */
    juce::String variationRowLabel(synth::PlacementId id, int letter) const;
    /** The text of New / Play everywhere / Remove for `id` ("Play B in every Verse"); empty for Switch. */
    juce::String variationChoiceLabel(synth::PlacementId id, VariationChoice choice) const;
    // Adds the variation items to `menu` (the block menu's submenu and the chip's menu are the same list).
    void addVariationItems(juce::PopupMenu& menu, synth::PlacementId id);
    // Outlines these placements as an edit that rewrote them would (400 ms, 80 ms fade under Reduce Motion).
    void outlinePlacements(const std::vector<synth::PlacementId>& placements);

    // ---- Renaming ----
    // Opens the in-place name editor over the block. No-op when the id does not resolve or the block is off screen.
    void beginRenameSection(synth::PlacementId id);
    juce::TextEditor* getRenameEditorForTest() const noexcept { return renameEditor_.get(); }
    /** The colour picker with the exact callbacks the menu uses, without launching a call-out. Null when `id` is gone.
     */
    std::unique_ptr<synth::ui::ColourPickerPopup> createColourPickerForTest(synth::PlacementId id) {
        return buildColourPicker(id);
    }

    // ---- The two settings this strip reads at use time ----
    // The mode a drop or a menu "Place again" uses: MakeRoom, or Cover when the Settings toggle is on. `flip` swaps it.
    synth::SectionDropMode dropMode(bool flip = false) const;

    // ---- Component ----
    void paint(juce::Graphics& g) override;
    // The strip's edge line for every placement start, drawn by the panel through the lanes below (25 px stems like
    // the ruler's marker stems). `lanes` is the lanes rect in the panel's coordinates.
    void paintLaneEdges(juce::Graphics& g, juce::Rectangle<int> lanes) const;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void modifierKeysChanged(const juce::ModifierKeys& mods) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    // Shift+F10 / the menu key: the selected block's menu. False when no block is selected.
    bool showContextMenuForKeyboardFocus() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    // ---- Tooltips and accessibility ----
    // What the tooltip says at `pos`: the block's, else the empty-strip one. Never empty.
    juce::String tooltipAt(juce::Point<float> pos) const;
    juce::String getTooltip() override;
    int getProxyCountForTest() const noexcept { return (int)proxies_.size(); }
    juce::String getProxyTitleForTest(int index) const;
    juce::String getProxyDescriptionForTest(int index) const;
    // What proxy `index` reports to a screen reader as its selected state.
    bool isProxySelectedForTest(int index) const;

    // ---- Test seams for the gestures ----
    bool isMakeDraggingForTest() const noexcept { return makeDragging_; }
    // The span a make-drag in flight would commit, when one is in flight.
    sectionstrip::Span getMakeDragSpanForTest() const noexcept { return makeDragSpan_; }
    bool isBlockDraggingForTest() const noexcept { return blockDragging_; }
    bool isBlockDragCopyForTest() const noexcept { return dragCopy_; }
    // The words of the hint the drop line carries now ("Release to ..."); empty unless a drag in flight shows it.
    juce::String getDropHintTextForTest() const { return dropHintShown() ? dropHint_ : juce::String(); }
    juce::Rectangle<float> getDropHintBoundsForTest() const { return dropHintPillRect(); }
    // What the block menu's "Place again after this" says with the current setting.
    juce::String getPlaceAgainAfterMenuTextForTest() const { return placeAgainAfterMenuText(); }
    // The beat a block drag in flight would drop its start on (what the accent drop line shows).
    double getBlockDragTargetBeatForTest() const noexcept { return dragPlan_.landing; }
    // The whole plan of the drag in flight: every other block's start after the drop and what a cover drop removes.
    const sectionstrip::DropPlan& getBlockDragPlanForTest() const noexcept { return dragPlan_; }
    // Where a block is drawn now, in beats: its start, eased toward where the drag in flight moves it.
    double getDrawnStartBeatForTest(synth::PlacementId id) const;
    juce::Rectangle<float> getDragGhostBoundsForTest() const;
    float getDragGhostAlphaForTest() const { return dragGhostAlpha(); }
    // The placements a drag in flight carries, in time order (one unless a multi-selection is dragged).
    std::vector<synth::PlacementId> getDragMembersForTest() const;
    // Where the ghost of drag member `index` is drawn now (the grabbed one is getDragGhostBoundsForTest()).
    juce::Rectangle<float> getDragMemberGhostBoundsForTest(int index) const;
    // One edge-scroll tick, as the timer would run it.
    void tickEdgeScrollForTest() { edgeScrollTick(); }
    bool isEdgeScrollRunningForTest() const noexcept { return edgeScroll_.isTimerRunning(); }
    KeyedPointTween& getDropGlideForTest() noexcept { return *dropGlide_; }
    SectionStripMotion& getMotionForTest() noexcept { return motion_; }

protected:
    /** Opens the block's right-click menu. A protected virtual for the reason TimelineRulerComponent::
     *  openMarkerContextMenu is: a real PopupMenu needs a display a headless CI runner lacks, so tests override this
     *  and drive applySectionMenuChoice instead. `id` is captured into every item. */
    virtual void openSectionContextMenu(synth::PlacementId id);
    // The empty strip's menu (Make sections from markers); protected virtual for the same reason.
    virtual void openStripContextMenu();
    // Opens the variation menu for the block, anchored at its chip; protected virtual for the same reason.
    virtual void openVariationMenu(synth::PlacementId id);

private:
    class BlockProxy : public juce::Component {
    public:
        std::function<void()> showVariations; // the screen reader's "show menu" action
        std::function<bool()> isSelected;     // read live, so a selection change needs no rebuild
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    };
    class Probe;

    // ---- State the pointer gestures share (Mouse.cpp). The drag of a placed block (move / place again) starts from
    //      `pressedBlock_` and `pressPos_` and lives in the same file. ----
    juce::Point<float> pressPos_;     // local mouse position at the press
    synth::PlacementId pressedBlock_; // the block under it (invalid on empty strip)
    bool blockDragging_ = false;      // set by dragPressedBlock once the pointer has passed kDragStartPx
    bool pressedOnChip_ = false;      // the press landed on the variation chip; a release without a drag opens its menu
    bool hoveredChip_ = false;        // the pointer is over the hovered block's chip
    synth::PlacementId pendingCollapse_; // a plain press on one of several selected: just it on release, unless dragged
    void pressBlock(const juce::MouseEvent& e, synth::PlacementId id);
    // Called by mouseDrag / mouseUp while `pressedBlock_` is valid; they own the drag of a placed block.
    void dragPressedBlock(const juce::MouseEvent& e);
    void releasePressedBlock(const juce::MouseEvent& e);
    void cancelBlockDrag(); // Esc mid-drag: no doc change
    void beginBlockDrag(const juce::MouseEvent& e);
    void updateBlockDrag(const juce::MouseEvent& e);
    void updateBlockDragAt(juce::Point<float> pos, bool shift);
    void setDropPlan(sectionstrip::DropPlan plan);
    bool isDragMember(synth::PlacementId id) const;
    bool dropChangesAnything() const;
    void commitBlockDrop(synth::SectionDropMode mode);
    std::optional<Block> blockOf(synth::PlacementId id) const;
    juce::Rectangle<float> memberGhostRect(std::size_t index) const;
    float dragGhostAlpha() const;
    void clearDropPlan();
    std::vector<sectionstrip::PlacedSpan> placedSpans() const;
    double drawnStartOf(synth::PlacementId id, double docStart) const;
    void paintBlockDrag(juce::Graphics& g) const;
    void paintDropHint(juce::Graphics& g, float lift) const;
    juce::Rectangle<float> dropHintPillRect() const;
    bool dropHintShown() const noexcept { return blockDragging_ && !dragCancelled_ && !dragOutside_; }
    void updateDropHint();
    juce::String placeAgainAfterMenuText() const;
    // Abandons any gesture in flight (Esc): the make-drag outline and a block drag. True when one was.
    bool cancelGesture();

    // The ONE undo seam every edit goes through (one lambda = one undo step), null-manager safe.
    void performEdit(const std::function<void()>& mutation);
    double beatsPerBar() const;
    double secondsPerBeat() const;
    double beatAtX(double x) const noexcept { return viewState_.xToBeat(x); }
    double xAtBeat(double beat) const noexcept { return viewState_.beatToX(beat); }

    // Pointer gestures (TimelineSectionStripMouse.cpp)
    void beginMakeDrag(const juce::MouseEvent& e);
    void updateMakeDrag(const juce::MouseEvent& e);
    void updateMakeDragAt(juce::Point<float> pos);
    void endMakeDrag(bool commit);

    // Edge scroll (TimelineSectionStripEdgeScroll.cpp)
    void updateEdgeScrollArming();
    void edgeScrollTick();
    void stopEdgeScroll();
    struct EdgeScrollTimer : juce::Timer {
        std::function<void()> onTick;
        void timerCallback() override {
            if (onTick)
                onTick();
        }
    };
    EdgeScrollTimer edgeScroll_;
    juce::Point<float> lastDragPointer_;
    bool lastDragShift_ = false;
    std::vector<sectionstrip::Span> blockSpans() const;
    void setHoveredBlock(synth::PlacementId id, bool overChip = false);

    // Keyboard (TimelineSectionStripKeys.cpp)
    bool matchesKey(const juce::String& actionId, const juce::KeyPress& fallback, const juce::KeyPress& key) const;
    void selectNeighbour(int step);
    void extendSelection(int step);
    void selectEnd(bool last);
    bool moveSelectedBlock(int step);
    bool moveSelectedGroup(int step);

    // Motion (TimelineSectionStripMotion.cpp)
    void trackChangesForMotion();
    void paintBlock(juce::Graphics& g, const Block& block, juce::Rectangle<float> rect, float alpha,
                    const juce::Font& nameFont, const juce::Font& countFont, bool chipHot = false) const;

    // Menus (TimelineSectionStripMenu.cpp)
    void buildAndShowBlockMenu(synth::PlacementId id);
    std::unique_ptr<synth::ui::ColourPickerPopup> buildColourPicker(synth::PlacementId id);
    bool placeAgain(synth::PlacementId id, bool atEnd);
    // The set verbs (TimelineSectionStripSelection.cpp): one undo step each over every selected placement.
    bool actsOnSelection(synth::PlacementId id) const;
    bool deleteSelected(bool keepMusic);
    bool placeSelectedAgain();
    bool makeUnique(synth::PlacementId id);
    bool selectClipsOf(synth::PlacementId id);
    juce::PopupMenu::Options variationMenuOptions(synth::PlacementId id) const;

    // Rename (TimelineSectionStripRename.cpp)
    void finishRename(bool commit);
    void renameSection(synth::SectionId id, const juce::String& newName);

    // Accessibility (TimelineSectionStripAccessibility.cpp)
    void addTestProbe();
    void rebuildProxies();
    void layoutProxies();

    TimelineViewState& viewState_;
    synth::TransportService* transport_ = nullptr;
    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    juce::PropertiesFile* propertiesFile_ = nullptr;
    const ShortcutManager* shortcuts_ = nullptr;

    std::unique_ptr<ListSelectionModel<synth::PlacementId>> selection_;
    synth::PlacementId extendEnd_; // the far end a Shift+arrow run reached last
    synth::PlacementId hovered_;

    // Make by drag: armed by a press on empty strip, dragging once the pointer has moved kDragStartPx.
    bool makeArmed_ = false;
    bool makeDragging_ = false;
    double makeAnchorBeat_ = 0.0;
    sectionstrip::Span makeDragSpan_;

    // Block drag (move, or Option-drag to place again): `dragBlock_` is the pressed block as it was at drag start.
    Block dragBlock_;
    // Every block the drag carries, in time order (the pressed one among them), and where each ghost settles.
    struct DragMember {
        Block block;
        double offsetBeats = 0.0; // its start after the drop minus the pressed member's
        float settleX = 0.0f;
    };
    std::vector<DragMember> dragMembers_;
    double dragLeadBeats_ = 0.0; // the summed length of the members before the pressed one
    bool dragCopy_ = false;
    bool dragCancelled_ = false;
    bool dragOutside_ = false;        // the pointer is above the strip: a release there cancels
    sectionstrip::DropPlan dragPlan_; // what a release now would do; landing is where the drop line is drawn
    bool dragMakeRoom_ = true;        // the mode the plan was made in (the setting, flipped while Shift is held)
    bool hasDragPlan_ = false;
    juce::String dropHint_; // the words beside the drop line, and the ones it fades from when Shift flips the mode
    juce::String dropHintFrom_;
    std::vector<synth::PlacementId> settlingIds_; // the blocks just dropped: their settling ghosts stand in for them
    std::unique_ptr<KeyedPointTween>
        dropGlide_; // blocks easing to a new start: a drag's plan, an undo, a key move (x = beats)
    float dragGrabOffsetPx_ = 0.0f;
    float dragGhostX_ = 0.0f;  // where the ghost's left edge was at the last drag event
    float dragSettleX_ = 0.0f; // where the ghost settles to once released

    // What trackChangesForMotion compared last: the placements seen (id to start), the replace count, stamp revision.
    std::map<std::int64_t, double> knownPlacements_;
    std::int64_t lastReplaceCount_ = 0;
    std::int64_t lastStampRevision_ = 0;
    std::int64_t lastSectionEditRevision_ = 0;
    RestoreWatch restoreWatch_; // whether a replaced doc is an undo or redo (moves like an edit) or a load
    AppUndoManager* watchedUndo_ = nullptr;
    // Each block's picture at the last change: what a block that is gone at the next one shrinks away from.
    std::map<std::int64_t, SectionStripMotion::Ghost> lastGhosts_;
    bool motionPrimed_ = false;
    bool growFromRight_ = false; // set by a make-drag that started on the span's right half
    SectionStripMotion motion_{*this, [this] { repaint(); }};

    std::unique_ptr<juce::TextEditor> renameEditor_;
    synth::PlacementId renamingPlacement_;

    std::vector<std::unique_ptr<BlockProxy>> proxies_;
    // Title and description per proxy: what refresh() compares to decide whether to rebuild them.
    std::vector<std::pair<juce::String, juce::String>> proxyText_;
    std::unique_ptr<Probe> probe_;
    // Key of the geometry the proxies were last placed for, so paint() re-places them only after a scroll or zoom.
    juce::String proxyLayoutKey_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineSectionStrip)
};

} // namespace synth::ui
