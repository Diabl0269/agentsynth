// PianoRoll velocity-strip slide and header group tests: the strip slides in and out like the scale
// panel (the progress seam stands in for the VBlank frames, the same idiom as
// PianoRollScaleAssistTests.cpp), and the Velocity chip, Humanize chip and value box sit inside one
// captioned frame without disturbing the hit-testing of the chips.

#include "PianoRollVelocityTestHelpers.h"

#include "UI/PianoRoll/VelocityLane/VelocityLaneSlide.h"
#include <cmath>

using synth::ui::VelocityLaneSlide;

TEST(PianoRollVelocitySlideTest, TogglingWhileNotShowingLandsAtOnce) {
    VelocityLaneFixture f;
    ASSERT_FALSE(f.roll.isShowing()) << "test premise: headless, no real window";
    const int full = f.lane().getHeight();
    ASSERT_GT(full, 0);

    f.roll.toggleVelocityLane();
    EXPECT_FALSE(f.roll.velocityLaneSlideForTest().isRunning());
    EXPECT_FLOAT_EQ(f.roll.velocityLaneSlideForTest().progress(), 0.0f);
    EXPECT_FALSE(f.roll.isVelocityLaneVisible());
    EXPECT_FALSE(f.lane().isVisible());
    EXPECT_EQ(f.roll.canvasBottom(), f.roll.getHeight());

    f.roll.toggleVelocityLane();
    EXPECT_FLOAT_EQ(f.roll.velocityLaneSlideForTest().progress(), 1.0f);
    EXPECT_TRUE(f.lane().isVisible());
    EXPECT_EQ(f.roll.canvasBottom(), f.roll.getHeight() - full);
}

TEST(PianoRollVelocitySlideTest, TheGridGivesUpRoundOfProgressTimesFullAndTheLaneKeepsItsFullHeight) {
    VelocityLaneFixture f;
    const int full = f.lane().getHeight();
    for (const float p : {1.0f, 0.75f, 0.5f, 0.3f, 0.1f}) {
        f.roll.setVelocityLaneSlideProgressForTest(p);
        const int expectedBottom = f.roll.getHeight() - (int)std::lround(p * full);
        EXPECT_EQ(f.roll.canvasBottom(), expectedBottom) << p;
        EXPECT_EQ(f.lane().getHeight(), full) << "never squashed, " << p;
        EXPECT_EQ(f.lane().getY(), expectedBottom) << "top sits at the grid's bottom, " << p;
        EXPECT_TRUE(f.lane().isVisible()) << p;
    }
    EXPECT_EQ(VelocityLaneSlide::revealedHeight(0.5f, 64), 32);
    EXPECT_EQ(VelocityLaneSlide::revealedHeight(2.0f, 64), 64);
}

TEST(PianoRollVelocitySlideTest, AClosingStripStaysVisibleUntilItReachesZero) {
    VelocityLaneFixture f;
    f.roll.toggleVelocityLane(); // closed, progress 0
    f.roll.setVelocityLaneSlideProgressForTest(0.5f);
    EXPECT_TRUE(f.lane().isVisible()) << "mid-slide the strip is still on screen";
    f.roll.setVelocityLaneSlideProgressForTest(0.0f);
    EXPECT_FALSE(f.lane().isVisible());
}

TEST(PianoRollVelocitySlideTest, TogglingMidFlightRetargetsFromTheCurrentProgress) {
    VelocityLaneFixture f;
    f.roll.setVelocityLaneSlideProgressForTest(0.6f);
    f.roll.toggleVelocityLane(); // close from 0.6
    EXPECT_FLOAT_EQ(f.roll.velocityLaneSlideForTest().animFrom(), 0.6f);
    EXPECT_FLOAT_EQ(f.roll.velocityLaneSlideForTest().animTo(), 0.0f);

    f.roll.setVelocityLaneSlideProgressForTest(0.3f);
    f.roll.toggleVelocityLane(); // reopen from 0.3
    EXPECT_FLOAT_EQ(f.roll.velocityLaneSlideForTest().animFrom(), 0.3f);
    EXPECT_FLOAT_EQ(f.roll.velocityLaneSlideForTest().animTo(), 1.0f);
}

TEST(PianoRollVelocitySlideTest, RestoringFromAPropertiesFileDoesNotAnimate) {
    auto props = makeScaleAssistTestProps("PianoRollVelocitySlideRestore");
    props->setValue("pianoRollVelocityLaneVisible", false);
    VelocityLaneFixture f;
    f.roll.setPropertiesFile(props.get());
    EXPECT_FALSE(f.roll.velocityLaneSlideForTest().isRunning());
    EXPECT_FLOAT_EQ(f.roll.velocityLaneSlideForTest().progress(), 0.0f);
    EXPECT_EQ(f.roll.canvasBottom(), f.roll.getHeight());
    props->getFile().deleteFile();
}

TEST(PianoRollVelocitySlideTest, HidingCancelsAGestureInFlight) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100});
    f.lane().mouseDown(leftClick(f.lane(), f.at(1.0, 100)));
    ASSERT_TRUE(f.lane().isGestureActive());
    f.roll.toggleVelocityLane();
    EXPECT_FALSE(f.lane().isGestureActive());
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 100);
}

// ============================================================================
// Group frame
// ============================================================================

TEST(PianoRollVelocityGroupTest, TheFrameEnclosesTheChipsTheCaptionAndTheBoxInOrder) {
    VelocityLaneFixture f;
    const auto group = f.roll.getVelocityGroupBounds();
    const auto velocity = f.roll.getVelocityChipBounds();
    const auto humanize = f.roll.getHumanizeChipBounds();
    const auto caption = f.roll.getVelocityCaptionBounds();
    const auto box = f.roll.getVelocityValueBox().getBounds();
    for (const auto& r : {velocity, humanize, caption, box}) {
        EXPECT_FALSE(r.isEmpty());
        EXPECT_TRUE(group.contains(r)) << r.toString() << " in " << group.toString();
    }
    EXPECT_LE(velocity.getRight(), humanize.getX());
    EXPECT_LE(humanize.getRight(), caption.getX());
    EXPECT_LE(caption.getRight(), box.getX()) << "\"Set\" sits immediately before the box";
    EXPECT_LE(box.getX() - caption.getRight(), 2);
    EXPECT_GE(velocity.getX() - group.getX(), 2) << "inner padding";
    EXPECT_GE(group.getRight() - box.getRight(), 2);
    EXPECT_GE(velocity.getY() - group.getY(), 2);
    EXPECT_LE(group.getBottom(), 19) << "clear of the toolbar's bottom rule";
}

TEST(PianoRollVelocityGroupTest, TheFrameOverlapsNoOtherHeaderControl) {
    VelocityLaneFixture f;
    const auto group = f.roll.getVelocityGroupBounds();
    for (const auto& other :
         {f.roll.getBackButtonBounds(), f.roll.getQuantiseButtonBounds(), f.roll.getQuantiseLengthButtonBounds(),
          f.roll.getQuantisePitchButtonBounds(), f.roll.getScaleButtonBounds(), f.roll.getScaleFilterButtonBounds()})
        EXPECT_FALSE(group.intersects(other)) << other.toString();
    EXPECT_LE(group.getRight(), f.roll.getWidth());
}

// Humanize is checked by hover, not a click: a click opens a real PopupMenu, which crashes on a
// display-less Linux runner while it looks for a screen to place itself on.
TEST(PianoRollVelocityGroupTest, TheCaptionIsNotAHitTargetAndBothChipsStillHit) {
    VelocityLaneFixture f;
    f.makeBed({100});
    const auto captionCentre = centreOf(f.roll.getVelocityCaptionBounds());
    f.roll.mouseDown(leftClick(f.roll, captionCentre));
    f.roll.mouseUp(leftClick(f.roll, captionCentre));
    EXPECT_TRUE(f.roll.isVelocityLaneVisible()) << "the caption does nothing";

    const auto chip = centreOf(f.roll.getVelocityChipBounds());
    f.roll.mouseDown(leftClick(f.roll, chip));
    f.roll.mouseUp(leftClick(f.roll, chip));
    EXPECT_FALSE(f.roll.isVelocityLaneVisible());

    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getHumanizeChipBounds())));
    EXPECT_TRUE(f.roll.isHeaderButtonHoveredForTest(PianoRollComponent::HeaderButtonId::Humanize));
    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getVelocityCaptionBounds())));
    EXPECT_FALSE(f.roll.isHeaderButtonHoveredForTest(PianoRollComponent::HeaderButtonId::Humanize))
        << "the caption is not part of the Humanize chip";
}
