// MixerPanelColumnMotionTests.cpp: a mixer column that leaves shrinks away and the columns after it close up; one that
// comes back (an undo or redo) is made room for, grows back and gets a fading outline around it; a duplicate's column
// comes out of the original and goes back into it on undo
// (MixerPanelColumnMotion.cpp). Drives a real off-screen MainComponent: Duplicate Track from the timeline row's menu
// makes a middle column that Cmd+Z removes and Cmd+Shift+Z brings back, and the panels' ExitEnterListMotion is
// stepped by hand. Adding and duplicating a track grow the same column and row in.
#include "../../TestSettingsHelpers.h"
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
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
        mc.getBottomDock().getMixerPanel().setVisible(true); // a hidden mixer defers its rebuilds
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

TEST(MixerPanelColumnMotion, UndoingADuplicateSendsItsColumnBackIntoTheOriginalWhileTheColumnsAfterItCloseUp) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    rig.duplicateFirstTrack();
    ASSERT_EQ(rig.columns().size(), 4u);
    EXPECT_TRUE(rig.motion().isRunning()) << "the duplicate's own column comes out of the original (test below)";
    rig.motion().finishNow();
    const auto before = rig.columns();
    const auto copy = before[1]->getUuid();
    const auto after = before[2]->getUuid();
    const float originalX = rig.panelX(*before[0]);
    const float afterOld = rig.panelX(*before[2]);
    const float pitch = afterOld - rig.panelX(*before[1]);

    ASSERT_TRUE(rig.mc.getUndoManager().undo());

    ASSERT_EQ(rig.columns().size(), 3u) << "the model changed at once";
    ASSERT_TRUE(rig.motion().isRunning());
    EXPECT_TRUE(rig.motion().timeline().together);
    EXPECT_TRUE(rig.motion().timeline().reverse);
    EXPECT_FALSE(rig.motion().timeline().hasEnter) << "no outline: it goes back into the original";
    EXPECT_DOUBLE_EQ(rig.motion().timeline().totalMs(), ExitEnterTimeline::kIntoSourceMs);
    ASSERT_TRUE(rig.motion().drawnRectFor(copy).has_value());

    rig.motion().applyAtMs(0.0);
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(copy)->getX(), originalX + pitch) << "it starts in its slot";
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(after)->getX(), afterOld);

    float lastCopyX = rig.motion().drawnRectFor(copy)->getCentreX();
    float lastAfterX = afterOld;
    for (const double fraction : {0.25, 0.5, 0.75}) {
        rig.motion().applyAtMs(ExitEnterTimeline::kIntoSourceMs * fraction);
        const auto drawn = rig.motion().drawnRectFor(copy);
        ASSERT_TRUE(drawn.has_value()) << fraction;
        EXPECT_LT(drawn->getCentreX(), lastCopyX) << "it moves left into the original";
        EXPECT_LT(rig.motion().drawnRectFor(after)->getX(), lastAfterX) << "the columns after it close up as it goes";
        lastCopyX = drawn->getCentreX();
        lastAfterX = rig.motion().drawnRectFor(after)->getX();
    }

    rig.motion().applyAtMs(ExitEnterTimeline::kIntoSourceMs);
    EXPECT_NEAR(rig.motion().drawnRectFor(copy)->getCentreX(), originalX + pitch * 0.5f, 1.0f)
        << "it ends on the original";
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(after)->getX(), afterOld - pitch);
    EXPECT_FLOAT_EQ(rig.panelX(*rig.columns()[1]), afterOld - pitch) << "the real column is where the picture ends";

    // The copy's row on the timeline went the same way.
    EXPECT_TRUE(rig.mc.getTimelinePanel().getTrackListMotionForTest().isRunning());
}

TEST(MixerPanelColumnMotion, RedoPlaysTheColumnOutOfTheOriginalAgainWithNoOutline) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    rig.duplicateFirstTrack();
    const auto copy = rig.columns()[1]->getUuid();
    const auto after = rig.columns()[2]->getUuid();
    const float originalX = rig.panelX(*rig.columns()[0]);
    const float afterX = rig.panelX(*rig.columns()[2]);
    const float pitch = afterX - rig.panelX(*rig.columns()[1]);
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    rig.motion().finishNow();

    ASSERT_TRUE(rig.mc.getUndoManager().redo());

    ASSERT_EQ(rig.columns().size(), 4u);
    ASSERT_TRUE(rig.motion().isRunning());
    EXPECT_TRUE(rig.motion().timeline().together);
    EXPECT_FALSE(rig.motion().timeline().reverse) << "redo plays the forward motion again";
    EXPECT_DOUBLE_EQ(rig.motion().timeline().totalMs(), ExitEnterTimeline::kOutOfSourceMs);

    rig.motion().applyAtMs(0.0);
    ASSERT_TRUE(rig.motion().drawnRectFor(copy).has_value());
    EXPECT_NEAR(rig.motion().drawnRectFor(copy)->getCentreX(), originalX + pitch * 0.5f, 1.0f)
        << "it starts on the original";
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(after)->getX(), afterX - pitch);

    rig.motion().applyAtMs(ExitEnterTimeline::kOutOfSourceMs * 0.5);
    EXPECT_GT(rig.motion().drawnRectFor(copy)->getCentreX(), originalX + pitch * 0.5f);
    EXPECT_GT(rig.motion().drawnRectFor(after)->getX(), afterX - pitch);
    EXPECT_LT(rig.motion().drawnRectFor(after)->getX(), afterX);

    rig.motion().applyAtMs(ExitEnterTimeline::kOutOfSourceMs);
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(copy)->getX(), originalX + pitch);
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(after)->getX(), afterX);
    EXPECT_FLOAT_EQ(rig.motion().outlineAlphaFor(copy), 0.0f);
    EXPECT_EQ(rig.columns()[1]->getUuid(), copy) << "the real column is back, with its own title";
}

TEST(MixerPanelColumnMotion, DuplicatingATrackBringsItsColumnOutOfTheOriginalWhileTheColumnsAfterItMakeRoom) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    const auto first = rig.columns()[0]->getUuid();
    const float originalX = rig.panelX(*rig.columns()[0]);
    const float pitch = rig.panelX(*rig.columns()[1]) - originalX;
    const auto secondOld = rig.columns()[1]->getUuid();
    const float secondOldX = rig.panelX(*rig.columns()[1]);

    rig.duplicateFirstTrack();

    ASSERT_EQ(rig.columns().size(), 4u);
    const auto copy = rig.columns()[1]->getUuid();
    ASSERT_NE(copy, secondOld);
    ASSERT_NE(copy, first);
    ASSERT_TRUE(rig.motion().isRunning());
    EXPECT_TRUE(rig.motion().timeline().together);
    EXPECT_FALSE(rig.motion().timeline().reverse);
    EXPECT_FALSE(rig.motion().timeline().hasExit);
    EXPECT_DOUBLE_EQ(rig.motion().timeline().totalMs(), ExitEnterTimeline::kOutOfSourceMs);

    rig.motion().applyAtMs(0.0);
    ASSERT_TRUE(rig.motion().drawnRectFor(copy).has_value()) << "the copy is visible from the first moment";
    EXPECT_NEAR(rig.motion().drawnRectFor(copy)->getCentreX(), originalX + pitch * 0.5f, 1.0f)
        << "it starts on the original";
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(secondOld)->getX(), secondOldX)
        << "the column after it has not moved yet";

    float lastCopyX = rig.motion().drawnRectFor(copy)->getCentreX();
    float lastSecondX = secondOldX;
    for (const double fraction : {0.25, 0.5, 0.75}) {
        rig.motion().applyAtMs(ExitEnterTimeline::kOutOfSourceMs * fraction);
        ASSERT_TRUE(rig.motion().drawnRectFor(copy).has_value()) << fraction;
        EXPECT_GT(rig.motion().drawnRectFor(copy)->getCentreX(), lastCopyX) << "it keeps moving right";
        EXPECT_GT(rig.motion().drawnRectFor(secondOld)->getX(), lastSecondX)
            << "the column after it makes room as it goes";
        lastCopyX = rig.motion().drawnRectFor(copy)->getCentreX();
        lastSecondX = rig.motion().drawnRectFor(secondOld)->getX();
    }

    rig.motion().applyAtMs(ExitEnterTimeline::kOutOfSourceMs);
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(copy)->getX(), originalX + pitch);
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(copy)->getWidth(), pitch);
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(secondOld)->getX(), secondOldX + pitch) << "it made room";
    EXPECT_FLOAT_EQ(rig.motion().outlineAlphaFor(copy), 0.0f) << "no outline";

    // The copy's row on the timeline moves the same way.
    auto& rows = rig.mc.getTimelinePanel().getTrackListMotionForTest();
    EXPECT_TRUE(rows.isRunning());
    EXPECT_TRUE(rows.timeline().together);
    EXPECT_FALSE(rows.timeline().hasExit);
}

TEST(MixerPanelColumnMotion, AddingATrackGrowsItsColumnAndRowInAndAnUndoStillShrinksThemAway) {
    ReducedMotionGuard guard(false);
    Rig rig(true);

    rig.mc.simulateAddAudioTrackClick();

    ASSERT_EQ(rig.columns().size(), 4u);
    ASSERT_TRUE(rig.motion().isRunning());
    EXPECT_TRUE(rig.motion().timeline().hasEnter);
    EXPECT_FALSE(rig.motion().timeline().hasExit);
    EXPECT_TRUE(rig.mc.getTimelinePanel().getTrackListMotionForTest().timeline().hasEnter);

    rig.motion().finishNow();
    rig.mc.getTimelinePanel().getTrackListMotionForTest().finishNow();
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    EXPECT_TRUE(rig.motion().timeline().hasExit);
}

TEST(MixerPanelColumnMotion, AddingAMidiTrackGrowsItsRowIn) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    rig.mc.simulateAddMidiTrackClick();
    EXPECT_TRUE(rig.mc.getTimelinePanel().getTrackListMotionForTest().isRunning());
    EXPECT_TRUE(rig.mc.getTimelinePanel().getTrackListMotionForTest().timeline().hasEnter);
}

TEST(MixerPanelColumnMotion, OffScreenAnAddedOrDuplicatedTrackLandsAtOnceAndNothingStaysArmed) {
    ReducedMotionGuard guard(false);
    Rig rig(false);
    rig.mc.simulateAddAudioTrackClick();
    rig.duplicateFirstTrack();
    EXPECT_FALSE(rig.motion().isRunning());
    EXPECT_FALSE(rig.mc.getTimelinePanel().getTrackListMotionForTest().isRunning());

    rig.mixer().forceColumnMotionForTest(true);
    rig.mc.getTimelinePanel().forceTrackGlideForTest(true);
    rig.mixer().rebuild();
    EXPECT_FALSE(rig.motion().isRunning()) << "a rebuild nobody armed lands at once";
}

TEST(MixerPanelColumnMotion, ReduceMotionFadesTheDuplicatedColumnInPlaceOver80Ms) {
    ReducedMotionGuard guard(true);
    Rig rig(true);
    rig.duplicateFirstTrack();
    const float width = rig.panelX(*rig.columns()[2]) - rig.panelX(*rig.columns()[1]);
    const float slotX = rig.panelX(*rig.columns()[1]);
    const float afterX = rig.panelX(*rig.columns()[2]);
    const auto copy = rig.columns()[1]->getUuid();
    const auto after = rig.columns()[2]->getUuid();

    ASSERT_TRUE(rig.motion().isRunning());
    EXPECT_DOUBLE_EQ(rig.motion().timeline().totalMs(), ExitEnterTimeline::kTogetherFadeMs);
    rig.motion().applyAtMs(ExitEnterTimeline::kTogetherFadeMs * 0.5);
    ASSERT_TRUE(rig.motion().drawnRectFor(copy).has_value());
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(copy)->getX(), slotX) << "in its slot, not travelling";
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(copy)->getWidth(), width) << "a fade, not a grow";
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(after)->getX(), afterX) << "the columns after it stay put";
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

// ---------------------------------------------------------------------------------------------------------------------
// Every route that adds or removes a channel animates its column by itself, in every mixer instance (the panel notices
// the change in its own rebuild; no add path arms it).
// ---------------------------------------------------------------------------------------------------------------------
namespace {

using Action = std::function<void(Rig&)>;
struct NamedAction {
    const char* name;
    Action prepare; // runs first, its motion finished (may be empty)
    Action act;     // the step that makes one new column
};

std::vector<NamedAction> addRoutes() {
    using Panel = synth::ui::TimelinePanelComponent;
    return {
        {"Audio Track", nullptr, [](Rig& r) { r.mc.simulateAddAudioTrackClick(); }},
        {"Instrument Track (Oscillator)", nullptr,
         [](Rig& r) { r.mc.simulateAddInstrumentTrackClick(Panel::kAddInstrumentOscillatorMenuId); }},
        {"Duplicate Track", nullptr, [](Rig& r) { r.duplicateFirstTrack(); }},
        {"Add Bus", nullptr, [](Rig& r) { r.mixer().createBus(); }},
    };
}

} // namespace

TEST(MixerPanelColumnMotion, EveryRouteThatAddsAChannelGrowsItsColumnInAndUndoAndRedoPlayItBack) {
    ReducedMotionGuard guard(false);
    for (const auto& route : addRoutes()) {
        SCOPED_TRACE(route.name);
        Rig rig(true);
        if (route.prepare)
            route.prepare(rig);
        rig.motion().finishNow();
        const auto before = rig.columns().size();

        route.act(rig);

        ASSERT_EQ(rig.columns().size(), before + 1) << "the route adds one channel";
        ASSERT_TRUE(rig.motion().isRunning()) << "its column grows in";
        EXPECT_TRUE(rig.motion().timeline().hasEnter);
        EXPECT_FALSE(rig.motion().timeline().hasExit);

        rig.motion().finishNow();
        ASSERT_TRUE(rig.mc.getUndoManager().undo());
        ASSERT_EQ(rig.columns().size(), before);
        ASSERT_TRUE(rig.motion().isRunning()) << "the undo shrinks it away";
        EXPECT_TRUE(rig.motion().timeline().hasExit);

        rig.motion().finishNow();
        ASSERT_TRUE(rig.mc.getUndoManager().redo());
        ASSERT_EQ(rig.columns().size(), before + 1);
        ASSERT_TRUE(rig.motion().isRunning()) << "the redo grows it back";
        EXPECT_TRUE(rig.motion().timeline().hasEnter);
    }
}

TEST(MixerPanelColumnMotion, UndoingATrackDeleteGrowsItsColumnBackWithAnOutline) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    rig.choose(1, synth::ui::TimelineTrackHeaderComponent::kDeleteTrackMenuId);
    rig.motion().finishNow();
    const auto count = rig.columns().size();

    ASSERT_TRUE(rig.mc.getUndoManager().undo());

    ASSERT_EQ(rig.columns().size(), count + 1);
    ASSERT_TRUE(rig.motion().isRunning());
    EXPECT_TRUE(rig.motion().timeline().hasEnter);
    EXPECT_TRUE(rig.motion().timeline().outline);
}

TEST(MixerPanelColumnMotion, ALoadAndTheCatchUpAfterBeingHiddenLandAtOnce) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    rig.motion().finishNow();

    // A column made while the panel is hidden is not animated when the panel is shown again.
    rig.mixer().setVisible(false);
    rig.mc.simulateAddAudioTrackClick();
    rig.mixer().setVisible(true);
    EXPECT_FALSE(rig.motion().isRunning()) << "a catch-up on becoming visible lands at once";

    // A column that arrives with a replaced document (a project load) is not an add.
    auto& doc = rig.mc.getTimelineDoc();
    doc.fromVar(doc.toVar());
    rig.mixer().createBus();
    EXPECT_FALSE(rig.motion().isRunning()) << "a load lands at once";

    rig.mixer().createBus();
    EXPECT_TRUE(rig.motion().isRunning()) << "while the next add, with nothing replaced, animates";
}

TEST(MixerPanelColumnMotion, TheMirrorMixerAnimatesItsColumnsLikeTheDockedOne) {
    synth::test::PersistedKeysGuard keys(
        {"detachedPanelBothPlaces", "mixerMirrorWindowBounds", "bottomDockActiveTab", "bottomDockTabOrder"});
    {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::test::userSettingsTestOptions());
        props.getUserSettings()->setValue("detachedPanelBothPlaces", "both");
        props.getUserSettings()->saveIfNeeded();
    }
    ReducedMotionGuard guard(false);
    Rig rig(true);
    auto& dock = rig.mc.getBottomDock();
    dock.setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    dock.getDetachIcon(synth::ui::BottomDockComponent::Tab::Mixer)->onClick();
    auto* mirror = dock.getMixerMirrorPanelForTest();
    ASSERT_NE(mirror, nullptr);
    mirror->setSize(1400, 360);
    mirror->forceColumnMotionForTest(true);
    mirror->rebuild();
    mirror->getColumnMotionForTest().finishNow();
    const int before = mirror->getColumnCount();

    rig.mc.simulateAddAudioTrackClick();
    ASSERT_GT(mirror->getColumnCount(), before);
    EXPECT_TRUE(mirror->getColumnMotionForTest().isRunning()) << "the mirror grows the column in";
    EXPECT_TRUE(mirror->getColumnMotionForTest().timeline().hasEnter);

    mirror->getColumnMotionForTest().finishNow();
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    EXPECT_TRUE(mirror->getColumnMotionForTest().isRunning()) << "and shrinks it away on undo";
    EXPECT_TRUE(mirror->getColumnMotionForTest().timeline().hasExit);
}

// Regression test for FRO901: a rebuild that follows an add (the graph catch-up lays the panel out again) ended the
// grow-in at once, so the new column appeared with no motion.
TEST(MixerPanelColumnMotion, ARebuildAfterTheStartLeavesTheMotionPlayingButAResizeEndsIt) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    rig.mc.simulateAddAudioTrackClick();
    ASSERT_TRUE(rig.motion().isRunning());

    rig.mixer().rebuild();
    rig.mixer().resized();
    EXPECT_TRUE(rig.motion().isRunning()) << "the same geometry: the motion plays on";
    rig.motion().applyAtMs(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs * 0.5);
    EXPECT_TRUE(rig.motion().drawnRectFor(rig.columns().back()->getUuid()).has_value());

    rig.mixer().setSize(1200, 360);
    EXPECT_FALSE(rig.motion().isRunning()) << "its picture is of the old size";
}
