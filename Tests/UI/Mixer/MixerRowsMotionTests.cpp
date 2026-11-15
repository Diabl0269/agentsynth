// MixerRowsMotionTests.cpp (docs/layout/animation.md "Delete and undo animation"): an insert or send row that is
// added or removed opens or closes where it was instead of jumping (MixerRowsMotion). Drives a real off-screen
// MainComponent through the lists' own mutations (+ Send, remove, the effect picker, undo), which rebuild the column,
// and steps each list's ExitEnterListMotion by hand.
#include "../Layout/UndoGlideSurfaces.h"
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
#include "UI/Mixer/MixerRowLookGlide.h"
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

// The look glides (an undo's rows and strip name) animate although the test's window is not on screen.
struct LookGlideForced {
    LookGlideForced() { MixerRowsMotion::setLookGlideForcedForTest(true); }
    ~LookGlideForced() { MixerRowsMotion::setLookGlideForcedForTest(false); }
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

// ---- An undo or redo glides what the forward edit did (MixerRowLookGlide) -----------------------------------------
// Each case: the first frame draws the old look, a frame in between draws something between, the landed look is the
// real one; then redo; then a second undo that starts while the first is still gliding starts from where it is drawn.

namespace {
const synth::ui::MixerRowLook* lookOf(const std::vector<synth::ui::MixerRowLook>& looks, const juce::String& key) {
    for (const auto& look : looks)
        if (look.key == key)
            return &look;
    return nullptr;
}

// A send is told by its name: its key is the slot, and a move swaps what the slots hold.
const synth::ui::MixerRowLook* byName(const std::vector<synth::ui::MixerRowLook>& looks, const juce::String& name) {
    for (const auto& look : looks)
        if (look.name == name)
            return &look;
    return nullptr;
}
} // namespace

TEST(MixerRowsMotion, UndoingABypassDimsTheInsertRowBackOverTimeAndRedoDimsItAgain) {
    ReducedMotionGuard guard(false);
    LookGlideForced forced;
    Rig rig(true);
    rig.track().getInsertListForTest().addModule("Reverb");
    rig.track().getInsertListForTest().addModule("Chorus");
    const auto uuid = rig.track().getInsertListForTest().rowLooks().front().key;
    const float was = lookOf(rig.track().getInsertListForTest().rowLooks(), uuid)->dim; // what an undo returns to
    const float edited = 1.0f - was;
    rig.track().getInsertListForTest().toggleBypassForRow(0);
    rig.track().getInsertListForTest().lookGlideForTest().stepForTest(2000.0); // the edit's own glide lands

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    auto* list = &rig.track().getInsertListForTest();
    ASSERT_TRUE(list->lookGlideForTest().isRunning());
    EXPECT_FLOAT_EQ(lookOf(list->drawnRowLooks(), uuid)->dim, edited) << "it starts as the row the edit left";
    list->lookGlideForTest().stepForTest(70.0);
    const float mid = lookOf(list->drawnRowLooks(), uuid)->dim;
    EXPECT_GT(mid, 0.05f);
    EXPECT_LT(mid, 0.95f);
    list->lookGlideForTest().stepForTest(2000.0);
    EXPECT_FLOAT_EQ(lookOf(list->drawnRowLooks(), uuid)->dim, was) << "and lands on the real look";
    EXPECT_FALSE(list->lookGlideForTest().isRunning());

    // Redo, then undo again while the redo is mid-glide: the new glide leaves from where the row is drawn.
    ASSERT_TRUE(rig.mc.getUndoManager().redo());
    list = &rig.track().getInsertListForTest();
    ASSERT_TRUE(list->lookGlideForTest().isRunning());
    EXPECT_FLOAT_EQ(lookOf(list->drawnRowLooks(), uuid)->dim, was);
    list->lookGlideForTest().stepForTest(70.0);
    const float drawn = lookOf(list->drawnRowLooks(), uuid)->dim;
    EXPECT_GT(drawn, 0.05f);
    EXPECT_LT(drawn, 0.95f);
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    list = &rig.track().getInsertListForTest();
    ASSERT_TRUE(list->lookGlideForTest().isRunning());
    EXPECT_NEAR(lookOf(list->drawnRowLooks(), uuid)->dim, drawn, 1e-3f) << "from the drawn place, not from bypassed";
}

TEST(MixerRowsMotion, TheForwardBypassOfAnInsertRowDimsItOverTime) {
    ReducedMotionGuard guard(false);
    LookGlideForced forced;
    Rig rig(true);
    rig.track().getInsertListForTest().addModule("Reverb");
    rig.track().getInsertListForTest().addModule("Chorus");
    const auto key = rig.track().getInsertListForTest().rowLooks().front().key;
    const float was = lookOf(rig.track().getInsertListForTest().rowLooks(), key)->dim;
    rig.track().getInsertListForTest().toggleBypassForRow(0);
    auto& list = rig.track().getInsertListForTest();
    ASSERT_TRUE(list.lookGlideForTest().isRunning()) << "the click glides the dim, it does not pop";
    EXPECT_FLOAT_EQ(lookOf(list.drawnRowLooks(), key)->dim, was) << "the first frame is the old look";
    list.lookGlideForTest().stepForTest(70.0);
    const float mid = lookOf(list.drawnRowLooks(), key)->dim;
    EXPECT_GT(std::min(mid, 1.0f - mid), 0.05f) << "in between the two looks";
    list.lookGlideForTest().stepForTest(2000.0);
    EXPECT_FLOAT_EQ(lookOf(list.drawnRowLooks(), key)->dim, 1.0f - was);
    EXPECT_FALSE(list.lookGlideForTest().isRunning());
}

TEST(MixerRowsMotion, TheForwardBypassOfASendRowDimsItOverTime) {
    ReducedMotionGuard guard(false);
    LookGlideForced forced;
    Rig rig(true);
    rig.track().getSendListForTest().addSendTo(rig.busA);
    rig.track().getSendListForTest().lookGlideForTest().stepForTest(2000.0);
    const float was = rig.track().getSendListForTest().rowLooks().front().dim;
    rig.track().getSendListForTest().toggleBypassForRow(0);
    auto& list = rig.track().getSendListForTest();
    ASSERT_TRUE(list.lookGlideForTest().isRunning());
    EXPECT_FLOAT_EQ(list.drawnRowLooks().front().dim, was);
    list.lookGlideForTest().stepForTest(70.0);
    const float mid = list.drawnRowLooks().front().dim;
    EXPECT_GT(std::min(mid, 1.0f - mid), 0.05f);
    list.lookGlideForTest().stepForTest(2000.0);
    EXPECT_FLOAT_EQ(list.drawnRowLooks().front().dim, 1.0f - was);
}

TEST(MixerRowsMotion, TheForwardBypassUnderAnimationsOffLandsAtOnce) {
    LookGlideForced forced;
    synth::ui::MixerRowsMotion::setLookGlideForcedForTest(false);
    const auto mode = synth::ui::animationMode();
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    {
        ReducedMotionGuard guard(false);
        Rig rig(true);
        rig.track().getInsertListForTest().addModule("Reverb");
        rig.track().getInsertListForTest().toggleBypassForRow(0);
        EXPECT_FALSE(rig.track().getInsertListForTest().lookGlideForTest().isRunning());
    }
    synth::ui::setAnimationMode(mode);
}

TEST(MixerRowsMotion, UndoingAMoveSlidesBothRowsBackToTheirPlaces) {
    ReducedMotionGuard guard(false);
    LookGlideForced forced;
    Rig rig(true);
    auto& start = rig.track().getInsertListForTest();
    start.addModule("Reverb");
    rig.track().getInsertListForTest().addModule("Chorus");
    auto* list = &rig.track().getInsertListForTest();
    const auto first = list->getRowUuid(0), second = list->getRowUuid(1);
    const float firstY = (float)list->getRowBounds(0).getY(), secondY = (float)list->getRowBounds(1).getY();
    ASSERT_TRUE(list->moveRowTo(1, 0));
    ASSERT_EQ(rig.track().getInsertListForTest().getRowUuid(0), second);

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    list = &rig.track().getInsertListForTest();
    ASSERT_EQ(list->getRowUuid(0), first);
    ASSERT_TRUE(list->lookGlideForTest().isRunning());
    auto looks = list->drawnRowLooks();
    EXPECT_FLOAT_EQ(lookOf(looks, first)->y, secondY) << "the first row starts where the move left it (second place)";
    EXPECT_FLOAT_EQ(lookOf(looks, second)->y, firstY);
    EXPECT_EQ(list->getRowBounds(0).getY(), (int)firstY) << "the geometry is final at once";
    list->lookGlideForTest().stepForTest(70.0);
    looks = list->drawnRowLooks();
    EXPECT_GT(lookOf(looks, first)->y, firstY);
    EXPECT_LT(lookOf(looks, first)->y, secondY);
    list->lookGlideForTest().stepForTest(2000.0);
    looks = list->drawnRowLooks();
    EXPECT_FLOAT_EQ(lookOf(looks, first)->y, firstY);
    EXPECT_FLOAT_EQ(lookOf(looks, second)->y, secondY);

    ASSERT_TRUE(rig.mc.getUndoManager().redo()); // the move again
    list = &rig.track().getInsertListForTest();
    ASSERT_EQ(list->getRowUuid(0), second);
    ASSERT_TRUE(list->lookGlideForTest().isRunning());
    EXPECT_FLOAT_EQ(lookOf(list->drawnRowLooks(), second)->y, secondY);
}

TEST(MixerRowsMotion, TheControlsOfAGlidingRowFollowItsDrawnPlace) {
    ReducedMotionGuard guard(false);
    LookGlideForced forced;
    Rig rig(true);
    rig.track().getInsertListForTest().addModule("Reverb");
    rig.track().getInsertListForTest().addModule("Chorus");
    ASSERT_TRUE(rig.track().getInsertListForTest().moveRowTo(1, 0));
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    auto& list = rig.track().getInsertListForTest();
    ASSERT_TRUE(list.lookGlideForTest().isRunning());
    const int firstTop = list.getRowBounds(0).getY(), secondTop = list.getRowBounds(1).getY();
    EXPECT_EQ(list.getBypassButtonForTest(0)->getY() - 1, secondTop) << "row 0's bypass button is where it was";
    list.lookGlideForTest().stepForTest(2000.0);
    EXPECT_EQ(list.getBypassButtonForTest(0)->getY() - 1, firstTop);
}

TEST(MixerRowsMotion, UndoingASendRetargetCrossFadesItsName) {
    ReducedMotionGuard guard(false);
    LookGlideForced forced;
    Rig rig(true);
    rig.track().getSendListForTest().addSendTo(rig.busA);
    const auto before = rig.track().getSendListForTest().rowLooks().front().name;
    rig.track().getSendListForTest().retargetRow(0, rig.busB);
    const auto after = rig.track().getSendListForTest().rowLooks().front().name;
    ASSERT_NE(before, after);

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    auto& list = rig.track().getSendListForTest();
    ASSERT_EQ(list.rowLooks().front().name, before);
    ASSERT_TRUE(list.lookGlideForTest().isRunning());
    EXPECT_FLOAT_EQ(list.lookGlideForTest().oldNameWeightFor(0), 1.0f) << "the retargeted name shows at first";
    EXPECT_EQ(list.lookGlideForTest().oldNameFor(0), after);
    list.lookGlideForTest().stepForTest(70.0);
    const float mid = list.lookGlideForTest().oldNameWeightFor(0);
    EXPECT_GT(mid, 0.05f);
    EXPECT_LT(mid, 0.95f);
    list.lookGlideForTest().stepForTest(2000.0);
    EXPECT_FLOAT_EQ(list.lookGlideForTest().oldNameWeightFor(0), 0.0f);
}

TEST(MixerRowsMotion, UndoingASendBypassBrightensTheRowOverTime) {
    ReducedMotionGuard guard(false);
    LookGlideForced forced;
    Rig rig(true);
    rig.track().getSendListForTest().addSendTo(rig.busA);
    rig.track().getSendListForTest().toggleBypassForRow(0);
    rig.track().getSendListForTest().lookGlideForTest().stepForTest(2000.0); // the edit's own glide lands
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    auto& list = rig.track().getSendListForTest();
    ASSERT_TRUE(list.lookGlideForTest().isRunning());
    EXPECT_FLOAT_EQ(list.drawnRowLooks().front().dim, 1.0f);
    list.lookGlideForTest().stepForTest(70.0);
    EXPECT_LT(list.drawnRowLooks().front().dim, 0.95f);
    EXPECT_GT(list.drawnRowLooks().front().dim, 0.05f);
    list.lookGlideForTest().stepForTest(2000.0);
    EXPECT_FLOAT_EQ(list.drawnRowLooks().front().dim, 0.0f);
}

TEST(MixerRowsMotion, UndoingASendMoveSlidesTheRows) {
    ReducedMotionGuard guard(false);
    LookGlideForced forced;
    Rig rig(true);
    rig.track().getSendListForTest().addSendTo(rig.busA);
    rig.track().getSendListForTest().addSendTo(rig.busB);
    const auto original = rig.track().getSendListForTest().rowLooks();
    ASSERT_TRUE(rig.track().getSendListForTest().moveRow(1, 0));
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    auto& list = rig.track().getSendListForTest();
    ASSERT_TRUE(list.lookGlideForTest().isRunning());
    const auto looks = list.drawnRowLooks();
    EXPECT_FLOAT_EQ(byName(looks, original[0].name)->y, original[1].y) << "starts where the move left it";
    list.lookGlideForTest().stepForTest(70.0);
    EXPECT_GT(byName(list.drawnRowLooks(), original[0].name)->y, original[0].y);
    list.lookGlideForTest().stepForTest(2000.0);
    EXPECT_FLOAT_EQ(byName(list.drawnRowLooks(), original[0].name)->y, original[0].y);
}

TEST(MixerRowsMotion, MastersInsertRowsGlideOnUndoToo) {
    ReducedMotionGuard guard(false);
    LookGlideForced forced;
    Rig rig(true);
    auto* master = rig.mixer().getMasterColumnForTest();
    ASSERT_NE(master, nullptr);
    master->getInsertList().addModule("Reverb");
    master->getInsertList().toggleBypassForRow(0);
    master->getInsertList().lookGlideForTest().stepForTest(2000.0); // the edit's own glide lands
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    master = rig.mixer().getMasterColumnForTest();
    auto& list = master->getInsertList();
    ASSERT_TRUE(list.lookGlideForTest().isRunning());
    EXPECT_FLOAT_EQ(list.drawnRowLooks().front().dim, 1.0f);
    list.lookGlideForTest().stepForTest(2000.0);
    EXPECT_FLOAT_EQ(list.drawnRowLooks().front().dim, 0.0f);
}

TEST(MixerRowsMotion, UndoingARenameCrossFadesTheStripName) {
    ReducedMotionGuard guard(false);
    LookGlideForced forced;
    Rig rig(true);
    auto& nameLabel = rig.track().getHeaderForTest().getNameLabelForTest();
    const auto original = rig.track().getHeaderForTest().getDisplayName();
    nameLabel.setText("Lead Vox", juce::sendNotificationSync);
    ASSERT_EQ(rig.track().getHeaderForTest().getDisplayName(), "Lead Vox");
    ASSERT_FALSE(rig.track().getHeaderForTest().isNameGlideRunningForTest()) << "the edit itself lands at once";

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    auto& header = rig.track().getHeaderForTest();
    ASSERT_EQ(header.getDisplayName(), original);
    ASSERT_TRUE(header.isNameGlideRunningForTest());
    EXPECT_FLOAT_EQ(header.getNameGlideMixForTest(), 0.0f) << "the old name shows at first";
    header.stepNameGlideForTest(70.0);
    EXPECT_GT(header.getNameGlideMixForTest(), 0.05f);
    EXPECT_LT(header.getNameGlideMixForTest(), 0.95f);
    header.stepNameGlideForTest(2000.0);
    EXPECT_FALSE(header.isNameGlideRunningForTest());
}

TEST(MixerRowsMotion, UndoingTheRemovalOfAnUnpluggedModuleGrowsItsRowBack) {
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

    // Remove the not-connected module: its row shrinks away; undo: the row makes room and grows back in.
    const auto delayId = delay->nodeID;
    rig.mc.getUndoManager().recordStructuralChange(graph, [&] { graph.removeNode(delayId); });
    rig.mixer().rebuild();
    ASSERT_EQ(rig.track().getInsertListForTest().getDanglingCount(), 0);
    ASSERT_TRUE(rig.inserts().isRunning());
    EXPECT_EQ(rig.inserts().exitGhostCount(), 1);
    rig.inserts().finishNow();

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    ASSERT_EQ(rig.track().getInsertListForTest().getDanglingCount(), 1);
    ASSERT_TRUE(rig.inserts().isRunning());
    EXPECT_TRUE(rig.inserts().timeline().hasEnter);
    EXPECT_FALSE(rig.inserts().timeline().hasExit);
}

TEST(MixerRowsMotion, ReduceMotionFadesAnUndoneMoveInPlaceInsteadOfSlidingIt) {
    ReducedMotionGuard guard(true);
    LookGlideForced forced;
    Rig rig(true);
    rig.track().getInsertListForTest().addModule("Reverb");
    rig.track().getInsertListForTest().addModule("Chorus");
    ASSERT_TRUE(rig.track().getInsertListForTest().moveRowTo(1, 0));
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    auto& list = rig.track().getInsertListForTest();
    const auto key = list.rowLooks().front().key;
    ASSERT_TRUE(list.lookGlideForTest().isRunning());
    EXPECT_FLOAT_EQ(lookOf(list.drawnRowLooks(), key)->y, list.rowLooks().front().y) << "no slide: it is at its place";
    EXPECT_LT(list.lookGlideForTest().alphaFor(0), 1.0f) << "it fades in there instead";
    list.lookGlideForTest().stepForTest(2000.0);
    EXPECT_FLOAT_EQ(list.lookGlideForTest().alphaFor(0), 1.0f);
}

TEST(MixerRowsMotion, AnimationsOffLandsAnUndoneBypassAtOnce) {
    ReducedMotionGuard guard(false);
    LookGlideForced forced;
    synth::ui::MixerRowsMotion::setLookGlideForcedForTest(false); // Animations Off must win without the test seam
    const auto mode = synth::ui::animationMode();
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    {
        Rig rig(true);
        rig.track().getInsertListForTest().addModule("Reverb");
        const float was = rig.track().getInsertListForTest().rowLooks().front().dim;
        rig.track().getInsertListForTest().toggleBypassForRow(0);
        ASSERT_TRUE(rig.mc.getUndoManager().undo());
        auto& list = rig.track().getInsertListForTest();
        EXPECT_FALSE(list.lookGlideForTest().isRunning());
        EXPECT_FLOAT_EQ(list.drawnRowLooks().front().dim, was);
    }
    synth::ui::setAnimationMode(mode);
}

// ---- The registry fixtures (UndoGlideSurfaces.h) for a mixer column's insert and send rows ----
namespace undo_glide_test {

using synth::ui::MixerRowsMotion;

namespace {

enum class Mode { InsertMove, SendMove, InsertBypass };

class MixerRowsSurface : public Surface {
public:
    explicit MixerRowsSurface(Mode mode)
        : mode_(mode) {
        synth::ui::MixerRowsMotion::setLookGlideForcedForTest(true); // not on screen: the glide is what is under test
        mc_.setSize(1400, 900);
        mixer().setVisible(true); // a hidden mixer defers its rebuilds
        mc_.getAudioEngine().suspendDeviceCallback();
        mc_.newPatchForTest();
        mc_.simulateAddAudioTrackClick();
        mixer().setSize(1400, 360);
        mixer().rebuild();
        mixer().forceColumnMotionForTest(true);
        if (mode_ == Mode::SendMove) {
            const auto busA = mixer().createBus();
            const auto busB = mixer().createBus();
            mixer().setSize(1400, 360);
            mixer().rebuild();
            sends().addSendTo(busA);
            sends().addSendTo(busB);
        } else {
            inserts().addModule("Reverb");
            inserts().addModule("Chorus");
        }
        // The row that is first before the move. A send is told by its name: its key is the slot it sits in.
        key_ = mode_ == Mode::SendMove ? looks().front().name : looks().front().key;
    }
    ~MixerRowsSurface() override { synth::ui::MixerRowsMotion::setLookGlideForcedForTest(false); }

    void move() override {
        if (mode_ == Mode::InsertMove)
            inserts().moveRowTo(1, 0);
        else if (mode_ == Mode::SendMove)
            sends().moveRow(1, 0);
        else
            inserts().toggleBypassForRow(0);
    }
    bool undo() override { return mc_.getUndoManager().undo(); }
    bool redo() override { return mc_.getUndoManager().redo(); }
    // A dim is drawn as a hundredth of a step so the registry's "the move changed the place" bar (4) applies.
    double drawn() const override {
        for (const auto& look : drawnLooks())
            if (isKey(look))
                return mode_ == Mode::InsertBypass ? look.dim * 100.0 : look.y;
        return -1.0;
    }
    bool gliding() const override { return glide().isRunning(); }
    void stepTo(double elapsedMs) override { glide().stepForTest(elapsedMs); }
    float alpha() const override { return glide().alphaFor(rowOf()); }
    juce::Component* paintedComponent() override {
        return mode_ == Mode::SendMove ? static_cast<juce::Component*>(&sends())
                                       : static_cast<juce::Component*>(&inserts());
    }

private:
    synth::ui::MixerPanelComponent& mixer() const {
        return const_cast<MainComponent&>(mc_).getBottomDock().getMixerPanel();
    }
    synth::ui::MixerColumnComponent& column() const { return *mixer().getStripColumnForTest(0); }
    synth::ui::MixerInsertList& inserts() const { return column().getInsertListForTest(); }
    synth::ui::MixerSendList& sends() const { return column().getSendListForTest(); }
    std::vector<synth::ui::MixerRowLook> looks() const {
        return mode_ == Mode::SendMove ? sends().rowLooks() : inserts().rowLooks();
    }
    std::vector<synth::ui::MixerRowLook> drawnLooks() const {
        return mode_ == Mode::SendMove ? sends().drawnRowLooks() : inserts().drawnRowLooks();
    }
    synth::ui::MixerRowLookGlide& glide() const {
        return mode_ == Mode::SendMove ? sends().lookGlideForTest() : inserts().lookGlideForTest();
    }
    bool isKey(const synth::ui::MixerRowLook& look) const {
        return (mode_ == Mode::SendMove ? look.name : look.key) == key_;
    }
    int rowOf() const {
        const auto rows = looks();
        for (int i = 0; i < (int)rows.size(); ++i)
            if (isKey(rows[(size_t)i]))
                return i;
        return 0;
    }

    Mode mode_;
    MainComponent mc_{std::make_unique<MockProviderMRMT>()};
    juce::String key_;
};

} // namespace

std::unique_ptr<Surface> makeMixerInsertRowMove() { return std::make_unique<MixerRowsSurface>(Mode::InsertMove); }
std::unique_ptr<Surface> makeMixerSendRowMove() { return std::make_unique<MixerRowsSurface>(Mode::SendMove); }
std::unique_ptr<Surface> makeMixerInsertBypassDim() { return std::make_unique<MixerRowsSurface>(Mode::InsertBypass); }

} // namespace undo_glide_test
