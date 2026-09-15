// MixerPanelUndoUnbindTests.cpp -- FRO11 crash fix regression: a graph-structural undo/redo (or
// New Patch) used to destroy the OLD MixerColumnComponent set (MixerPanelComponent::rebuild(),
// reached from the AFTER-restore hook / reconcileTimelineAfterGraphChange()) AFTER the graph
// mutation had already freed the ChannelStripModule/MasterModule nodes the mixer's MixerFader
// (and pan SliderParameterAttachment) still pointed at -- ~MixerFader -> unbind() ->
// AudioProcessorParameter::removeListener() on freed memory, which hung the Linux CI build
// (Tests/Mixer/ChannelFlow/ChannelFlowCreateChannelsTests.cpp's
// CreateChannelsIsANoOpWhenNothingNeedsAChannel: exit 124 + SIGABRT, deadlocked inside
// CriticalSection::enter on freed memory).
//
// The fix: GraphEditor::onBeforeDetachAllModuleComponents, fired at the top of
// GraphEditor::detachAllModuleComponents() -- the ONE seam every graph-replacing path already
// funnels through (undo/redo's lazy preRestore, New Patch, Load, AI patch apply, teardown) --
// wired to MixerPanelComponent::unbindAllColumns(), so the mixer's own bindings are torn down
// BEFORE the mutation frees anything, exactly like ModuleComponent::detachFromProcessor() already
// is via the same call.
//
// MixerFader::getLiveUnbindCallCountForTest() only counts an unbind() that actually detached a
// live parameter (not a defensive no-op on an already-unbound fader), which is what lets these
// tests prove the pre-restore hook actually ran and did real work -- not just that nothing crashed.
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerFader.h"
#include <gtest/gtest.h>

namespace {

// Same minimal mock as every other headless MainComponent test in this suite
// (ChannelFlowTestFixture.h's MockProviderCFT / MixerPanelComponentTests.cpp's MockProviderMPCT)
// -- a unique name to avoid an ODR clash across test translation units.
class MockProviderMPUT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMPUT"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

int countChannelStrips(juce::AudioProcessorGraph& graph) {
    int count = 0;
    for (auto* node : graph.getNodes())
        if (node != nullptr && dynamic_cast<ChannelStripModule*>(node->getProcessor()) != nullptr)
            ++count;
    return count;
}

} // namespace

TEST(MixerPanelUndoUnbindTests, MixerPanelUnbindsBeforeAGraphRestoreSoUndoNeverTouchesFreedParameters) {
    MainComponent mc(std::make_unique<MockProviderMPUT>());
    mc.setSize(1400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();

    // simulateAddAudioTrackClick() creates a Track Audio -> insert chain -> ChannelStrip macro as
    // ONE undo step (T173a), and reconcileTimelineAfterGraphChange() (the same funnel every graph
    // change reaches) rebuilds the mixer right away, binding a real MixerFader to the new strip's
    // live "gain" AudioParameterFloat.
    mc.simulateAddAudioTrackClick();
    auto& graph = mc.getAudioEngine().getGraph();
    ASSERT_EQ(countChannelStrips(graph), 1);

    auto& mixerPanel = mc.getMixerDock().getMixerPanel();
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    ASSERT_TRUE(column->isFaderBoundForTest()) << "the strip's fader must be bound before undo";

    ASSERT_TRUE(mc.getUndoManager().canUndo()) << "simulateAddAudioTrackClick itself pushed one undo step";
    const int liveUnbindsBefore = synth::ui::MixerFader::getLiveUnbindCallCountForTest();

    // This is the exact repro: undoing the channel creation frees the ChannelStripModule (and its
    // "gain" parameter) the mixer's column is still bound to. Before the fix, the OLD column
    // (still holding that now-freed pointer) was destroyed by mixerDock.rebuildMixer() AFTER the
    // restore had already freed it -- a crash/deadlock, not an assertion failure, is what a
    // regression here looks like (the process never reaches the EXPECTs below).
    ASSERT_TRUE(mc.getUndoManager().undo());

    EXPECT_GT(synth::ui::MixerFader::getLiveUnbindCallCountForTest(), liveUnbindsBefore)
        << "the pre-restore hook (MixerPanelComponent::unbindAllColumns(), reached via "
           "GraphEditor::onBeforeDetachAllModuleComponents) must have unbound the strip's fader "
           "from its live parameter as part of the restore";

    // And the restore genuinely completed: the channel is gone, and the mixer's own rebuild (the
    // after-restore hook) has already caught up to a graph with no strips left.
    EXPECT_EQ(countChannelStrips(graph), 0);
    EXPECT_EQ(mixerPanel.getStripColumnForTest(0), nullptr);
}

TEST(MixerPanelUndoUnbindTests, MixerPanelUnbindsBeforeNewPatchReplacesTheDocument) {
    MainComponent mc(std::make_unique<MockProviderMPUT>());
    mc.setSize(1400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getMixerDock().getMixerPanel();
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    ASSERT_TRUE(column->isFaderBoundForTest());

    const int liveUnbindsBefore = synth::ui::MixerFader::getLiveUnbindCallCountForTest();

    // New Patch is the document-replace seam (GraphEditor::newPatch()'s own doClear() calls
    // detachAllModuleComponents() directly, then graph.clear() -- the same seam, a different
    // caller than undo/redo's preRestore lambda). No crash is the primary assertion; the counter
    // proves the hook actually fired rather than the test getting lucky.
    mc.newPatchForTest();

    EXPECT_GT(synth::ui::MixerFader::getLiveUnbindCallCountForTest(), liveUnbindsBefore)
        << "New Patch must unbind the old strip's fader before clearing the graph";
    EXPECT_EQ(countChannelStrips(mc.getAudioEngine().getGraph()), 0);
    EXPECT_EQ(mixerPanel.getStripColumnForTest(0), nullptr);
}

// FRO16 review follow-up: GraphEditor::deleteSelection() (a canvas "Delete", or
// deleteMacroAndMembers -- the exact repro in MixerPanelKeyboardFocusTests.cpp's
// DeletingTheFocusedStripClearsFocus) is a THIRD graph-freeing path, distinct from both a
// graph-replacing restore above and MixerInsertList::removeRow()'s own single-row hook. It has no
// rebuild of its own to lean on (reconcileTimelineBindingsOnly(), its only guaranteed post-apply
// site, deliberately never rebuilds the mixer), so a stale fader binding could sit until an
// UNRELATED later graph edit finally destroyed the old column and dereferenced it --
// exit 124 + SIGABRT, deadlocked inside CriticalSection::enter on freed memory, same signature as
// the two crashes this file already regression-tests. Fixed by also firing
// onBeforeDetachAllModuleComponents from deleteSelection(), before it frees the selected nodes.
TEST(MixerPanelUndoUnbindTests, MixerPanelUnbindsBeforeDeleteSelectionFreesTheStripsNodes) {
    MainComponent mc(std::make_unique<MockProviderMPUT>());
    mc.setSize(1400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getMixerDock().getMixerPanel();
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    ASSERT_TRUE(column->isFaderBoundForTest());

    auto& macros = mc.getGraphEditor().getMacros();
    ASSERT_EQ(macros.size(), 1u) << "simulateAddAudioTrackClick boxes the strip's chain into one macro";
    const auto macroId = macros.getAll()[0].id;

    const int liveUnbindsBefore = synth::ui::MixerFader::getLiveUnbindCallCountForTest();

    // deleteMacroAndMembers -> GraphCanvasHost::deleteSelection() frees every member node
    // (including the ChannelStrip the fader is bound to) synchronously. No crash/hang is the
    // primary assertion here; the counter proves the fix's own pre-removal hook actually ran
    // rather than the test getting lucky on this run's heap layout.
    mc.getGraphEditor().deleteMacroAndMembers(macroId);

    EXPECT_GT(synth::ui::MixerFader::getLiveUnbindCallCountForTest(), liveUnbindsBefore)
        << "deleteSelection() must unbind the strip's fader before removeNode() frees it";

    // The eventual mixer rebuild (whenever it next runs, exactly like production) must not
    // dereference anything freed above.
    mixerPanel.rebuild();
    EXPECT_EQ(countChannelStrips(mc.getAudioEngine().getGraph()), 0);
    EXPECT_EQ(mixerPanel.getStripColumnForTest(0), nullptr);
}
