#pragma once

#include "MacroSet.h"
#include "Mixer/MixerModel/MixerModel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager;
class GraphEditor;

// MixerInsertList.h -- FRO11 (P9-5, docs/mixer/mixer.md#inserts-in-a-free-form-graph): a column's module list.
// FRO148: the Master column owns one too -- there the "source" is Master's own node and the "strip" end is the
// chain terminator (Rec Tap / Audio Output); nothing here is ChannelStrip-specific.
//
// Linear chain: a plain list, right-click for "Add...", "Move Up"/"Move Down"/"Remove" on one
// entry -- a menu-driven reorder rather than the plan's own drag-to-reorder idiom (scope trim: no
// single existing drag-reorder widget in this codebase was a clean fit within this ticket's
// budget, see the PR description's deviations). Each mutation is ONE
// AppUndoManager::recordGraphAndMacroChange, via MixerModel's spliceOutInsert/spliceInInsert/
// reorderInsert (Core, pure graph splices).
//
// Branching chain: the same list, read-only, plus a bottom "Edit on canvas" link that resolves and
// selects editOnCanvasTargetUuid exactly like a column header click does.
namespace synth::ui {

class MixerInsertList : public juce::Component {
public:
    MixerInsertList();

    /** Must be called once before setEntries() -- these outlive every entry set on this list. */
    void configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, synth::MacroSet& macros,
                   GraphEditor& graphEditor);

    /** `stripNodeId` is the node the chain feeds: the strip itself, or (FRO148) Master's chain terminator. */
    void setEntries(const std::vector<synth::MixerInsertEntry>& entries, bool linear,
                    const juce::String& editOnCanvasTargetUuid, juce::AudioProcessorGraph::NodeID sourceNodeId,
                    juce::AudioProcessorGraph::NodeID stripNodeId);

    /** Fires when "Edit on canvas" is clicked, with editOnCanvasTargetUuid. */
    std::function<void(const juce::String&)> onEditOnCanvas;
    /** Fires after a mutation changed the graph -- the caller runs its own post-topology-change
     *  reconcile (MainComponent::reconcileTimelineAfterGraphChange). */
    std::function<void()> onMutated;
    /** FRO16 UAF fix: fires from removeRow(), with the node about to be removed, BEFORE
     *  graph_->removeNode() frees its processor. graph.removeNode() destroys the Node (and any
     *  processor it owns) synchronously, but the caller's own reaction to onMutated -- bubbling up
     *  to MixerPanelComponent::rebuild(), which destroys the OLD MixerColumnComponent (and its
     *  MixerEqThumbnail member) -- only runs afterwards, still inside this same user gesture. A UI
     *  object that holds a raw pointer into the node being removed right now (not the whole column
     *  MixerColumnComponent::unbindFromGraph() already covers for graph-replacing restores) must
     *  unbind from here instead, or its own later destruction dereferences freed memory. */
    std::function<void(juce::AudioProcessorGraph::NodeID)> onBeforeNodeRemoved;

    int getPreferredHeight() const noexcept;

    /** The module type names the "Add..." menu offers (FRO148: includes Limiter and Gate) -- exposed so a test can
     *  check every one resolves through AIStateMapper::createModule without a popup menu. */
    static const juce::StringArray& getAddableModuleTypes() noexcept;

    /** FRO15 test seams: what setEntries() last recorded, without a juce::Image round-trip --
     *  Tests/UI/Mixer/MixerColumnComponentTests.cpp's bus-column layout cases and
     *  MixerInsertListTests.cpp's overlap regression use these directly. */
    int getEntryCountForTest() const noexcept { return (int)entries_.size(); }
    bool isLinearForTest() const noexcept { return linear_; }

    /** FRO228: the transparent, name-only proxy component behind row `rowIndex` -- see
     *  RowAccessibilityProxy's own comment on why paint() above draws every row itself while
     *  accessibility still needs a real child Component per row. Null out of range. */
    juce::Component* getRowAccessibilityComponentForTest(int rowIndex) const noexcept {
        return rowIndex >= 0 && rowIndex < (int)rowProxies_.size() ? rowProxies_[(size_t)rowIndex].get() : nullptr;
    }

    // ---- Headless test seams -- juce::PopupMenu never runs in a test process (see
    // docs/development/test-patterns.md), so these are the same real methods the async menu
    // callbacks below call, exposed directly (the ChannelFlow suite's own `applyAddTrackMenuChoice`
    // precedent). Only valid on a linear chain (linear_) with configure() already called.
    void moveRow(int rowIndex, int delta);
    void removeRow(int rowIndex);
    void addModule(const juce::String& moduleTypeName);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    static constexpr int kRowHeight = 18;

    // FRO228: paint() above draws every row itself (a plain custom-painted list, not real per-row
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

    int rowIndexAt(juce::Point<int> position) const;
    void showRowMenu(int rowIndex);
    void showAddMenu();
    void mutateAndNotify(const std::function<bool()>& mutation);
    void rebuildRowAccessibilityProxies();

    juce::AudioProcessorGraph* graph_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::MacroSet* macros_ = nullptr;
    GraphEditor* graphEditor_ = nullptr;

    std::vector<synth::MixerInsertEntry> entries_;
    bool linear_ = false;
    juce::String editOnCanvasTargetUuid_;
    juce::AudioProcessorGraph::NodeID sourceNodeId_;
    juce::AudioProcessorGraph::NodeID stripNodeId_;
    // FRO228: one per entries_ row, rebuilt (and re-titled "<name>, bypassed"/"<name>") every
    // setEntries() -- see RowAccessibilityProxy's own comment.
    std::vector<std::unique_ptr<RowAccessibilityProxy>> rowProxies_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerInsertList)
};

} // namespace synth::ui
