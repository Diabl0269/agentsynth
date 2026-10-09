// MixerPanelColumnMotionTests.cpp: a mixer column that leaves shrinks away and the columns after it close up; one that
// comes back (an undo or redo) is made room for, grows back and gets a fading outline around it
// (MixerPanelColumnMotion.cpp). Drives a real off-screen MainComponent: Duplicate Track from the timeline row's menu
// makes a middle column that Cmd+Z removes and Cmd+Shift+Z brings back, and the panels' ExitEnterListMotion is
// stepped by hand.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::ExitEnterTimeline;

class MockProviderMPCM : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMPCM"; }
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

struct Rig {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderMPCM>()};

    explicit Rig(bool animate) {
        mc.setSize(1400, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        for (int i = 0; i < 3; ++i)
            mc.simulateAddAudioTrackClick();
        mixer().setSize(1400, 360);
        mixer().rebuild();
        mc.getTimelinePanel().setSize(1400, 360);
        if (animate) {
            mixer().forceColumnMotionForTest(true);
            mc.getTimelinePanel().forceTrackGlideForTest(true);
        }
    }
    synth::ui::MixerPanelComponent& mixer() { return mc.getBottomDock().getMixerPanel(); }
    auto& motion() { return mixer().getColumnMotionForTest(); }
    std::vector<synth::ui::MixerColumnComponent*> columns() {
        std::vector<synth::ui::MixerColumnComponent*> out;
        for (int i = 0; auto* column = mixer().getStripColumnForTest(i); ++i)
            out.push_back(column);
        return out;
    }
    // Where a column stands on the panel: its own x plus the scrolling viewport's.
    float panelX(const synth::ui::MixerColumnComponent& column) {
        return static_cast<float>(mixer().getViewportForTest().getX() + column.getX());
    }
    void choose(int trackIndex, int menuId) {
        auto* header = mc.getTimelinePanel().getTrackHeaderAt(trackIndex);
        ASSERT_NE(header, nullptr);
        header->applyContextMenuChoice(menuId);
    }
    // What choosing Duplicate Track on the first track's row does: a copy and its channel, right after it.
    void duplicateFirstTrack() { choose(0, synth::ui::TimelineTrackHeaderComponent::kDuplicateTrackMenuId); }
};

} // namespace

TEST(MixerPanelColumnMotion, UndoingADuplicateShrinksItsColumnThenTheColumnsAfterItCloseUp) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    rig.duplicateFirstTrack();
    ASSERT_EQ(rig.columns().size(), 4u);
    EXPECT_FALSE(rig.motion().isRunning()) << "a column added any other way lands at once";
    rig.motion().finishNow();
    const auto before = rig.columns();
    const auto copy = before[1]->getUuid();
    const auto after = before[2]->getUuid();
    const float afterOld = rig.panelX(*before[2]);
    const float copyX = rig.panelX(*before[1]);
    const float pitch = afterOld - copyX;

    ASSERT_TRUE(rig.mc.getUndoManager().undo());

    ASSERT_EQ(rig.columns().size(), 3u) << "the model changed at once";
    ASSERT_TRUE(rig.motion().isRunning());
    EXPECT_EQ(rig.motion().exitGhostCount(), 1);
    ASSERT_TRUE(rig.motion().drawnRectFor(copy).has_value());

    rig.motion().applyAtMs(ExitEnterTimeline::kExitMs * 0.5);
    const auto shrinking = *rig.motion().drawnRectFor(copy);
    EXPECT_LT(shrinking.getWidth(), pitch);
    EXPECT_NEAR(shrinking.getCentreX(), copyX + pitch * 0.5f, 1.0f) << "toward its centre";
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(after)->getX(), afterOld) << "the column after it waits for the exit";

    rig.motion().applyAtMs(ExitEnterTimeline::kExitMs);
    EXPECT_FALSE(rig.motion().drawnRectFor(copy).has_value());
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(after)->getX(), afterOld);

    rig.motion().applyAtMs(rig.motion().timeline().totalMs());
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(after)->getX(), afterOld - pitch);
    EXPECT_FLOAT_EQ(rig.panelX(*rig.columns()[1]), afterOld - pitch) << "the real column is where the picture ends";

    // The copy's row on the timeline went the same way.
    EXPECT_TRUE(rig.mc.getTimelinePanel().getTrackListMotionForTest().isRunning());
}

TEST(MixerPanelColumnMotion, RedoMakesRoomGrowsTheColumnBackAndOutlinesIt) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    rig.duplicateFirstTrack();
    const auto copy = rig.columns()[1]->getUuid();
    const auto after = rig.columns()[2]->getUuid();
    const float afterX = rig.panelX(*rig.columns()[2]);
    const float pitch = afterX - rig.panelX(*rig.columns()[1]);
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    rig.motion().finishNow();

    ASSERT_TRUE(rig.mc.getUndoManager().redo());

    ASSERT_EQ(rig.columns().size(), 4u);
    ASSERT_TRUE(rig.motion().isRunning());
    EXPECT_TRUE(rig.motion().timeline().hasEnter);
    EXPECT_FALSE(rig.motion().drawnRectFor(copy).has_value()) << "the gap opens first";
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(after)->getX(), afterX - pitch);

    rig.motion().applyAtMs(ExitEnterTimeline::kGapMs);
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(after)->getX(), afterX);
    EXPECT_FALSE(rig.motion().drawnRectFor(copy).has_value());

    rig.motion().applyAtMs(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs * 0.5);
    ASSERT_TRUE(rig.motion().drawnRectFor(copy).has_value());
    EXPECT_LT(rig.motion().drawnRectFor(copy)->getWidth(), pitch);

    rig.motion().applyAtMs(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs +
                           ExitEnterTimeline::kOutlineMs * 0.25);
    EXPECT_GT(rig.motion().outlineAlphaFor(copy), 0.0f);
    EXPECT_EQ(rig.columns()[1]->getUuid(), copy) << "the real column is back, with its own title";

    rig.motion().applyAtMs(rig.motion().timeline().totalMs());
    EXPECT_FLOAT_EQ(rig.motion().outlineAlphaFor(copy), 0.0f);
}

TEST(MixerPanelColumnMotion, ReduceMotionFadesTheColumnInPlace) {
    ReducedMotionGuard guard(true);
    Rig rig(true);
    rig.duplicateFirstTrack();
    const float width = rig.panelX(*rig.columns()[2]) - rig.panelX(*rig.columns()[1]);
    const auto copy = rig.columns()[1]->getUuid();
    ASSERT_TRUE(rig.mc.getUndoManager().undo());

    ASSERT_TRUE(rig.motion().isRunning());
    rig.motion().applyAtMs(ExitEnterTimeline::kExitMs * 0.5);
    ASSERT_TRUE(rig.motion().drawnRectFor(copy).has_value());
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(copy)->getWidth(), width) << "a fade, not a shrink";
}

TEST(MixerPanelColumnMotion, OffScreenTheColumnsLandAtOnce) {
    ReducedMotionGuard guard(false);
    Rig rig(false);
    rig.duplicateFirstTrack();
    ASSERT_EQ(rig.columns().size(), 4u);
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    EXPECT_FALSE(rig.motion().isRunning());
    EXPECT_FALSE(rig.mc.getTimelinePanel().getTrackListMotionForTest().isRunning());
    EXPECT_EQ(rig.columns().size(), 3u);
    ASSERT_TRUE(rig.mc.getUndoManager().redo());
    EXPECT_FALSE(rig.motion().isRunning());
    EXPECT_EQ(rig.columns().size(), 4u);
}

TEST(MixerPanelColumnMotion, ADeletedTracksChannelLeavesWithItsRow) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    const auto channels = rig.columns().size();

    rig.choose(1, synth::ui::TimelineTrackHeaderComponent::kDeleteTrackMenuId);

    EXPECT_EQ(rig.columns().size(), channels - 1) << "the channel is deleted with its track";
    EXPECT_TRUE(rig.motion().isRunning()) << "its column shrinks away like the row";
    EXPECT_EQ(rig.motion().exitGhostCount(), 1);
    EXPECT_TRUE(rig.mc.getTimelinePanel().getTrackListMotionForTest().isRunning());
}
