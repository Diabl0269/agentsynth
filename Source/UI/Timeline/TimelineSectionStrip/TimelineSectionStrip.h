#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Timeline/TimelineSectionStrip/SectionStripGeometry.h"
#include "UI/Timeline/TimelineViewState.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
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
// Split by concern: TimelineSectionStrip.cpp (state, painting, blocks), ...Mouse.cpp (make by drag and the other
// pointer gestures), ...Menu.cpp (block and strip menus and their headless seams), ...Rename.cpp (the in-place name
// editor), ...Accessibility.cpp (one proxy per block, tooltips, the test-mode probe), ...Keys.cpp (keyboard).
namespace synth::ui {

class TimelineSectionStrip
    : public juce::Component
    , public juce::TooltipClient {
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
        int ordinal = 1; // 1-based position among the section's placements
        int count = 1;   // how many times the section plays
        juce::Rectangle<float> bounds;
    };
    // Every placement whose block intersects the visible strip, in doc order.
    std::vector<Block> buildBlocks() const;
    synth::PlacementId blockAt(juce::Point<float> pos) const;

    synth::PlacementId getSelectedPlacement() const noexcept { return selected_; }
    void selectPlacement(synth::PlacementId id);

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
    bool keyPressed(const juce::KeyPress& key) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    // ---- Tooltips and accessibility ----
    // What the tooltip says at `pos`: the block's, else the empty-strip one. Never empty.
    juce::String tooltipAt(juce::Point<float> pos) const;
    juce::String getTooltip() override;
    int getProxyCountForTest() const noexcept { return (int)proxies_.size(); }
    juce::String getProxyTitleForTest(int index) const;
    juce::String getProxyDescriptionForTest(int index) const;

    // ---- Test seams for the gestures ----
    bool isMakeDraggingForTest() const noexcept { return makeDragging_; }
    // The span a make-drag in flight would commit, when one is in flight.
    sectionstrip::Span getMakeDragSpanForTest() const noexcept { return makeDragSpan_; }

protected:
    /** Opens the block's right-click menu. A protected virtual for the reason TimelineRulerComponent::
     *  openMarkerContextMenu is: a real PopupMenu needs a display a headless CI runner lacks, so tests override this
     *  and drive applySectionMenuChoice instead. `id` is captured into every item. */
    virtual void openSectionContextMenu(synth::PlacementId id);
    // The empty strip's menu (Make sections from markers); protected virtual for the same reason.
    virtual void openStripContextMenu();

private:
    class BlockProxy : public juce::Component {
    public:
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    };
    class Probe;

    // ---- State the pointer gestures share (Mouse.cpp). The drag of a placed block (move / place again) starts from
    //      `pressedBlock_` and `pressPos_` and lives in the same file. ----
    juce::Point<float> pressPos_;     // local mouse position at the press
    synth::PlacementId pressedBlock_; // the block under it (invalid on empty strip)
    bool blockDragging_ = false;      // set by dragPressedBlock once the pointer has passed kDragStartPx
    // Called by mouseDrag / mouseUp while `pressedBlock_` is valid; they own the drag of a placed block.
    void dragPressedBlock(const juce::MouseEvent& e);
    void releasePressedBlock(const juce::MouseEvent& e);
    void cancelBlockDrag(); // Esc mid-drag: no doc change
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
    void endMakeDrag(bool commit);
    std::vector<sectionstrip::Span> blockSpans() const;
    void setHoveredBlock(synth::PlacementId id);

    // Menus (TimelineSectionStripMenu.cpp)
    void buildAndShowBlockMenu(synth::PlacementId id);
    std::unique_ptr<synth::ui::ColourPickerPopup> buildColourPicker(synth::PlacementId id);
    bool placeAgain(synth::PlacementId id, bool atEnd);
    bool makeUnique(synth::PlacementId id);
    bool selectClipsOf(synth::PlacementId id);

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

    synth::PlacementId selected_;
    synth::PlacementId hovered_;

    // Make by drag: armed by a press on empty strip, dragging once the pointer has moved kDragStartPx.
    bool makeArmed_ = false;
    bool makeDragging_ = false;
    double makeAnchorBeat_ = 0.0;
    sectionstrip::Span makeDragSpan_;

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
