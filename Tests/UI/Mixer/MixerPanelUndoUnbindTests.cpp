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
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
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

// The mixer column binds to the ChannelStripModule's own gain/pan/sendNLevel parameters, NOT to
// the track-audio node the timeline track's bindingUuid points at -- resolving the node any other
// way would hand these tests a node the mixer never bound, and the live-unbind counter would sit
// still whether the fix is present or not.
juce::AudioProcessorGraph::Node* findChannelStripNode(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && dynamic_cast<ChannelStripModule*>(node->getProcessor()) != nullptr)
            return node;
    return nullptr;
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

    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    ASSERT_TRUE(column->isFaderBoundForTest()) << "the strip's fader must be bound before undo";

    ASSERT_TRUE(mc.getUndoManager().canUndo()) << "simulateAddAudioTrackClick itself pushed one undo step";
    const int liveUnbindsBefore = synth::ui::MixerFader::getLiveUnbindCallCountForTest();

    // This is the exact repro: undoing the channel creation frees the ChannelStripModule (and its
    // "gain" parameter) the mixer's column is still bound to. Before the fix, the OLD column
    // (still holding that now-freed pointer) was destroyed by bottomDock.rebuildMixer() AFTER the
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

    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
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

    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
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
    mc.getGraphEditor().getMacroController().deleteMacroAndMembers(macroId);

    EXPECT_GT(synth::ui::MixerFader::getLiveUnbindCallCountForTest(), liveUnbindsBefore)
        << "deleteSelection() must unbind the strip's fader before removeNode() frees it";

    // The eventual mixer rebuild (whenever it next runs, exactly like production) must not
    // dereference anything freed above.
    mixerPanel.rebuild();
    EXPECT_EQ(countChannelStrips(mc.getAudioEngine().getGraph()), 0);
    EXPECT_EQ(mixerPanel.getStripColumnForTest(0), nullptr);
}

// FRO16's review follow-up closed GraphEditor::deleteSelection() (above), but left the two OTHER
// single-node removal commands open: requestDeleteModule() -- a module card's own delete button
// (ModuleComponent.cpp) and its "Delete Module" context-menu item -- and replaceModule(), the
// "Replace with..." submenu, offered for every module except the singleton Audio Input/Output.
// Both can free a ChannelStripModule (or MasterModule, which is deliberately kept OUT of any
// collapsed macro and so is always individually addressable on the canvas) that a mixer column's
// fader, pan attachment or send rows are still bound to, and neither has a rebuild of its own to
// lean on: updateComponents() only reaches reconcileTimelineBindingsOnly(), which deliberately
// never rebuilds the mixer. The dangling binding then sat until an unrelated later graph edit
// destroyed the old column and dereferenced it -- the exact heap-use-after-free signature (or,
// non-deterministically, the exit-124 deadlock inside CriticalSection::enter) this file's other
// three tests exist for.
TEST(MixerPanelUndoUnbindTests, MixerPanelUnbindsBeforeRequestDeleteModuleFreesTheStripsNode) {
    MainComponent mc(std::make_unique<MockProviderMPUT>());
    mc.setSize(1400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& graph = mc.getAudioEngine().getGraph();
    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    ASSERT_TRUE(column->isFaderBoundForTest()) << "the strip's fader must be bound before the delete";

    auto* stripNode = findChannelStripNode(graph);
    ASSERT_NE(stripNode, nullptr);

    const int liveUnbindsBefore = synth::ui::MixerFader::getLiveUnbindCallCountForTest();

    // The card's delete button and "Delete Module" both land here, with the strip's own NodeID.
    mc.getGraphEditor().requestDeleteModule(stripNode->nodeID);

    EXPECT_GT(synth::ui::MixerFader::getLiveUnbindCallCountForTest(), liveUnbindsBefore)
        << "requestDeleteModule() must unbind the strip's fader before removeNode() frees it";

    // The eventual mixer rebuild -- whenever it next runs, exactly like production -- must not
    // dereference anything freed above.
    mixerPanel.rebuild();
    EXPECT_EQ(countChannelStrips(graph), 0);
    EXPECT_EQ(mixerPanel.getStripColumnForTest(0), nullptr);
}

TEST(MixerPanelUndoUnbindTests, MixerPanelUnbindsBeforeReplaceModuleFreesTheStripsNode) {
    MainComponent mc(std::make_unique<MockProviderMPUT>());
    mc.setSize(1400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& graph = mc.getAudioEngine().getGraph();
    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    ASSERT_TRUE(column->isFaderBoundForTest()) << "the strip's fader must be bound before the replace";

    auto* stripNode = findChannelStripNode(graph);
    ASSERT_NE(stripNode, nullptr);

    // replaceModule() resolves the old node by processor pointer, so it wants the card, not the id.
    ModuleComponent* stripCard = nullptr;
    for (auto* card : mc.getGraphEditor().getModuleComponents())
        if (card != nullptr && card->getModule() == stripNode->getProcessor())
            stripCard = card;
    ASSERT_NE(stripCard, nullptr) << "the strip has a canvas card, which is what carries the menu";

    const int liveUnbindsBefore = synth::ui::MixerFader::getLiveUnbindCallCountForTest();

    // "Replace with... Oscillator" frees the ChannelStripModule the fader points at.
    mc.getGraphEditor().replaceModule(stripCard, "Oscillator");

    EXPECT_GT(synth::ui::MixerFader::getLiveUnbindCallCountForTest(), liveUnbindsBefore)
        << "replaceModule() must unbind the strip's fader before removeNode() frees it";

    mixerPanel.rebuild();
    EXPECT_EQ(countChannelStrips(graph), 0);
}

// The other half of FRO103. The pre-removal unbind above is what keeps these paths from
// dereferencing freed parameters, but on its own it also leaves every column attached to nothing:
// unbindAllColumns() unbinds the WHOLE mixer, including columns whose own nodes were never
// touched, and nothing on these paths rebuilds the panel (updateComponents() only reaches
// reconcileTimelineBindingsOnly(), which deliberately never does). Without the matching
// onAfterGraphNodesRemoved hook, deleting any module from the canvas left every fader on screen
// but inert until an unrelated later change happened to rebuild -- a visible dead mixer, traded
// for a fixed crash. This test deletes a module the mixer never bound (an EQ insert) and holds the
// surviving strip's fader to being live again afterwards, with no manual rebuild() anywhere.
TEST(MixerPanelUndoUnbindTests, MixerColumnsAreReboundAfterAnUnrelatedModuleIsDeleted) {
    MainComponent mc(std::make_unique<MockProviderMPUT>());
    mc.setSize(1400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& graph = mc.getAudioEngine().getGraph();
    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    ASSERT_NE(mixerPanel.getStripColumnForTest(0), nullptr);
    ASSERT_TRUE(mixerPanel.getStripColumnForTest(0)->isFaderBoundForTest());

    juce::AudioProcessorGraph::Node* victim = nullptr;
    for (auto* n : graph.getNodes())
        if (n != nullptr && n->getProcessor() != nullptr && n->getProcessor()->getName().containsIgnoreCase("EQ"))
            victim = n;
    ASSERT_NE(victim, nullptr) << "the default audio channel has an EQ insert to delete";

    mc.getGraphEditor().requestDeleteModule(victim->nodeID);

    // The strip itself is untouched, so its column must still be there AND still drive its gain
    // parameter -- no manual rebuild, exactly like production after a canvas delete.
    ASSERT_EQ(countChannelStrips(graph), 1) << "only the EQ insert was deleted";
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    ASSERT_TRUE(column->isFaderBoundForTest())
        << "the pre-removal unbind left every column detached; the panel must have rebuilt so the "
           "surviving strip's fader is attached to its gain parameter again";

    // ...and prove the binding is live rather than merely present: drive the fader the way a user
    // does (the panel's own Up-arrow nudge, MixerPanelKeyboardFocusTests' path) and hold the
    // strip's real "gain" parameter to having moved. A rebuilt-but-dead column would pass the
    // flag check above and fail here.
    auto* stripNode = findChannelStripNode(graph);
    ASSERT_NE(stripNode, nullptr);
    float gainBefore = 0.0f;
    for (auto* param : stripNode->getProcessor()->getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(param); f != nullptr && f->paramID == "gain")
            gainBefore = f->get();

    // Right once moves focus from "nothing focused" onto the first column (the same step
    // MixerPanelKeyboardFocusTests' own navigation tests take); there is no public setter.
    ASSERT_TRUE(mixerPanel.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0)));
    ASSERT_EQ(mixerPanel.getFocusedColumnIndexForTest(), 0);
    ASSERT_TRUE(mixerPanel.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::noModifiers, 0)))
        << "the panel must claim Up as a fader nudge on the focused column";

    float gainAfter = gainBefore;
    for (auto* param : stripNode->getProcessor()->getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(param); f != nullptr && f->paramID == "gain")
            gainAfter = f->get();
    EXPECT_GT(gainAfter, gainBefore)
        << "the rebuilt column's fader must actually move the strip's gain parameter -- this is the "
           "user-visible half of the fix, not just that a pointer is non-null";
}
