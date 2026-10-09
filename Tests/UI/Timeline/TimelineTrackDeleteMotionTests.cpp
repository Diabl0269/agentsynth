// TimelineTrackDeleteMotionTests.cpp: deleting a track shrinks its row (header and its whole lane line) away and the
// rows below close the gap; Cmd+Z makes room, grows it back and fades an outline around it
// (TimelinePanelTrackListMotion.cpp). The timeline is stepped by hand through the panel's ExitEnterListMotion; nothing
// here waits for a frame.

#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>

using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;
using synth::ui::ExitEnterTimeline;
using synth::ui::TrackHeaderHost;

namespace {

struct StubHost : TrackHeaderHost {
    std::vector<BindingOption> getAvailableTrackInNodes(TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
};

struct ReducedMotionGuard {
    explicit ReducedMotionGuard(bool reduced) { synth::ui::setReducedMotionForTest(reduced); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

juce::String keyOf(TrackId id) { return juce::String(static_cast<juce::int64>(id.value)); }

struct Fixture {
    TimelineDoc doc;
    StubHost host;
    AppUndoManager undo;
    synth::ui::TimelinePanelComponent panel;
    TrackId a, b, c;

    Fixture() {
        panel.setSize(1200, 420);
        panel.setTrackHeaderHost(&host);
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        undo.setTrackListHooks([this] { panel.noteTracksLeaving(); }, [this] { panel.finishTrackListChange(); });
        a = doc.addTrack(TrackKind::Midi, "A");
        b = doc.addTrack(TrackKind::Midi, "B");
        c = doc.addTrack(TrackKind::Midi, "C");
        undo.beginNewTransaction();
    }

    auto& motion() { return panel.getTrackListMotionForTest(); }
    int headerY(int index) { return panel.getTrackHeaderViewport().getY() + panel.getTrackHeaderAt(index)->getY(); }
    int rowHeight() { return panel.getTrackHeaderAt(0)->getHeight(); }

    // What Cmd+Backspace on a row does in the app: picture the rows, remove the track in one undo step, settle.
    void deleteTrackLikeTheApp(TrackId id) {
        panel.noteTracksLeaving();
        ASSERT_TRUE(undo.recordTimelineChange(doc, [this, id] { doc.removeTrack(id); }));
        panel.finishTrackListChange();
    }
};

} // namespace

TEST(TimelineTrackDeleteMotion, TheRowShrinksInPlaceThenTheRowsBelowCloseTheGap) {
    ReducedMotionGuard guard(false);
    Fixture f;
    f.panel.forceTrackGlideForTest(true);
    const int rowH = f.rowHeight();
    const int top = f.headerY(0);
    const int cOld = f.headerY(2);
    ASSERT_EQ(cOld, top + 2 * rowH);

    f.deleteTrackLikeTheApp(f.b);

    ASSERT_TRUE(f.motion().isRunning());
    EXPECT_EQ(f.panel.getTrackHeaderCount(), 2) << "the model changed at once";
    EXPECT_EQ(f.motion().exitGhostCount(), 1) << "the removed row's picture is on screen";
    EXPECT_EQ(f.motion().timeline().hasEnter, false);

    f.motion().applyAtMs(ExitEnterTimeline::kExitMs * 0.5);
    const auto shrinking = f.motion().drawnRectFor(keyOf(f.b));
    ASSERT_TRUE(shrinking.has_value());
    EXPECT_LT(shrinking->getHeight(), static_cast<float>(rowH));
    EXPECT_NEAR(shrinking->getCentreY(), static_cast<float>(top + rowH + rowH / 2), 1.0f) << "toward its centre";
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.c))->getY(), static_cast<float>(cOld))
        << "the row below has not moved while the exit runs";

    f.motion().applyAtMs(ExitEnterTimeline::kExitMs);
    EXPECT_EQ(f.motion().exitGhostCount(), 0);
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.c))->getY(), static_cast<float>(cOld)) << "the gap starts after";

    f.motion().applyAtMs(ExitEnterTimeline::kExitMs + ExitEnterTimeline::kGapMs * 0.5);
    const float mid = f.motion().drawnRectFor(keyOf(f.c))->getY();
    EXPECT_LT(mid, static_cast<float>(cOld));
    EXPECT_GT(mid, static_cast<float>(top + rowH));

    f.motion().applyAtMs(f.motion().timeline().totalMs());
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.c))->getY(), static_cast<float>(top + rowH));
    EXPECT_EQ(f.headerY(1), top + rowH) << "the real row is already where the picture ends";
    f.motion().finishNow();
    EXPECT_FALSE(f.motion().isRunning());
}

TEST(TimelineTrackDeleteMotion, ThePictureIsInertAndNeverTakesFocus) {
    ReducedMotionGuard guard(false);
    Fixture f;
    f.panel.forceTrackGlideForTest(true);
    f.deleteTrackLikeTheApp(f.b);
    auto* overlay = f.motion().overlayComponent();
    ASSERT_NE(overlay, nullptr);
    EXPECT_FALSE(overlay->getWantsKeyboardFocus());
    EXPECT_FALSE(overlay->isAccessible());
    bool onThis = true, onChildren = true;
    overlay->getInterceptsMouseClicks(onThis, onChildren);
    EXPECT_FALSE(onThis || onChildren);
    for (int i = 0; i < f.panel.getTrackHeaderCount(); ++i)
        EXPECT_TRUE(f.panel.getTrackHeaderAt(i)->isVisible()) << "the real rows stay in the tree under the picture";
}

TEST(TimelineTrackDeleteMotion, UndoMakesRoomThenGrowsTheRowBackThenFadesAnOutline) {
    ReducedMotionGuard guard(false);
    Fixture f;
    f.panel.forceTrackGlideForTest(true);
    const int rowH = f.rowHeight();
    const int top = f.headerY(0);
    f.deleteTrackLikeTheApp(f.b);
    f.motion().finishNow();

    ASSERT_TRUE(f.undo.undo());

    ASSERT_TRUE(f.motion().isRunning());
    EXPECT_EQ(f.panel.getTrackHeaderCount(), 3) << "the model came back at once";
    EXPECT_TRUE(f.motion().timeline().hasEnter);
    EXPECT_FALSE(f.motion().timeline().hasExit);
    EXPECT_EQ(f.panel.getTrackHeaderAt(1)->getTrackId(), f.b) << "the real row is back, with its own title and focus";
    EXPECT_FALSE(f.motion().drawnRectFor(keyOf(f.b)).has_value()) << "the gap opens first, empty";
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.c))->getY(), static_cast<float>(top + rowH))
        << "the row below starts where it stood without the track";

    f.motion().applyAtMs(ExitEnterTimeline::kGapMs);
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.c))->getY(), static_cast<float>(top + 2 * rowH));
    EXPECT_FALSE(f.motion().drawnRectFor(keyOf(f.b)).has_value()) << "nothing grows until the gap is open";

    f.motion().applyAtMs(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs * 0.5);
    const auto growing = f.motion().drawnRectFor(keyOf(f.b));
    ASSERT_TRUE(growing.has_value());
    EXPECT_LT(growing->getHeight(), static_cast<float>(rowH));
    EXPECT_FLOAT_EQ(f.motion().outlineAlphaFor(keyOf(f.b)), 0.0f);

    f.motion().applyAtMs(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs + ExitEnterTimeline::kOutlineMs * 0.5);
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.b))->getHeight(), static_cast<float>(rowH));
    EXPECT_NEAR(f.motion().outlineAlphaFor(keyOf(f.b)), 0.5f, 0.01f);

    f.motion().applyAtMs(f.motion().timeline().totalMs());
    EXPECT_FLOAT_EQ(f.motion().outlineAlphaFor(keyOf(f.b)), 0.0f);
}

TEST(TimelineTrackDeleteMotion, RedoOfADeleteExitsAgain) {
    ReducedMotionGuard guard(false);
    Fixture f;
    f.panel.forceTrackGlideForTest(true);
    f.deleteTrackLikeTheApp(f.b);
    f.motion().finishNow();
    ASSERT_TRUE(f.undo.undo());
    f.motion().finishNow();

    ASSERT_TRUE(f.undo.redo());

    ASSERT_TRUE(f.motion().isRunning());
    EXPECT_EQ(f.panel.getTrackHeaderCount(), 2);
    EXPECT_EQ(f.motion().exitGhostCount(), 1);
    EXPECT_TRUE(f.motion().timeline().hasExit);
}

TEST(TimelineTrackDeleteMotion, ReduceMotionFadesTheRowInPlace) {
    ReducedMotionGuard guard(true);
    Fixture f;
    f.panel.forceTrackGlideForTest(true);
    const int rowH = f.rowHeight();
    f.deleteTrackLikeTheApp(f.b);

    ASSERT_TRUE(f.motion().isRunning());
    EXPECT_TRUE(f.motion().isReducedMotion());
    f.motion().applyAtMs(ExitEnterTimeline::kExitMs * 0.5);
    const auto fading = f.motion().drawnRectFor(keyOf(f.b));
    ASSERT_TRUE(fading.has_value());
    EXPECT_FLOAT_EQ(fading->getHeight(), static_cast<float>(rowH)) << "a fade, not a shrink";
}

TEST(TimelineTrackDeleteMotion, OffScreenTheDeleteAndTheUndoLandAtOnce) {
    ReducedMotionGuard guard(false);
    Fixture f; // the panel is not showing and nothing forces the glide
    f.deleteTrackLikeTheApp(f.b);
    EXPECT_FALSE(f.motion().isRunning());
    EXPECT_EQ(f.panel.getTrackHeaderCount(), 2);
    ASSERT_TRUE(f.undo.undo());
    EXPECT_FALSE(f.motion().isRunning());
    EXPECT_EQ(f.panel.getTrackHeaderCount(), 3);
}

TEST(TimelineTrackDeleteMotion, ATrackAddedAnyOtherWayLandsAtOnce) {
    ReducedMotionGuard guard(false);
    Fixture f;
    f.panel.forceTrackGlideForTest(true);
    f.doc.addTrack(TrackKind::Midi, "D");
    f.panel.finishTrackListChange();
    EXPECT_FALSE(f.motion().isRunning());
}

TEST(TimelineTrackDeleteMotion, ANewDeleteLandsTheOneStillPlaying) {
    ReducedMotionGuard guard(false);
    Fixture f;
    f.panel.forceTrackGlideForTest(true);
    f.deleteTrackLikeTheApp(f.a);
    ASSERT_TRUE(f.motion().isRunning());
    f.motion().applyAtMs(ExitEnterTimeline::kExitMs * 0.5);
    f.deleteTrackLikeTheApp(f.b);
    ASSERT_TRUE(f.motion().isRunning());
    EXPECT_EQ(f.motion().exitGhostCount(), 1);
    EXPECT_FALSE(f.motion().drawnRectFor(keyOf(f.a)).has_value()) << "the first one is not in the second's picture";
}
