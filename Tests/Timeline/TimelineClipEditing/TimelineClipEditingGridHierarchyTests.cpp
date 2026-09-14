// TimelineClipEditing grid-hierarchy tests: the shared time-grid colour policy
// (gridLineAlphaFor/gridLineColourFor/gridLevelIsReadable) both TimelinePanelComponent and PianoRollComponent read.

#include "TimelineClipEditingToolTestHelpers.h"

// ---------------------------------------------- the shared time-grid colour policy --
//
// synth::ui::gridLineAlphaFor / gridLineColourFor / gridLevelIsReadable (TimelineClipLaneArea.h) are
// the ONE policy both grid painters read — TimelinePanelComponent for the lanes and
// PianoRollComponent for the roll. They are pure, so the two properties that actually matter (a
// monotonic hierarchy and the density guard) are asserted directly here rather than screenshotted.

TEST(TimelineGridHierarchyTest, AlphaHierarchyIsMonotonicAndBarIsStrongest) {
    using synth::ui::gridLineAlphaFor;
    using synth::ui::GridLineLevel;

    const float sub = gridLineAlphaFor(GridLineLevel::Subdivision);
    const float beat = gridLineAlphaFor(GridLineLevel::Beat);
    const float bar = gridLineAlphaFor(GridLineLevel::Bar);

    EXPECT_GT(beat, sub) << "a beat line must read as stronger than a snap subdivision";
    EXPECT_GT(bar, beat) << "and a bar line stronger than a beat";
    EXPECT_LE(bar, 1.0f);
}

// The regression this policy exists for: the sub-beat level used to be a 0.14 hairline, invisible on
// every dark theme. It is a HINT, not a whisper — pinned well clear of zero so a future tweak cannot
// quietly walk it back there.
TEST(TimelineGridHierarchyTest, SubdivisionAlphaIsAVisibleHintNotAHairline) {
    using synth::ui::gridLineAlphaFor;
    using synth::ui::GridLineLevel;

    EXPECT_GE(gridLineAlphaFor(GridLineLevel::Subdivision), 0.2f);
    EXPECT_LT(gridLineAlphaFor(GridLineLevel::Subdivision), gridLineAlphaFor(GridLineLevel::Bar));
}

// Density guard: a level whose lines would land closer than kMinGridLinePixels apart is dropped
// wholesale rather than drawn as a solid block that swamps the beat and bar lines above it.
TEST(TimelineGridHierarchyTest, DensityGuardDropsLevelsTighterThanTheMinimumSpacing) {
    using synth::ui::gridLevelIsReadable;
    using synth::ui::kMinGridLinePixels;

    // Exactly at the threshold is readable; a hair under it is not.
    EXPECT_TRUE(gridLevelIsReadable(1.0, kMinGridLinePixels));
    EXPECT_FALSE(gridLevelIsReadable(1.0, kMinGridLinePixels - 0.01));

    // A sixteenth grid: fine at 40 px/beat (10 px apart), gone at the minimum zoom (0.375 px apart).
    EXPECT_TRUE(gridLevelIsReadable(0.25, 40.0));
    EXPECT_FALSE(gridLevelIsReadable(0.25, synth::ui::TimelineViewState::kMinPixelsPerBeat));

    // Snap::Off reports a 0.0 division, and a garbage spacing must not sneak past either.
    EXPECT_FALSE(gridLevelIsReadable(0.0, 512.0));
    EXPECT_FALSE(gridLevelIsReadable(-1.0, 512.0));
    EXPECT_FALSE(gridLevelIsReadable(std::numeric_limits<double>::quiet_NaN(), 512.0));
    EXPECT_FALSE(gridLevelIsReadable(1.0, std::numeric_limits<double>::quiet_NaN()));
}

// Raising alpha alone cannot rescue a dark theme, where the `border` token is a shade off the
// background: the line is lifted TOWARDS the background's contrasting end first. Asserted in both
// directions, because a light theme has to get darker lines, not brighter ones.
TEST(TimelineGridHierarchyTest, GridLineColourLiftsAwayFromTheBackgroundOnDarkAndLightThemes) {
    using synth::ui::gridLineAlphaFor;
    using synth::ui::gridLineColourFor;
    using synth::ui::GridLineLevel;

    const juce::Colour darkBg(0xff101014);
    const juce::Colour darkBorder(0xff1e1e24); // a real dark-theme border: barely off the background

    const auto barOnDark = gridLineColourFor(GridLineLevel::Bar, darkBorder, darkBg);
    EXPECT_GT(barOnDark.getBrightness(), darkBorder.getBrightness())
        << "on a dark theme the line has to get brighter than the token it came from";
    // juce::Colour stores alpha as a uint8, so the round-trip is only accurate to 1/255.
    EXPECT_NEAR(barOnDark.getFloatAlpha(), gridLineAlphaFor(GridLineLevel::Bar), 1.0f / 255.0f);

    const juce::Colour lightBg(0xfff5f5f7);
    const juce::Colour lightBorder(0xffe2e2e6);
    const auto barOnLight = gridLineColourFor(GridLineLevel::Bar, lightBorder, lightBg);
    EXPECT_LT(barOnLight.getBrightness(), lightBorder.getBrightness())
        << "and darker on a light one — contrast, not brightness";

    // The hierarchy survives the colour derivation: same base, same background, monotonic alpha.
    const auto subOnDark = gridLineColourFor(GridLineLevel::Subdivision, darkBorder, darkBg);
    const auto beatOnDark = gridLineColourFor(GridLineLevel::Beat, darkBorder, darkBg);
    EXPECT_LT(subOnDark.getFloatAlpha(), beatOnDark.getFloatAlpha());
    EXPECT_LT(beatOnDark.getFloatAlpha(), barOnDark.getFloatAlpha());
    EXPECT_EQ(subOnDark.withAlpha(1.0f), barOnDark.withAlpha(1.0f)) << "one colour, three opacities";
}
