#pragma once

#include "Modules/FX/ParametricEQModule.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// MixerEqThumbnail.h -- FRO16 (P9-10, docs/mixer.md §5.10): a small frequency-response curve on a
// mixer column, Cubase's top-mixer-row idiom. Shown only when the column's first (signal-order)
// insert is a Parametric EQ (MixerColumnComponent::rebindControls() decides that); hidden
// otherwise.
//
// Lifetime: setEqModule(nullptr) must run BEFORE the bound module's owning node is removed from
// the graph, not only before a full graph-replacing restore. MixerColumnComponent calls it from
// two seams: unbindFromGraph() (the FRO11 pre-restore hook, for undo/redo/New Patch/Load/AI
// apply) AND MixerInsertList::onBeforeNodeRemoved (fired from MixerInsertList::removeRow, for a
// live single-insert removal via the mixer's own row menu -- graph.removeNode() frees the
// processor synchronously, so without this second seam the column's later rebuild
// (MixerPanelComponent::rebuild(), reached through onMutated) would destroy this thumbnail's
// still-bound eq_ pointer AFTER the module was already freed, and ~MixerEqThumbnail's
// detachListeners() would dereference it).
//
// Belt-and-braces: the row menu is not the only way to free a single node out from under a bound
// thumbnail (a canvas "Delete" on the same EQ module's card goes through
// GraphEditor::requestDeleteModule(), a different removeNode() call site with no equivalent
// pre-removal hook). setEqModule()'s optional `graph`/`nodeId` let detachListeners() check the
// node is still actually in the graph before touching `eq_`'s parameters at all -- if it's
// already gone, its parameters died with it and there is nothing left to call removeListener()
// on. A null `graph` (the default -- e.g. a test binding a stack-allocated module never added to
// any graph) means no liveness info is available, so detachListeners() assumes live and detaches
// unconditionally, same as before this existed.
//
// Repaint discipline (root CLAUDE.md "No unconditional per-tick repaint"): no Timer, no
// AnimationDriver. A parameter write on ANY thread (the audio thread included -- CV-modulated
// bands write their resolved value there) can only call the allocation-free, coalescing
// AsyncUpdater; the actual recompute + repaint happens once, on the message thread, in
// handleAsyncUpdate() -- the same thread-hop HostedPluginModule::InstanceListener uses for its own
// audioProcessorChanged callback. paint() reads only the cached result, never the module.
namespace synth::ui {

class MixerEqThumbnail
    : public juce::Component
    , private juce::AudioProcessorParameter::Listener
    , private juce::AsyncUpdater {
public:
    MixerEqThumbnail();
    ~MixerEqThumbnail() override;

    /** nullptr hides the thumbnail and detaches from whatever EQ was bound. The same pointer
     *  twice is a no-op. A different EQ detaches the old one, binds the new one, and recomputes
     *  synchronously so the very first paint already has data (no one-frame flash of stale/empty
     *  curve). Registers as a juce::AudioProcessorParameter::Listener on every one of `eq`'s
     *  parameters (band on/freq/gain/Q x4, output gain, and the inherited "bypassed" -- all of
     *  ModuleBase::getParameters()), so a change from ANYWHERE (the module card's own knobs, an
     *  undo/redo, CV automation) keeps the thumbnail in sync, not just edits made through this
     *  column. `graph`/`nodeId` are optional liveness info (see the header comment's
     *  "Belt-and-braces" paragraph) -- pass the graph `eq` lives in and its own NodeID whenever
     *  the caller has them (MixerColumnComponent always does); omit only when there genuinely is
     *  none (a headless test binding a module with no graph at all). */
    void setEqModule(ParametricEQModule* eq, juce::AudioProcessorGraph* graph = nullptr,
                     juce::AudioProcessorGraph::NodeID nodeId = {});

    /** Fires on mouseUp -- MixerColumnComponent forwards this through its existing onEditOnCanvas
     *  seam with the EQ node's own uuid, exactly like a column header click or the insert list's
     *  own "Edit on canvas" link. */
    std::function<void()> onClicked;

    /** Number of times recompute() has actually run -- proves the "recompute only on parameter
     *  change" discipline: stays flat across repeated paints, bumps by exactly one per coalesced
     *  parameter-change burst. */
    int getRecomputeCountForTest() const noexcept { return recomputeCount_; }

    /** Counts only detachListeners() calls that actually removed a live listener registration --
     *  neither a defensive no-op with nothing bound (eq_ already null) NOR the belt-and-braces
     *  no-op when the bound node is already gone from the graph (its parameters died with it) --
     *  same accounting as MixerFader::getLiveUnbindCallCountForTest(). Lets a test prove a
     *  pre-removal unbind hook actually ran BEFORE the node was freed, not just that nothing
     *  crashed. */
    static int getLiveUnbindCallCountForTest() noexcept { return liveUnbindCallCountForTest_; }

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;

    void detachListeners();
    void recompute();

    ParametricEQModule* eq_ = nullptr;
    juce::AudioProcessorGraph* graph_ = nullptr;
    juce::AudioProcessorGraph::NodeID nodeId_;

    std::vector<float> cachedMagnitudesDb_;
    bool cachedBypassed_ = false;
    int recomputeCount_ = 0;

    static int liveUnbindCallCountForTest_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerEqThumbnail)
};

} // namespace synth::ui
