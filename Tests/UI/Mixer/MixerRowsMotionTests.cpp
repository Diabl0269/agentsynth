// MixerRowsMotionTests.cpp (docs/layout/animation.md "Delete and undo animation"): an insert or send row that is
// added or removed opens or closes where it was instead of jumping (MixerRowsMotion). Drives a real off-screen
// MainComponent through the lists' own mutations (+ Send, remove, the effect picker, undo), which rebuild the column,
// and steps each list's ExitEnterListMotion by hand.
#include "AI/AIProvider.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <gtest/gtest.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using synth::ui::ExitEnterTimeline;
using synth::ui::MixerRowsMotion;

class MockProviderMRMT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMRMT"; }
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

struct ReducedMotionGuard {
    explicit ReducedMotionGuard(bool reduced) { synth::ui::setReducedMotionForTest(reduced); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

struct PickerCapture {
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    PickerCapture() {
        synth::ui::MixerInsertList::pickerHookForTest() = [this](auto p) { picker = std::move(p); };
    }
    ~PickerCapture() { synth::ui::MixerInsertList::pickerHookForTest() = nullptr; }
    void pick(const juce::String& text) {
        ASSERT_NE(picker, nullptr);
        const auto items = picker->getVisibleItemTextsForTest();
        const auto it = std::find(items.begin(), items.end(), text);
        ASSERT_NE(it, items.end()) << text;
        picker->chooseVisibleItemForTest((int)(it - items.begin()));
    }
};

struct Rig {
    MainComponent mc{std::make_unique<MockProviderMRMT>()};
    NodeID busA, busB;

    explicit Rig(bool animate) {
        mc.setSize(1400, 900);
        mixer().setVisible(true); // a hidden mixer defers its rebuilds
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        mc.simulateAddAudioTrackClick();
        busA = mixer().createBus();
        busB = mixer().createBus();
        mixer().setSize(1400, 360);
        mixer().rebuild();
        if (animate)
            mixer().forceColumnMotionForTest(true);
    }
    synth::ui::MixerPanelComponent& mixer() { return mc.getBottomDock().getMixerPanel(); }
    synth::ui::MixerColumnComponent& track() { return *mixer().getStripColumnForTest(0); }
    synth::ui::ExitEnterListMotion& sends() {
        return track().getRowsMotionForTest().motionFor(MixerRowsMotion::List::Sends);
    }
    synth::ui::ExitEnterListMotion& inserts() {
        return track().getRowsMotionForTest().motionFor(MixerRowsMotion::List::Inserts);
    }
    // Where send row `index` is on the column, in the column's coordinates.
    juce::Rectangle<float> sendRowOnColumn(int index) {
        auto& column = track();
        auto& viewport = column.getSectionViewportForTest(synth::ui::MixerSection::Sends);
        return column.getSendListForTest().getRowBounds(index).translated(viewport.getX(), viewport.getY()).toFloat();
    }
    void addInsert(const juce::String& effect) {
        PickerCapture capture;
        track().getInsertListForTest().activateAddRow();
        capture.pick(effect);
    }
};

} // namespace

TEST(MixerRowsMotion, AnAddedSendOpensAGapGrowsInAndLandsOnItsRow) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    rig.track().getSendListForTest().addSendTo(rig.busA);

    ASSERT_EQ(rig.track().getSendListForTest().getRowCount(), 1);
    ASSERT_TRUE(rig.sends().isRunning());
    EXPECT_TRUE(rig.sends().timeline().hasEnter);
    EXPECT_FALSE(rig.sends().timeline().hasExit);
    EXPECT_FALSE(rig.inserts().isRunning()) << "the inserts did not change";

    rig.sends().applyAtMs(rig.sends().timeline().totalMs() - 100.0);
    const auto row = rig.sendRowOnColumn(0);
    const auto drawn = rig.sends().drawnRectFor("0");
    ASSERT_TRUE(drawn.has_value());
    EXPECT_NEAR(drawn->getCentreY(), row.getCentreY(), 1.0f) << "the picture ends where the real row is";
    EXPECT_GT(rig.sends().outlineAlphaFor("0"), 0.0f) << "an outline marks the arrival";
}

TEST(MixerRowsMotion, ARemovedSendShrinksAwayThenTheRowsBelowCloseUp) {
    ReducedMotionGuard guard(false);
    Rig rig(false);
    rig.track().getSendListForTest().addSendTo(rig.busA);
    rig.track().getSendListForTest().addSendTo(rig.busB);
    rig.mixer().forceColumnMotionForTest(true);
    ASSERT_EQ(rig.track().getSendListForTest().getRowCount(), 2);
    const auto secondBefore = rig.sendRowOnColumn(1);

    rig.track().getSendListForTest().removeRow(0);

    ASSERT_EQ(rig.track().getSendListForTest().getRowCount(), 1) << "the model changed at once";
    ASSERT_TRUE(rig.sends().isRunning());
    EXPECT_TRUE(rig.sends().timeline().hasExit);
    EXPECT_EQ(rig.sends().exitGhostCount(), 1);

    rig.sends().applyAtMs(ExitEnterTimeline::kExitMs);
    EXPECT_FALSE(rig.sends().drawnRectFor("0").has_value()) << "the removed row is gone";
    ASSERT_TRUE(rig.sends().drawnRectFor("1").has_value());
    EXPECT_NEAR(rig.sends().drawnRectFor("1")->getY(), secondBefore.getY(), 0.5f) << "the row below waits for the exit";

    rig.sends().applyAtMs(rig.sends().timeline().totalMs());
    EXPECT_NEAR(rig.sends().drawnRectFor("1")->getY(), rig.sendRowOnColumn(0).getY(), 0.5f)
        << "and ends on the row's real place";
}

TEST(MixerRowsMotion, UndoingARemovedInsertMakesRoomAndGrowsItBack) {
    ReducedMotionGuard guard(false);
    Rig rig(false);
    rig.addInsert("Reverb");
    const int withReverb = rig.track().getInsertListForTest().getEntryCountForTest();
    rig.mixer().forceColumnMotionForTest(true);

    rig.track().getInsertListForTest().removeRow(withReverb - 1);
    ASSERT_EQ(rig.track().getInsertListForTest().getEntryCountForTest(), withReverb - 1);
    ASSERT_TRUE(rig.inserts().isRunning());
    EXPECT_EQ(rig.inserts().exitGhostCount(), 1);
    rig.inserts().finishNow();

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    ASSERT_EQ(rig.track().getInsertListForTest().getEntryCountForTest(), withReverb);
    ASSERT_TRUE(rig.inserts().isRunning());
    EXPECT_TRUE(rig.inserts().timeline().hasEnter);
    rig.inserts().applyAtMs(rig.inserts().timeline().totalMs() - 100.0);
    EXPECT_GT(rig.inserts().outlineAlphaFor(rig.track().getInsertListForTest().getRowUuid(withReverb - 1)), 0.0f);
}

TEST(MixerRowsMotion, AnInsertAddedFromThePickerGrowsIn) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    const int before = rig.track().getInsertListForTest().getEntryCountForTest();
    rig.addInsert("Reverb");
    ASSERT_EQ(rig.track().getInsertListForTest().getEntryCountForTest(), before + 1);
    ASSERT_TRUE(rig.inserts().isRunning());
    EXPECT_TRUE(rig.inserts().timeline().hasEnter);
}

// A cable from the chain to a module that leads nowhere puts a not-connected row in (it opens a gap and grows in);
// cutting the cable takes it out again (it shrinks away and the rows below close up).
TEST(MixerRowsMotion, ANotConnectedRowGrowsInOnConnectAndShrinksAwayOnDisconnect) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    rig.addInsert("Reverb");
    rig.inserts().finishNow();
    auto& graph = rig.mc.getAudioEngine().getGraph();
    const auto firstUuid = rig.track().getInsertListForTest().getRowUuid(0);
    juce::AudioProcessorGraph::Node* first = nullptr;
    for (auto* node : graph.getNodes())
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            if (juce::String(module->getNodeUuid()) == firstUuid)
                first = node;
    ASSERT_NE(first, nullptr);
    rig.mc.getGraphEditor().addModuleAtCanvasPosition("Delay", {3000, 3000}, nullptr);
    juce::AudioProcessorGraph::Node* delay = nullptr;
    for (auto* node : graph.getNodes())
        if (node->getProcessor()->getName() == "Delay")
            delay = node;
    ASSERT_NE(delay, nullptr);
    rig.mixer().rebuild();
    ASSERT_EQ(rig.track().getInsertListForTest().getDanglingCount(), 0) << "no cable yet, nothing to show";
    rig.inserts().finishNow();

    rig.mc.getUndoManager().recordStructuralChange(graph, [&] {
        graph.addConnection({{first->nodeID, 0}, {delay->nodeID, 0}});
        graph.addConnection({{first->nodeID, 1}, {delay->nodeID, 1}});
    });
    rig.mixer().rebuild();
    ASSERT_EQ(rig.track().getInsertListForTest().getDanglingCount(), 1);
    ASSERT_TRUE(rig.inserts().isRunning());
    EXPECT_TRUE(rig.inserts().timeline().hasEnter);
    EXPECT_FALSE(rig.inserts().timeline().hasExit);
    rig.inserts().finishNow();

    ASSERT_TRUE(rig.mc.getUndoManager().undo()); // the cable goes
    rig.mixer().rebuild();
    ASSERT_EQ(rig.track().getInsertListForTest().getDanglingCount(), 0);
    ASSERT_TRUE(rig.inserts().isRunning());
    EXPECT_TRUE(rig.inserts().timeline().hasExit);
    EXPECT_EQ(rig.inserts().exitGhostCount(), 1);
}

// A module that is a not-connected row and then an insert (or the reverse) is one row: no row comes or goes, no picture
// plays over the list; the row itself dims (or brightens) as it slides to its place. Plugging in = the cable from the
// chain's last module to the strip moves to the module hung off it; unplugging is the undo.
TEST(MixerRowsMotion, AModuleThatJoinsOrLeavesTheChainSlidesAndDimsInPlaceInsteadOfLandingAtOnce) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    rig.addInsert("Reverb");
    rig.inserts().finishNow();
    auto& graph = rig.mc.getAudioEngine().getGraph();
    const auto firstUuid = rig.track().getInsertListForTest().getRowUuid(0);
    juce::AudioProcessorGraph::Node* first = nullptr;
    for (auto* node : graph.getNodes())
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            if (juce::String(module->getNodeUuid()) == firstUuid)
                first = node;
    ASSERT_NE(first, nullptr);
    rig.mc.getGraphEditor().addModuleAtCanvasPosition("Delay", {3000, 3000}, nullptr);
    juce::AudioProcessorGraph::Node* delay = nullptr;
    for (auto* node : graph.getNodes())
        if (node->getProcessor()->getName() == "Delay")
            delay = node;
    ASSERT_NE(delay, nullptr);
    rig.mc.getUndoManager().recordStructuralChange(graph, [&] {
        graph.addConnection({{first->nodeID, 0}, {delay->nodeID, 0}});
        graph.addConnection({{first->nodeID, 1}, {delay->nodeID, 1}});
    });
    rig.mixer().rebuild();
    rig.inserts().finishNow();
    auto& list = rig.track().getInsertListForTest();
    ASSERT_EQ(list.getDanglingCount(), 1);
    const auto delayUuid = juce::String(dynamic_cast<ModuleBase*>(delay->getProcessor())->getNodeUuid());
    list.setKindFlipAmountForTest(1.0f);

    // Plug in: the first module's cable to the strip moves to the Delay, so the Delay is the chain's last insert.
    std::vector<juce::AudioProcessorGraph::Connection> towardStrip;
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == first->nodeID && c.destination.nodeID != delay->nodeID)
            towardStrip.push_back(c);
    ASSERT_FALSE(towardStrip.empty());
    rig.mc.getUndoManager().recordStructuralChange(graph, [&] {
        for (const auto& c : towardStrip) {
            graph.removeConnection(c);
            graph.addConnection({{delay->nodeID, c.source.channelIndex}, c.destination});
        }
    });
    rig.mixer().rebuild();
    auto& plugged = rig.track().getInsertListForTest();
    ASSERT_EQ(plugged.getDanglingCount(), 0);
    ASSERT_EQ(plugged.getRowCount() >= 3, true) << "Reverb, Delay and the + Insert row";
    EXPECT_FALSE(rig.inserts().isRunning()) << "no row came or went, so no picture plays over the list";
    ASSERT_TRUE(plugged.isKindFlipRunning());
    EXPECT_FLOAT_EQ(plugged.kindFlipDimFor(delayUuid), 1.0f) << "it starts dimmed, as it was";
    plugged.setKindFlipAmountForTest(0.5f);
    EXPECT_NEAR(plugged.kindFlipDimFor(delayUuid), 0.5f, 1e-4f);
    plugged.setKindFlipAmountForTest(1.0f);
    EXPECT_FALSE(plugged.isKindFlipRunning());
    EXPECT_FLOAT_EQ(plugged.kindFlipDimFor(delayUuid), 0.0f);

    // Unplug (undo): the same row dims again where it stands.
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    rig.mixer().rebuild();
    auto& unplugged = rig.track().getInsertListForTest();
    ASSERT_EQ(unplugged.getDanglingCount(), 1);
    EXPECT_FALSE(rig.inserts().isRunning());
    ASSERT_TRUE(unplugged.isKindFlipRunning());
    EXPECT_FLOAT_EQ(unplugged.kindFlipDimFor(delayUuid), 0.0f) << "it starts as a chain row";
    unplugged.setKindFlipAmountForTest(0.5f);
    EXPECT_NEAR(unplugged.kindFlipDimFor(delayUuid), 0.5f, 1e-4f);
    unplugged.setKindFlipAmountForTest(1.0f);
    EXPECT_FALSE(unplugged.isKindFlipRunning());
}

// Plugging a not-connected module into the strip beside the chain makes a split whose paths are folded: the module's
// row goes under the fold and the split row grows in where it stood (and the reverse on undo), instead of the two
// swapping in one frame.
TEST(MixerRowsMotion, APluggedInModuleBecomesAFoldedSplitWithMotionAndComesBackOutOfItOnUndo) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    rig.addInsert("Reverb");
    rig.inserts().finishNow();
    auto& graph = rig.mc.getAudioEngine().getGraph();
    const auto firstUuid = rig.track().getInsertListForTest().getRowUuid(0);
    juce::AudioProcessorGraph::Node* first = nullptr;
    for (auto* node : graph.getNodes())
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            if (juce::String(module->getNodeUuid()) == firstUuid)
                first = node;
    ASSERT_NE(first, nullptr);
    rig.mc.getGraphEditor().addModuleAtCanvasPosition("Delay", {3000, 3000}, nullptr);
    juce::AudioProcessorGraph::Node* delay = nullptr;
    for (auto* node : graph.getNodes())
        if (node->getProcessor()->getName() == "Delay")
            delay = node;
    ASSERT_NE(delay, nullptr);
    rig.mc.getUndoManager().recordStructuralChange(graph, [&] {
        graph.addConnection({{first->nodeID, 0}, {delay->nodeID, 0}});
        graph.addConnection({{first->nodeID, 1}, {delay->nodeID, 1}});
    });
    rig.mixer().rebuild();
    rig.inserts().finishNow();
    ASSERT_EQ(rig.track().getInsertListForTest().getDanglingCount(), 1);

    std::vector<juce::AudioProcessorGraph::Connection> towardStrip;
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == first->nodeID && c.destination.nodeID != delay->nodeID)
            towardStrip.push_back(c);
    ASSERT_FALSE(towardStrip.empty());
    rig.mc.getUndoManager().recordStructuralChange(graph, [&] {
        for (const auto& c : towardStrip)
            graph.addConnection({{delay->nodeID, c.source.channelIndex}, c.destination});
    });
    rig.mixer().rebuild();
    ASSERT_EQ(rig.track().getInsertListForTest().getDanglingCount(), 0);
    ASSERT_TRUE(rig.inserts().isRunning()) << "the split row comes in; the module's row is not a row that went";
    EXPECT_TRUE(rig.inserts().timeline().hasEnter);
    rig.inserts().finishNow();

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    rig.mixer().rebuild();
    ASSERT_EQ(rig.track().getInsertListForTest().getDanglingCount(), 1);
    ASSERT_TRUE(rig.inserts().isRunning()) << "the split's rows shrink away; the module's row shows where they were";
    EXPECT_TRUE(rig.inserts().timeline().hasExit);
}

TEST(MixerRowsMotion, ReduceMotionFadesTheRowInsteadOfScalingIt) {
    ReducedMotionGuard guard(true);
    Rig rig(true);
    rig.track().getSendListForTest().addSendTo(rig.busA);
    ASSERT_TRUE(rig.sends().isRunning());
    EXPECT_TRUE(rig.sends().isReducedMotion());
}

TEST(MixerRowsMotion, NothingMovesWhenTheRowsOnlyChangeInPlace) {
    ReducedMotionGuard guard(false);
    Rig rig(false);
    rig.track().getSendListForTest().addSendTo(rig.busA);
    rig.mixer().forceColumnMotionForTest(true);

    rig.track().getSendListForTest().togglePreFaderForRow(0);

    EXPECT_FALSE(rig.sends().isRunning());
    EXPECT_FALSE(rig.inserts().isRunning());
}

TEST(MixerRowsMotion, ALandedMixerPlaysNoMotionWhenItIsNotShown) {
    ReducedMotionGuard guard(false);
    Rig rig(false); // never forced: headless is not showing
    rig.track().getSendListForTest().addSendTo(rig.busA);
    ASSERT_EQ(rig.track().getSendListForTest().getRowCount(), 1);
    EXPECT_FALSE(rig.sends().isRunning());
}

TEST(MixerRowsMotion, MastersInsertRowsMoveToo) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    auto* master = rig.mixer().getMasterColumnForTest();
    ASSERT_NE(master, nullptr);
    master->forceRowMotionForTest(true);
    PickerCapture capture;
    master->getInsertListForTest().activateAddRow();
    capture.pick("Reverb");

    ASSERT_EQ(master->getInsertListForTest().getEntryCountForTest(), 1);
    auto& motion = master->getRowsMotionForTest().motionFor(MixerRowsMotion::List::Inserts);
    ASSERT_TRUE(motion.isRunning());
    EXPECT_TRUE(motion.timeline().hasEnter);
}
