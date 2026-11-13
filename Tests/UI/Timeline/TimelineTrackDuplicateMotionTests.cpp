// TimelineTrackDuplicateMotionTests.cpp: a duplicated track's row comes out of the original row while the rows below
// make room in the same movement, and Cmd+Z sends it back into the original (TimelinePanelTrackListMotion.cpp,
// docs/layout/duplicate-motion.md). The motion is stepped by hand through the panel's ExitEnterListMotion; positions
// are asserted at several moments, not only the end (bug kind: motion-over-time).

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
        panel.setSize(1200, 520);
        panel.setTrackHeaderHost(&host);
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        undo.setTrackListHooks([this] { panel.noteTracksLeaving(); }, [this] { panel.finishTrackListChange(); });
        a = doc.addTrack(TrackKind::Midi, "A");
        b = doc.addTrack(TrackKind::Midi, "B");
        c = doc.addTrack(TrackKind::Midi, "C");
        undo.beginNewTransaction();
        panel.forceTrackGlideForTest(true);
    }

    auto& motion() { return panel.getTrackListMotionForTest(); }
    int headerY(int index) { return panel.getTrackHeaderViewport().getY() + panel.getTrackHeaderAt(index)->getY(); }
    int rowHeight() { return panel.getTrackHeaderAt(0)->getHeight(); }

    // What Cmd+D does in the app: arm, copy each source in one undo step, tell the panel where each came from, settle.
    std::vector<TrackId> duplicateLikeTheApp(const std::vector<TrackId>& sources) {
        std::vector<TrackId> copies;
        panel.armTrackInsertMotion();
        EXPECT_TRUE(undo.recordTimelineChange(doc, [&] {
            for (const auto source : sources)
                copies.push_back(doc.duplicateTrack(source, "copy", 0xff336699u, {}));
        }));
        for (size_t i = 0; i < copies.size(); ++i)
            panel.noteDuplicateOrigin(copies[i], sources[i]);
        panel.finishTrackListChange();
        return copies;
    }
};

} // namespace

TEST(TimelineTrackDuplicateMotion, TheCopyComesOutOfTheOriginalWhileTheRowsBelowMakeRoom) {
    ReducedMotionGuard guard(false);
    Fixture f;
    const int rowH = f.rowHeight();
    const int top = f.headerY(0);
    const int cOld = f.headerY(2);

    const auto copy = f.duplicateLikeTheApp({f.a}).front();

    ASSERT_TRUE(f.motion().isRunning());
    EXPECT_EQ(f.panel.getTrackHeaderCount(), 4) << "the model changed at once";
    EXPECT_TRUE(f.motion().timeline().together);
    EXPECT_DOUBLE_EQ(f.motion().timeline().totalMs(), ExitEnterTimeline::kOutOfSourceMs);

    f.motion().applyAtMs(0.0);
    auto drawn = f.motion().drawnRectFor(keyOf(copy));
    ASSERT_TRUE(drawn.has_value());
    EXPECT_NEAR(drawn->getCentreY(), static_cast<float>(top) + static_cast<float>(rowH) * 0.5f, 0.5f)
        << "the copy starts on the original row";
    EXPECT_LT(drawn->getHeight(), static_cast<float>(rowH));
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.b))->getY(), static_cast<float>(top + rowH));
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.c))->getY(), static_cast<float>(cOld));

    float lastCopy = drawn->getCentreY();
    float lastB = f.motion().drawnRectFor(keyOf(f.b))->getY();
    for (const double fraction : {0.25, 0.5, 0.75}) {
        f.motion().applyAtMs(ExitEnterTimeline::kOutOfSourceMs * fraction);
        drawn = f.motion().drawnRectFor(keyOf(copy));
        ASSERT_TRUE(drawn.has_value()) << fraction;
        const float copyTravel = (drawn->getCentreY() - static_cast<float>(top) - static_cast<float>(rowH) * 0.5f) /
                                 static_cast<float>(rowH);
        const float bTravel =
            (f.motion().drawnRectFor(keyOf(f.b))->getY() - static_cast<float>(top + rowH)) / static_cast<float>(rowH);
        EXPECT_NEAR(copyTravel, bTravel, 0.01f) << "the copy and the rows below move in the same span";
        EXPECT_GT(drawn->getCentreY(), lastCopy) << "it keeps moving";
        EXPECT_GT(f.motion().drawnRectFor(keyOf(f.b))->getY(), lastB);
        EXPECT_GT(copyTravel, 0.0f);
        EXPECT_LT(copyTravel, 1.0f);
        lastCopy = drawn->getCentreY();
        lastB = f.motion().drawnRectFor(keyOf(f.b))->getY();
    }

    f.motion().applyAtMs(ExitEnterTimeline::kOutOfSourceMs);
    drawn = f.motion().drawnRectFor(keyOf(copy));
    EXPECT_FLOAT_EQ(drawn->getY(), static_cast<float>(top + rowH)) << "settled in its slot under the original";
    EXPECT_FLOAT_EQ(drawn->getHeight(), static_cast<float>(rowH));
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.b))->getY(), static_cast<float>(top + 2 * rowH));
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.c))->getY(), static_cast<float>(top + 3 * rowH));
    EXPECT_EQ(f.headerY(1), top + rowH) << "the real row is already where the picture ends";
    EXPECT_FLOAT_EQ(f.motion().outlineAlphaFor(keyOf(copy)), 0.0f);
}

TEST(TimelineTrackDuplicateMotion, UndoSendsTheCopyBackIntoTheOriginalAndRedoPlaysForwardAgain) {
    ReducedMotionGuard guard(false);
    Fixture f;
    const int rowH = f.rowHeight();
    const int top = f.headerY(0);
    const auto copy = f.duplicateLikeTheApp({f.a}).front();
    f.motion().finishNow();

    ASSERT_TRUE(f.undo.undo());
    ASSERT_TRUE(f.motion().isRunning());
    EXPECT_EQ(f.panel.getTrackHeaderCount(), 3);
    EXPECT_TRUE(f.motion().timeline().together);
    EXPECT_TRUE(f.motion().timeline().reverse);
    EXPECT_DOUBLE_EQ(f.motion().timeline().totalMs(), ExitEnterTimeline::kIntoSourceMs);

    f.motion().applyAtMs(0.0);
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(copy))->getY(), static_cast<float>(top + rowH))
        << "starts in its slot";
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.b))->getY(), static_cast<float>(top + 2 * rowH));
    float last = f.motion().drawnRectFor(keyOf(copy))->getCentreY();
    for (const double fraction : {0.5, 0.75}) {
        f.motion().applyAtMs(ExitEnterTimeline::kIntoSourceMs * fraction);
        const auto drawn = f.motion().drawnRectFor(keyOf(copy));
        ASSERT_TRUE(drawn.has_value());
        EXPECT_LT(drawn->getCentreY(), last) << "it moves up into the original";
        last = drawn->getCentreY();
        EXPECT_LT(f.motion().drawnRectFor(keyOf(f.b))->getY(), static_cast<float>(top + 2 * rowH));
    }
    f.motion().applyAtMs(ExitEnterTimeline::kIntoSourceMs);
    EXPECT_NEAR(f.motion().drawnRectFor(keyOf(copy))->getCentreY(), static_cast<float>(top) + rowH * 0.5f, 1.0f)
        << "it ends on the original";
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.b))->getY(), static_cast<float>(top + rowH));
    f.motion().finishNow();

    ASSERT_TRUE(f.undo.redo());
    ASSERT_TRUE(f.motion().isRunning());
    EXPECT_TRUE(f.motion().timeline().together);
    EXPECT_FALSE(f.motion().timeline().reverse) << "redo plays the forward motion again";
    f.motion().applyAtMs(0.0);
    EXPECT_NEAR(f.motion().drawnRectFor(keyOf(copy))->getCentreY(), static_cast<float>(top) + rowH * 0.5f, 1.0f);
    EXPECT_FLOAT_EQ(f.motion().outlineAlphaFor(keyOf(copy)), 0.0f);
}

TEST(TimelineTrackDuplicateMotion, SeveralCopiesEachComeOutOfTheirOwnOriginal) {
    ReducedMotionGuard guard(false);
    Fixture f;
    const int rowH = f.rowHeight();
    const int top = f.headerY(0);

    const auto copies = f.duplicateLikeTheApp({f.a, f.b});
    ASSERT_EQ(copies.size(), 2u);
    ASSERT_TRUE(f.motion().isRunning());
    f.motion().applyAtMs(0.0);
    // Rows are now A, A copy, B, B copy, C. Each copy starts on its own original.
    const float centreOfA = static_cast<float>(top) + static_cast<float>(rowH) * 0.5f;
    EXPECT_NEAR(f.motion().drawnRectFor(keyOf(copies[0]))->getCentreY(), centreOfA, 0.5f);
    EXPECT_NEAR(f.motion().drawnRectFor(keyOf(copies[1]))->getCentreY(), centreOfA + static_cast<float>(rowH), 0.5f)
        << "B's copy starts on B, which stands below A";
    f.motion().applyAtMs(ExitEnterTimeline::kOutOfSourceMs);
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(copies[0]))->getY(), static_cast<float>(top + rowH));
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(copies[1]))->getY(), static_cast<float>(top + 3 * rowH));
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.c))->getY(), static_cast<float>(top + 4 * rowH));
}

TEST(TimelineTrackDuplicateMotion, ReduceMotionPutsTheRowsInPlaceAndFadesTheCopyOver80Ms) {
    ReducedMotionGuard guard(true);
    Fixture f;
    const int rowH = f.rowHeight();
    const int top = f.headerY(0);
    const auto copy = f.duplicateLikeTheApp({f.a}).front();

    ASSERT_TRUE(f.motion().isRunning());
    EXPECT_DOUBLE_EQ(f.motion().timeline().totalMs(), ExitEnterTimeline::kTogetherFadeMs);
    f.motion().applyAtMs(ExitEnterTimeline::kTogetherFadeMs * 0.5);
    EXPECT_FLOAT_EQ(f.motion().drawnRectFor(keyOf(f.b))->getY(), static_cast<float>(top + 2 * rowH))
        << "nothing slides";
    const auto drawn = f.motion().drawnRectFor(keyOf(copy));
    ASSERT_TRUE(drawn.has_value());
    EXPECT_FLOAT_EQ(drawn->getY(), static_cast<float>(top + rowH));
    EXPECT_FLOAT_EQ(drawn->getHeight(), static_cast<float>(rowH));
}

TEST(TimelineTrackDuplicateMotion, OffScreenTheDuplicateAndItsUndoLandAtOnce) {
    ReducedMotionGuard guard(false);
    Fixture f;
    f.panel.forceTrackGlideForTest(false);
    f.duplicateLikeTheApp({f.a});
    EXPECT_FALSE(f.motion().isRunning());
    EXPECT_EQ(f.panel.getTrackHeaderCount(), 4);
    ASSERT_TRUE(f.undo.undo());
    EXPECT_FALSE(f.motion().isRunning());
    EXPECT_EQ(f.panel.getTrackHeaderCount(), 3);
}

TEST(TimelineTrackDuplicateMotion, DeletingTheCopyByHandStillShrinksItAndItsUndoGrowsItBackInPlace) {
    ReducedMotionGuard guard(false);
    Fixture f;
    const auto copy = f.duplicateLikeTheApp({f.a}).front();
    f.motion().finishNow();

    f.panel.noteTracksLeaving();
    ASSERT_TRUE(f.undo.recordTimelineChange(f.doc, [&] { f.doc.removeTrack(copy); }));
    f.panel.finishTrackListChange();
    ASSERT_TRUE(f.motion().isRunning());
    EXPECT_FALSE(f.motion().timeline().together) << "a hand delete is the plain shrink, not 'back into the original'";
    EXPECT_TRUE(f.motion().timeline().hasExit);
    f.motion().finishNow();

    ASSERT_TRUE(f.undo.undo());
    ASSERT_TRUE(f.motion().isRunning());
    EXPECT_FALSE(f.motion().timeline().together) << "its undo grows it back where it stood, with the outline";
    EXPECT_TRUE(f.motion().timeline().hasEnter);
    EXPECT_TRUE(f.motion().timeline().outline);
}
