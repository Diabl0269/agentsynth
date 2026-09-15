#pragma once

#include "MacroSet.h"
#include "Mixer/MixerModel/MixerModel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class AppUndoManager;
class GraphEditor;

// MixerSendList.h -- FRO15 (P9-9, docs/mixer.md §5.15): a column's send rows, sibling of
// MixerInsertList and laid out directly under it.
//
// One row per ACTIVE slot, in slot order: the target bus's name (click to retarget), a small rotary
// level knob, a PRE/POST toggle and an `x` remove; then a "+ Send" row while the strip has a free
// slot. Each mutation is ONE AppUndoManager::recordGraphAndMacroChange around synth::MixerSends'
// Core flows (which have no undo of their own), exactly as MixerInsertList wraps the insert
// splices.
//
// The level knob attaches straight onto the strip's own `sendNLevel` AudioParameterFloat, so send
// level is host-visible and automatable with no lane plumbing of its own. That attachment is a live
// pointer into a graph node's parameter, which makes unbindFromGraph() below load-bearing: without
// it, an undo that REPLACES the graph frees the parameter this list is still attached to.
namespace synth::ui {

class MixerSendList : public juce::Component {
public:
    MixerSendList();
    ~MixerSendList() override;

    /** Must be called once before setEntries() -- these outlive every entry set on this list. */
    void configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, synth::MacroSet& macros,
                   GraphEditor& graphEditor);

    void setEntries(const std::vector<synth::MixerSendEntry>& entries, juce::AudioProcessorGraph::NodeID stripNodeId);

    /** Fires after a mutation changed the graph -- the caller runs its own post-topology-change
     *  reconcile, same contract as MixerInsertList::onMutated. */
    std::function<void()> onMutated;

    /** "+ Send > New bus..." -- creates a bus channel (its own undo step) and returns its strip's
     *  node id, or an invalid id on failure. Supplied by MixerPanelComponent, which is the one that
     *  can size canvas cards. */
    std::function<juce::AudioProcessorGraph::NodeID()> createBus;

    int getPreferredHeight() const noexcept;

    /** FRO15: drops every SliderParameterAttachment and this list's own graph pointers -- called
     *  from MixerColumnComponent::unbindFromGraph(), i.e. BEFORE a graph-replacing mutation frees
     *  the parameters those attachments point at. Idempotent and null-safe. */
    void unbindFromGraph();

    // ---- Headless test seams. juce::PopupMenu never runs in a test process (docs/testing.md), so
    // the menu callbacks below call these same real methods. Row indices address the VISIBLE rows,
    // i.e. positions in `entries_`, not slot numbers.
    void addSendTo(juce::AudioProcessorGraph::NodeID target);
    void removeRow(int rowIndex);
    void togglePreFaderForRow(int rowIndex);
    void retargetRow(int rowIndex, juce::AudioProcessorGraph::NodeID target);
    std::vector<juce::AudioProcessorGraph::NodeID> availableTargets() const;
    bool canAddSend() const;
    bool isAttachedForTest(int rowIndex) const;
    juce::Slider* getKnobForTest(int rowIndex) const;
    int getRowCountForTest() const noexcept { return (int)entries_.size(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    static constexpr int kRowHeight = 20;
    static constexpr int kKnobWidth = 20;
    static constexpr int kToggleWidth = 30;
    static constexpr int kRemoveWidth = 14;

    struct Row {
        std::unique_ptr<juce::Slider> knob;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
    };

    int rowIndexAt(juce::Point<int> position) const;
    void rebuildKnobs();
    void showTargetMenu(int rowIndex);
    void showAddMenu();
    void mutateAndNotify(const std::function<bool()>& mutation);
    juce::String targetNameFor(juce::AudioProcessorGraph::NodeID target) const;

    juce::AudioProcessorGraph* graph_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::MacroSet* macros_ = nullptr;
    GraphEditor* graphEditor_ = nullptr;

    std::vector<synth::MixerSendEntry> entries_;
    std::vector<Row> rows_;
    juce::AudioProcessorGraph::NodeID stripNodeId_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerSendList)
};

} // namespace synth::ui
