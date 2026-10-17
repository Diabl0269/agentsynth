// MixerRowsMotionTests.cpp (docs/layout/animation.md "Delete and undo animation"): an insert or send row that is
// added or removed opens or closes where it was instead of jumping (MixerRowsMotion). Drives a real off-screen
// MainComponent through the lists' own mutations (+ Send, remove, the effect picker, undo), which rebuild the column,
// and steps each list's ExitEnterListMotion by hand.
#include "AI/AIProvider.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
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
