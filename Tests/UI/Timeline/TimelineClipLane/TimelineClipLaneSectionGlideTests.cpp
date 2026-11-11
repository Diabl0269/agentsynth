// TimelineClipLaneSectionGlideTests.cpp -- a section move, place or delete and a repeat stamp glide the clips they
// change instead of popping them: moved clips glide from where they were, new copies grow in from their left edge,
// removed clips shrink toward their centre (docs/layout/undo-glide.md "Points, dots and clips"). The lane is
// refreshed on every doc notification, the way the timeline panel does it, so the section edit's own steps reach it.
#include "TimelineClipLaneTestFixture.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/TimelineClipLaneArea/ClipLaneEnterExit.h"

using synth::PlacementId;
using synth::SectionDropMode;
using synth::SectionId;
using synth::TrackId;
using synth::ui::AnimationMode;

namespace {

constexpr double kSpb = 0.5;

struct ModeGuard {
    explicit ModeGuard(AnimationMode mode) { synth::ui::setAnimationMode(mode); }
    ~ModeGuard() { synth::ui::setAnimationMode(AnimationMode::followSystem); }
};

// Track A: c0 [0,4) and c1 [4,8) in the Verse, tail [8,12) after it. Track B: bass [0,8) in the Verse.
struct SectionRig
    : ClipLaneFixture
    , TimelineDoc::Listener {
    TrackId a, b;
    ClipId c0, c1, tail, bass;
    SectionId verse;
    PlacementId first;

    SectionRig() {
        a = doc.addTrack(TrackKind::Midi, "A");
        b = doc.addTrack(TrackKind::Midi, "B");
        c0 = doc.addClip(a, 0.0, 4.0, "c0");
        doc.addNote(c0, makeNote(0.0, 60));
        c1 = doc.addClip(a, 4.0, 4.0, "c1");
        tail = doc.addClip(a, 8.0, 4.0, "tail");
        bass = doc.addClip(b, 0.0, 8.0, "bass");
        verse = doc.makeSection(0.0, 8.0, "Verse", 0, kSpb);
        first = doc.getPlacements().front().id;
        lane.refreshFromDoc();
        doc.addListener(this);
        lane.getClipGlideForTest().forceAnimateForTest(true);
        lane.getEnterExitForTest().forceAnimateForTest(true);
    }
    ~SectionRig() override { doc.removeListener(this); }
    void timelineChanged(const TimelineDoc&) override { lane.refreshFromDoc(); }

    juce::Rectangle<int> drawn(ClipId id) const { return lane.getDrawnClipRectForTest(id); }
    juce::Rectangle<int> resting(ClipId id) const { return lane.getClipRect(id); }
    synth::ui::KeyedRectTween& glide() { return lane.getClipGlideForTest(); }
    synth::ui::ClipLaneEnterExit& exits() { return lane.getEnterExitForTest(); }
    void paintFrame() {
        juce::Image frame(juce::Image::ARGB, lane.getWidth(), lane.getHeight(), true);
        juce::Graphics g(frame);
        lane.paintEntireComponent(g, false);
    }
    std::vector<ClipId> clipsOn(TrackId track, double from, double to) const {
        std::vector<ClipId> out;
        for (const auto& clip : doc.getTrack(track)->clips)
            if (clip.startBeat >= from && clip.startBeat < to)
                out.push_back(clip.id);
        return out;
    }
};

} // namespace

// Regression test for FRO1619: a section move deleted and re-stamped its clips, so they vanished and popped in.
TEST(TimelineClipLaneSectionGlide, AForwardSectionMoveGlidesItsClipsFromWhereTheyWereAndSettles) {
    ModeGuard full(AnimationMode::full);
    SectionRig r;
    const auto from = r.drawn(r.c0);
    ASSERT_TRUE(r.doc.movePlacement(r.first, 12.0, SectionDropMode::MakeRoom, kSpb));
    ASSERT_NE(r.doc.getClip(r.c0), nullptr) << "the moved clip keeps its id";
    const auto to = r.resting(r.c0);
    ASSERT_GT(to.getX(), from.getX() + 4);
    ASSERT_TRUE(r.glide().isAnimating());
    EXPECT_EQ(r.drawn(r.c0).getX(), from.getX()) << "the first frame is where the clip was";
    EXPECT_TRUE(r.exits().ghosts().empty()) << "nothing leaves: the same clips moved";
    r.glide().stepForTest(80.0);
    EXPECT_GT(r.drawn(r.c0).getX(), from.getX());
    EXPECT_LT(r.drawn(r.c0).getX(), to.getX());
    EXPECT_NE(r.drawn(r.tail), r.resting(r.tail)) << "the clip the move pushed glides too";
    r.paintFrame();
    r.glide().stepForTest(600.0);
    EXPECT_FALSE(r.glide().isAnimating());
    EXPECT_EQ(r.drawn(r.c0), to);
}

TEST(TimelineClipLaneSectionGlide, ARepeatStampGlidesOnlyTheStampedClipsNotTheOneBeingEdited) {
    ModeGuard full(AnimationMode::full);
    SectionRig r;
    ASSERT_TRUE(r.doc.placeSection(r.verse, 8.0, SectionDropMode::MakeRoom, kSpb).isValid());
    r.glide().stepForTest(600.0);
    r.exits().stepForTest(600.0);
    const auto copy = r.clipsOn(r.a, 8.0, 12.0);
    ASSERT_EQ(copy.size(), 1u);
    const int wide = r.drawn(r.c0).getWidth();

    ASSERT_TRUE(r.doc.resizeClip(copy.front(), 2.0));
    ASSERT_TRUE(r.glide().isAnimating());
    EXPECT_EQ(r.drawn(copy.front()), r.resting(copy.front())) << "the edited clip lands at once";
    ASSERT_NE(r.doc.getClip(r.c0), nullptr);
    EXPECT_LT(r.resting(r.c0).getWidth(), wide);
    EXPECT_EQ(r.drawn(r.c0).getWidth(), wide) << "the other copy's clip glides to its new length";
    EXPECT_EQ(r.drawn(r.bass), r.resting(r.bass)) << "a clip the stamp left alone stays";
    r.glide().stepForTest(600.0);
    EXPECT_EQ(r.drawn(r.c0), r.resting(r.c0));
}

// Under Reduce Motion the other repeats' clips take their new length at once and fade in; nothing travels.
TEST(TimelineClipLaneSectionGlide, UnderReduceMotionARepeatStampLandsAndFadesTheOtherRepeats) {
    ModeGuard reduced(AnimationMode::reduced);
    SectionRig r;
    ASSERT_TRUE(r.doc.placeSection(r.verse, 8.0, SectionDropMode::MakeRoom, kSpb).isValid());
    r.glide().stepForTest(600.0);
    r.exits().stepForTest(600.0);
    const auto copy = r.clipsOn(r.a, 8.0, 12.0);
    ASSERT_EQ(copy.size(), 1u);

    ASSERT_TRUE(r.doc.resizeClip(copy.front(), 2.0));
    EXPECT_EQ(r.drawn(r.c0), r.resting(r.c0)) << "no travel under Reduce Motion: the new length at once";
    EXPECT_TRUE(r.glide().isFading()) << "the restamped repeat fades in";
    EXPECT_TRUE(r.exits().ghosts().empty()) << "the same clip, so nothing leaves";
    r.glide().stepForTest(600.0);
    EXPECT_FALSE(r.glide().isAnimating());
}

TEST(TimelineClipLaneSectionGlide, APlacedCopyGrowsInFromItsLeftEdgeAndADeletedPlacementsClipsShrinkAway) {
    ModeGuard full(AnimationMode::full);
    SectionRig r;
    const auto placed = r.doc.placeSection(r.verse, 12.0, SectionDropMode::MakeRoom, kSpb);
    ASSERT_TRUE(placed.isValid());
    const auto copies = r.clipsOn(r.a, 12.0, 20.0);
    ASSERT_EQ(copies.size(), 2u);
    const auto rest = r.resting(copies.front());
    EXPECT_LE(r.drawn(copies.front()).getWidth(), 1) << "a new copy starts with no width";
    EXPECT_EQ(r.drawn(copies.front()).getX(), rest.getX()) << "at its own left edge";
    r.glide().stepForTest(80.0);
    EXPECT_GT(r.drawn(copies.front()).getWidth(), 1);
    EXPECT_LT(r.drawn(copies.front()).getWidth(), rest.getWidth());
    r.glide().stepForTest(600.0);
    EXPECT_EQ(r.drawn(copies.front()), rest);

    ASSERT_TRUE(r.doc.deletePlacementAndBars(placed, kSpb));
    ASSERT_TRUE(r.exits().isExitingForTest());
    EXPECT_EQ(r.exits().ghosts().size(), 3u) << "both copies on A and the bass copy on B";
    EXPECT_FLOAT_EQ(r.exits().scale(), 1.0f);
    r.exits().stepForTest(90.0);
    EXPECT_LT(r.exits().scale(), 1.0f);
    EXPECT_GT(r.exits().scale(), 0.0f);
    EXPECT_FLOAT_EQ(r.exits().alpha(), 1.0f) << "full motion shrinks, it does not fade";
    r.paintFrame();
    r.exits().stepForTest(400.0);
    EXPECT_FALSE(r.exits().isExitingForTest());
    EXPECT_TRUE(r.exits().ghosts().empty());
}

TEST(TimelineClipLaneSectionGlide, UnderReduceMotionAMoveLandsAndFadesAndADeleteFadesItsClipsOut) {
    ModeGuard reduced(AnimationMode::reduced);
    SectionRig r;
    ASSERT_TRUE(r.doc.movePlacement(r.first, 12.0, SectionDropMode::MakeRoom, kSpb));
    EXPECT_EQ(r.drawn(r.c0), r.resting(r.c0)) << "no travel under Reduce Motion";
    EXPECT_TRUE(r.glide().isFading());
    r.glide().stepForTest(600.0);

    ASSERT_TRUE(r.doc.deletePlacementAndBars(r.first, kSpb));
    ASSERT_TRUE(r.exits().isExitingForTest());
    r.exits().stepForTest(40.0);
    EXPECT_FLOAT_EQ(r.exits().scale(), 1.0f) << "the picture does not shrink";
    EXPECT_LT(r.exits().alpha(), 1.0f) << "it fades";
    r.exits().stepForTest(200.0);
    EXPECT_FALSE(r.exits().isExitingForTest()) << "the fade is short";
}

TEST(TimelineClipLaneSectionGlide, WithAnimationsOffEverySectionEditLandsAtOnce) {
    ModeGuard off(AnimationMode::off);
    SectionRig r;
    ASSERT_TRUE(r.doc.movePlacement(r.first, 12.0, SectionDropMode::MakeRoom, kSpb));
    EXPECT_FALSE(r.glide().isAnimating());
    EXPECT_EQ(r.drawn(r.c0), r.resting(r.c0));
    ASSERT_TRUE(r.doc.placeSection(r.verse, 0.0, SectionDropMode::MakeRoom, kSpb).isValid());
    EXPECT_FALSE(r.glide().isAnimating());
    ASSERT_TRUE(r.doc.deletePlacementAndBars(r.doc.getPlacements().back().id, kSpb));
    EXPECT_FALSE(r.exits().isExitingForTest());
    EXPECT_TRUE(r.exits().ghosts().empty());
}

TEST(TimelineClipLaneSectionGlide, ALoadOrAReplaceWithNoSurvivingClipGrowsAndShrinksNothing) {
    ModeGuard full(AnimationMode::full);
    SectionRig r;
    TimelineDoc other;
    const auto track = other.addTrack(TrackKind::Midi, "Other");
    other.addClip(track, 2.0, 4.0, "x");
    other.makeSection(0.0, 8.0, "Intro", 0, kSpb);
    ASSERT_TRUE(r.doc.fromVar(other.toVar()));
    r.lane.refreshFromDoc();
    EXPECT_FALSE(r.glide().isAnimating()) << "a new song is not an edit";
    EXPECT_TRUE(r.exits().ghosts().empty());

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(r.doc.toVar()));
    r.lane.setTimelineDoc(&loaded);
    EXPECT_FALSE(r.glide().isAnimating());
    EXPECT_TRUE(r.exits().ghosts().empty());
    r.lane.setTimelineDoc(&r.doc);
}

TEST(TimelineClipLaneSectionGlide, AnOrdinaryClipEditStillLandsAtOnce) {
    ModeGuard full(AnimationMode::full);
    SectionRig r;
    ASSERT_TRUE(r.doc.moveClip(r.tail, 10.0));
    EXPECT_FALSE(r.glide().isAnimating());
    ASSERT_TRUE(r.doc.resizeClip(r.c1, 2.0)); // inside the Verse, but it has no other copy to stamp
    EXPECT_FALSE(r.glide().isAnimating());
    EXPECT_TRUE(r.exits().ghosts().empty());
    r.doc.insertTime(0.0, 4.0, kSpb);
    EXPECT_FALSE(r.glide().isAnimating()) << "inserting bars is not a section edit";
}

// Regression test for FRO1532: an undo or redo that left no clip standing was taken for a load, so redoing the delete
// of the only clip's section popped the clip away and undoing it popped the clip back.
TEST(TimelineClipLaneSectionGlide, UndoAndRedoOfDeletingTheOnlyClipsSectionGrowAndShrinkIt) {
    ModeGuard full(AnimationMode::full);
    struct OneClip
        : ClipLaneFixture
        , TimelineDoc::Listener {
        ClipId only;
        OneClip() {
            const auto track = doc.addTrack(TrackKind::Midi, "A");
            only = doc.addClip(track, 0.0, 4.0, "only");
            doc.makeSection(0.0, 4.0, "Intro", 0, kSpb);
            doc.makeSection(4.0, 8.0, "Verse", 0, kSpb);
            lane.refreshFromDoc();
            doc.addListener(this);
            lane.getClipGlideForTest().forceAnimateForTest(true);
            lane.getEnterExitForTest().forceAnimateForTest(true);
        }
        ~OneClip() override { doc.removeListener(this); }
        void timelineChanged(const TimelineDoc&) override { lane.refreshFromDoc(); }
    } r;
    const auto intro = r.doc.getPlacements().front().id;
    r.undo.recordTimelineChange(r.doc, [&] { r.doc.deletePlacementAndBars(intro, kSpb); });
    ASSERT_EQ(r.doc.getClip(r.only), nullptr);
    ASSERT_TRUE(r.lane.getEnterExitForTest().isExitingForTest()) << "the delete itself shrinks the clip away";
    r.lane.getEnterExitForTest().stepForTest(600.0);

    ASSERT_TRUE(r.undo.undo());
    ASSERT_NE(r.doc.getClip(r.only), nullptr);
    ASSERT_TRUE(r.lane.getClipGlideForTest().isAnimating()) << "undo grows the clip back in";
    EXPECT_LE(r.lane.getDrawnClipRectForTest(r.only).getWidth(), 1);
    r.lane.getClipGlideForTest().stepForTest(600.0);
    EXPECT_EQ(r.lane.getDrawnClipRectForTest(r.only), r.lane.getClipRect(r.only));

    ASSERT_TRUE(r.undo.redo());
    ASSERT_EQ(r.doc.getClip(r.only), nullptr);
    ASSERT_TRUE(r.lane.getEnterExitForTest().isExitingForTest()) << "redo shrinks it away again, not a pop";
    EXPECT_EQ(r.lane.getEnterExitForTest().ghosts().size(), 1u);
}
