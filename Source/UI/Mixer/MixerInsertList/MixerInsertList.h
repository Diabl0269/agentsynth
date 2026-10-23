#pragma once

#include "MacroSet.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "UI/Layout/ExitEnterList/ExitEnterListPlan.h"
#include "UI/Layout/ReorderDrag/ReorderDragSession.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Mixer/MixerHeader/MixerIconButton.h"
#include "UI/Timeline/TrackShowModuleButton.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

class AppUndoManager;
class GraphEditor;

namespace synth::ui {
class ModMatrixPicker;
}

// MixerInsertList.h (docs/mixer/mixer.md#inserts-in-a-free-form-graph): a column's module list.
// The Master column owns one too -- there the "source" is Master's own node and the "strip" end is the
// chain terminator (Rec Tap / Audio Output); nothing here is ChannelStrip-specific.
//
// Linear chain: a plain list, right-click for "Add after", "Move Up"/"Move Down"/"Remove" on one entry. A row also
// drags by its name (Layout/ReorderDrag, as the send list does) and moves from the keyboard (the panel's row keys);
// all three are the same moveRowTo(). Each mutation is ONE AppUndoManager::recordGraphAndMacroChange, via
// MixerModel's spliceOutInsert/spliceInInsert/reorderInsert (Core, pure graph splices).
//
// Adding (docs/mixer/mixer.md#adding-an-insert): a "+ Insert" row ends every editable list, and hovering the line
// between two rows that share a cable shows a "+" there. Either opens the effect picker (the shared searchable list,
// recent picks first); the pick is cabled in at that spot and its card placed right after its upstream neighbour's
// on the canvas, the cards after it making room, as one undo step.
//
// Each row ends in a bypass icon button (the module's own bypass, one undo step); a bypassed row's name is dimmed.
// With the row focused in the panel's row walk, the rebindable "mixerToggleRowBypass" key (B) does the same.
//
// Split chain (docs/mixer/mixer.md#parallel-paths): each split is one row, [fold arrow][split icon] "N paths"
// [bypass], folded by default; a click opens its paths in place, one level deeper. The open state is per split and
// outlives the list, so a column rebuilt after an edit keeps it. Rows can be bypassed, moved within their path and
// removed; the split row's bypass covers every module under it.
//
// Any other branch: the same list, read-only (no "+ Insert" row; the column header's "Edit on canvas" button is the
// way into the canvas for every chain, whatever its shape).
//
// Row indices in this API are the rows on screen: a folded split's paths have none.
namespace synth::ui {

class MixerInsertList
    : public juce::Component
    , public juce::TooltipClient {
public:
    MixerInsertList();
    ~MixerInsertList() override;

    /** Must be called once before setEntries() -- these outlive every entry set on this list. */
    void configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, synth::MacroSet& macros,
                   GraphEditor& graphEditor);

    /** `stripNodeId` is the node the chain feeds: the strip itself, or Master's chain terminator. */
    void setEntries(const std::vector<synth::MixerInsertEntry>& entries, bool linear,
                    juce::AudioProcessorGraph::NodeID sourceNodeId, juce::AudioProcessorGraph::NodeID stripNodeId);

    /** Fires when a split folds or opens (every frame while it animates): the column re-reads getPreferredHeight(). */
    std::function<void()> onPreferredHeightChanged;
    /** Fires after a mutation changed the graph -- the caller runs its own post-topology-change
     *  reconcile (MainComponent::reconcileTimelineAfterGraphChange). */
    std::function<void()> onMutated;
    /** Fires from removeRow(), with the node about to be removed, BEFORE
     *  graph_->removeNode() frees its processor. graph.removeNode() destroys the Node (and any
     *  processor it owns) synchronously, but the caller's own reaction to onMutated -- bubbling up
     *  to MixerPanelComponent::rebuild(), which destroys the OLD MixerColumnComponent (and its
     *  MixerEqThumbnail member) -- only runs afterwards, still inside this same user gesture. A UI
     *  object that holds a raw pointer into the node being removed right now (not the whole column
     *  MixerColumnComponent::unbindFromGraph() already covers for graph-replacing restores) must
     *  unbind from here instead, or its own later destruction dereferences freed memory. */
    std::function<void(juce::AudioProcessorGraph::NodeID)> onBeforeNodeRemoved;

    /** Fired by a drop or a keyboard move just before it commits, with the moved row's final index and where it was
     *  drawn (this list's coordinates). The commit normally rebuilds the mixer and destroys this list, so the panel
     *  keeps the settle and hands it to the NEW list's startSettleFrom(). */
    std::function<void(int finalRow, float fromY)> onSettlePending;
    /** A module row's "Show on canvas" button, or a double-click on the row: the module's uuid. The owner selects that
     *  module (not its channel) and brings its card into view. */
    std::function<void(const juce::String& moduleUuid)> onFocusModule;
    /** +1 before a move's undo step and -1 after it: the panel holds its own rebuild so onMutated() does the one. */
    std::function<void(int delta)> holdRebuild;
    /** Glides row `rowIndex` from `fromY` into its slot (the settle of a move this list did not see). */
    void startSettleFrom(int rowIndex, float fromY);

    int getPreferredHeight() const noexcept;

    // ---- Keyboard row focus (docs/mixer/panel.md#keyboard-navigation-and-accessibility) ----
    // Same contract as MixerSendList's: the panel owns which row is focused and draws its ring.
    /** The rows on screen, the "+ Insert" row included. */
    int getRowCount() const noexcept { return (int)visible_.size() + (hasAddRow() ? 1 : 0); }
    /** -1 clears; any other row is scrolled into the section's frame. */
    void setFocusedRow(int rowIndex);
    int getFocusedRow() const noexcept { return focusedRow_; }
    /** Row `rowIndex`'s bounds in this list's coordinates; empty when out of range. */
    juce::Rectangle<int> getRowBounds(int rowIndex) const;
    /** What a screen reader says for the row: "Insert 2, Compressor", "Insert 3, split into 2 paths, folded",
     *  "Insert 4, Delay 1, path 1". Empty when out of range. */
    juce::String describeRow(int rowIndex) const;
    /** The motion's view of the list: every entry's row key, and the rows on screen (the "+ Insert" row excluded) in
     *  this list's coordinates. See MixerRowsMotion. */
    std::vector<juce::String> entryKeys() const;
    std::vector<ExitEnterListRow> motionRows() const;
    /** The row's module uuid (what "select on canvas" resolves), or empty when out of range or a split row. */
    juce::String getRowUuid(int rowIndex) const;
    /** A linear or split chain can be edited row by row; any other branch is read-only. */
    bool isEditable() const noexcept { return linear_ || hasSplits_; }

    // ---- Adding (docs/mixer/mixer.md#adding-an-insert) ----
    /** Where an add lands: between `predecessor` and `successor` (an invalid successor means `predecessor`'s single
     *  signal successor). */
    struct AddTarget {
        juce::AudioProcessorGraph::NodeID predecessor;
        juce::AudioProcessorGraph::NodeID successor;
    };
    /** True when the list ends in a "+ Insert" row: an editable chain that can take an add at its end. */
    bool hasAddRow() const noexcept { return canAddAtEnd(); }
    /** The "+ Insert" row's index (the last row), or -1. */
    int getAddRowIndex() const noexcept { return hasAddRow() ? (int)visible_.size() : -1; }
    /** Opens the effect picker for `target`, pointing at `area` (in this list's coordinates). */
    void showAddPicker(AddTarget target, juce::Rectangle<int> area);
    /** Return on the "+ Insert" row: the picker for the end of the chain. */
    void activateAddRow();
    /** Adds module `moduleTypeName` at `target` (one undo step), placing its card on the canvas. */
    void addModuleAt(const juce::String& moduleTypeName, AddTarget target);
    /** The gap "+" target at `position`, if the pointer is on the line between two rows that share a cable. */
    std::optional<AddTarget> gapTargetAt(juce::Point<int> position) const;
    /** The "Add to path" target for path `path` (0-based) of split row `rowIndex`. */
    std::optional<AddTarget> pathAddTarget(int rowIndex, int path) const;
    /** The effect picker's rows: "Recent" first (this session's last picks), then the effect categories. */
    static std::vector<juce::String> effectPickerTypesForTest();
    /** When set, showAddPicker() hands its picker here instead of opening a call-out. */
    static std::function<void(std::unique_ptr<ModMatrixPicker>)>& pickerHookForTest();
    static void resetRecentPicksForTest();

    // ---- Splits (docs/mixer/mixer.md#parallel-paths) ----
    bool isSplitRow(int rowIndex) const noexcept;
    /** True for an open split row (its paths are on screen). */
    bool isSplitOpen(int rowIndex) const noexcept;
    /** Opens or folds a split row (animated when on screen); no-op on any other row. */
    void setSplitOpen(int rowIndex, bool open);
    /** True for a dry-path row (a path with no module); Return or a click on it offers an add into that path. */
    bool isDryRow(int rowIndex) const noexcept;
    void activateDryRow(int rowIndex);
    /** The split row a path row sits under, or -1 at the top level / out of range. */
    int getParentSplitRow(int rowIndex) const noexcept;
    /** Clears every remembered open split, so a test starts from the folded default. */
    static void resetSplitOpenStateForTest();

    /** The tooltip for whatever part of the list the pointer is over. */
    juce::String getTooltip() override { return tooltipAt(getMouseXYRelative()); }
    /** The tooltip at `position`: on a split row "Split" over its icon, else the split's paths, one line each. */
    juce::String tooltipAt(juce::Point<int> position) const;

    /** The module type names the effect picker offers -- exposed so a test can check every one resolves through
     *  AIStateMapper::createModule without a popup. */
    static const juce::StringArray& getAddableModuleTypes() noexcept;

    /** Test seams: what setEntries() last recorded, without a juce::Image round-trip --
     *  Tests/UI/Mixer/MixerColumnComponentTests.cpp's bus-column layout cases and
     *  MixerInsertListTests.cpp's overlap regression use these directly. */
    int getEntryCountForTest() const noexcept { return (int)entries_.size(); }
    /** True when the "Add..." menu has somewhere to add: the chain ends in a module or the source, not a split. */
    bool canAddAtEndForTest() const noexcept { return canAddAtEnd(); }
    bool isLinearForTest() const noexcept { return linear_; }

    // ---- Bypass (docs/mixer/panel.md#bypassing-a-row) ----
    /** Flips row `rowIndex`'s module bypass as ONE undo step, then fires onMutated. A split row bypasses every module
     *  under it (or turns them all back on when all are bypassed), still one undo step. Out of range or unconfigured:
     *  no-op. */
    void toggleBypassForRow(int rowIndex);
    /** Names the bypass key for the buttons' tooltips ("B"); null leaves the tooltips without a key. */
    std::function<juce::String()> bypassShortcutText;
    /** The row's bypass button -- the real child a click or a Space press reaches. Null out of range. */
    TrackShowModuleButton* getShowButtonForTest(int rowIndex) const noexcept {
        return rowIndex >= 0 && rowIndex < (int)showButtons_.size() ? showButtons_[(size_t)rowIndex].get() : nullptr;
    }
    MixerIconButton* getBypassButtonForTest(int rowIndex) const noexcept {
        return rowIndex >= 0 && rowIndex < (int)bypassButtons_.size() ? bypassButtons_[(size_t)rowIndex].get()
                                                                      : nullptr;
    }

    // ---- Drag-reorder test seams ----
    bool canMoveRowForTest(int rowIndex, int delta) const { return canMoveRow(rowIndex, delta); }
    bool isRowDragActiveForTest() const noexcept { return rowDrag_.isReordering(); }
    bool sendEscapeToRowDragForTest() { return rowDrag_.sendEscapeForTest(); }
    /** The module names top to bottom as the drag draws them right now (the order a release would commit). */
    std::vector<juce::String> getDragPreviewNamesForTest() const;
    /** The last startSettleFrom(): row -1 if none. */
    struct SettleForTest {
        int row = -1;
        float fromY = 0.0f;
    };
    SettleForTest getLastSettleForTest() const noexcept { return lastSettle_; }

    /** The transparent, name-only proxy component behind row `rowIndex` -- see
     *  RowAccessibilityProxy's own comment on why paint() above draws every row itself while
     *  accessibility still needs a real child Component per row. Null out of range. */
    juce::Component* getRowAccessibilityComponentForTest(int rowIndex) const noexcept {
        return rowIndex >= 0 && rowIndex < (int)rowProxies_.size() ? rowProxies_[(size_t)rowIndex].get() : nullptr;
    }

    // ---- Headless test seams -- juce::PopupMenu never runs in a test process (see
    // docs/development/test-patterns.md), so these are the same real methods the async menu
    // callbacks below call, exposed directly (the ChannelFlow suite's own `applyAddTrackMenuChoice`
    // precedent). Only valid on an editable chain with configure() already called. On a split chain a row moves
    // only within its own path, and a split row cannot be moved or removed.
    void moveRow(int rowIndex, int delta);
    /** Moves row `fromRow` to row `toRow`'s place (one undo step), both within the same run of modules: the whole
     *  chain, or one path of a split. False (nothing changed) when either row is outside the run. */
    bool moveRowTo(int fromRow, int toRow);
    /** Where a keyboard move of `rowIndex` by `delta` lands, or -1 when it cannot move that way. */
    int keyboardMoveTarget(int rowIndex, int delta) const;
    void removeRow(int rowIndex);
    void addModule(const juce::String& moduleTypeName);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

private:
    static constexpr int kRowHeight = 18;
    static constexpr int kBypassWidth = 18;
    static constexpr int kShowWidth = 16; // a module row's "Show on canvas" button, left of its bypass button
    static constexpr int kIndent = 12;    // per split level
    static constexpr int kArrowWidth = 12;
    static constexpr int kSplitIconWidth = 16;

    // paint() above draws every row itself (a plain custom-painted list, not real per-row
    // components), so VoiceOver/NVDA had nothing to land on for a row at all -- one of these, sized
    // and positioned over its row in resized(), gives each row a real, named, clickable AX child
    // without touching the existing paint()/rowIndexAt() hit-testing (mouseDown() still owns real
    // clicks: this stays setInterceptsMouseClicks(false, false), an accessibility-only overlay).
    class RowAccessibilityProxy : public juce::Component {
    public:
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
            return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::button);
        }
    };

    /** The width a row's buttons take at its right edge: bypass, and "Show on canvas" for a module. */
    int rowButtonsWidth(const synth::MixerInsertEntry& entry) const noexcept;
    int rowIndexAt(juce::Point<int> position) const;
    int rowTop(int rowIndex) const;
    int rowHeight(int rowIndex) const;
    int contentHeight() const;
    const synth::MixerInsertEntry* entryForRow(int rowIndex) const noexcept;
    /** Entry index one past `entryIndex`'s split subtree (the rows deeper than it that follow it). */
    int subtreeEnd(int entryIndex) const noexcept;
    bool isEntryOpen(int entryIndex) const;
    bool isSplitBypassed(int entryIndex) const;
    juce::String splitTooltip(int entryIndex) const;
    juce::Rectangle<int> splitIconBounds(int rowIndex) const;
    bool canMoveRow(int rowIndex, int delta) const;
    bool inSameRun(int row, int other) const;
    bool moveFeasible(int from, int to) const;
    /** The rows `rowIndex` can trade places with: its run of modules, as first/last row indices. A row outside any
     *  movable run (a split, a dry path, a one-row run, a bus's fixed first insert) gets {-1, -1}. */
    std::pair<int, int> moveRunOf(int rowIndex) const;
    bool isNameArea(juce::Point<int> position) const;
    void updateHoverCursor(juce::Point<int> position);
    float rowDrawTop(int rowIndex) const;
    juce::Rectangle<int> drawBounds(int rowIndex) const;
    int liftedRow() const;
    std::vector<ReorderDragAnimator::Slot> runSlots(int first, int last) const;
    void beginRowDrag(int rowIndex, const juce::MouseEvent& event);
    void commitRowDrag(int pressedRow);
    void paintRow(juce::Graphics& g, int rowIndex, float lift);
    void placeRows();
    bool canAddAtEnd() const noexcept;
    AddTarget endTarget() const noexcept;
    int listedRowsHeight() const;
    bool isAddRow(int rowIndex) const noexcept { return rowIndex >= 0 && rowIndex == getAddRowIndex(); }
    void rebuildVisibleRows();
    void refreshRows();
    void notifyHeightChanged();
    void showSplitMenu(int rowIndex);
    void showRowMenu(int rowIndex);
    bool mutateAndNotify(const std::function<bool()>& mutation, bool holdPanelRebuild = false);
    void rebuildRowAccessibilityProxies();
    void rebuildBypassButtons();
    juce::String bypassTooltip(int rowIndex) const;

    juce::AudioProcessorGraph* graph_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::MacroSet* macros_ = nullptr;
    GraphEditor* graphEditor_ = nullptr;

    std::vector<synth::MixerInsertEntry> entries_;
    /** The entries on screen, in order: every row except those under a folded split. */
    std::vector<int> visible_;
    bool linear_ = false;
    bool hasSplits_ = false;
    // A fold or open in flight: the split's entry index, and how much of its paths' height shows (0..1). Its rows
    // stay in visible_ while it folds, and leave when the fold lands.
    int animatingSplit_ = -1;
    float animatingAmount_ = 1.0f;
    bool animatingOpen_ = false;
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver foldAnimation_;
    int focusedRow_ = -1;
    // The row boundary the pointer is on with a "+" showing there, or -1.
    int hoverGap_ = -1;
    juce::AudioProcessorGraph::NodeID sourceNodeId_;
    juce::AudioProcessorGraph::NodeID stripNodeId_;
    // One per row on screen, rebuilt (and re-titled "<name>, bypassed"/"<name>") every
    // setEntries() and fold -- see RowAccessibilityProxy's own comment.
    std::vector<std::unique_ptr<RowAccessibilityProxy>> rowProxies_;
    // One per entries_ row, rebuilt with the proxies.
    std::vector<std::unique_ptr<MixerIconButton>> bypassButtons_;
    // Parallel to bypassButtons_; null on a split or dry-path row, which have no single module to show.
    std::vector<std::unique_ptr<TrackShowModuleButton>> showButtons_;

    // Drag-reorder gesture: pressedRow_ is armed on a name-area press and stays set for the whole press; the
    // animator's keys are run offsets (row - dragRunFirst_) and its slots the run's static places.
    int pressedRow_ = -1;
    int dragRunFirst_ = -1;
    int dragRunLast_ = -1;
    SettleForTest lastSettle_;
    ReorderDragSession rowDrag_{*this, [this] { placeRows(); }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerInsertList)
};

} // namespace synth::ui
