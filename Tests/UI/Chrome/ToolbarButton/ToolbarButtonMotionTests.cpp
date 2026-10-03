// ToolbarButtonMotionTests.cpp -- the top bar's hover motion: a real mouse enter / exit / press on the
// button drives it (headless, so each tween lands at once), and under Reduce Motion nothing moves while
// the colours still change (docs/layout/animation.md#motion-rules).
#include "ToolbarButtonTestHelpers.h"

using namespace toolbartest;

namespace {
void enter(juce::Component& c) { c.mouseEnter(mouseEventOn(c)); }
void exitButton(juce::Component& c) { c.mouseExit(mouseEventOn(c)); }
void press(juce::Component& c) { c.mouseDown(mouseEventOn(c)); }

bool isIdentity(const juce::AffineTransform& t) { return t.isIdentity(); }
} // namespace

TEST_F(ToolbarButtonTest, NothingMovesAtRest) {
    for (const auto& spec : allToolbarIcons()) {
        auto& b = make(spec.icon, spec.group, spec.caption);
        EXPECT_TRUE(isIdentity(b.getPartTransform(0))) << spec.caption;
        EXPECT_TRUE(isIdentity(b.getPartTransform(1))) << spec.caption;
        EXPECT_EQ(b.getIconLift(), 0.0f);
        EXPECT_EQ(b.getChipSquash(), juce::Point<float>(1.0f, 1.0f));
    }
}

TEST_F(ToolbarButtonTest, HoveringStartsTheIconsMotionAndLeavingEndsIt) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(false);
    auto& b = make(Icon::ActionSettings, ToolbarGroup::Housekeeping, "Settings");
    const auto restImage = render(b);

    enter(b);
    EXPECT_FLOAT_EQ(b.getHoverAmount(), 1.0f);
    EXPECT_TRUE(b.isMotionEnabled());
    EXPECT_FALSE(isIdentity(b.getPartTransform(0))) << "the cog turns";
    EXPECT_FLOAT_EQ(b.getIconLift(), 1.0f);
    // The cog turns about its own centre: the centre stays put.
    const auto centre = b.getArt(false).partBounds[0].getCentre();
    const auto moved = centre.transformedBy(b.getPartTransform(0));
    EXPECT_NEAR(moved.x, centre.x, 1.0e-3f);
    EXPECT_NEAR(moved.y, centre.y, 1.0e-3f);
    EXPECT_FALSE(render(b).getPixelAt(chipOnlyPixel(b).x, chipOnlyPixel(b).y) ==
                 restImage.getPixelAt(chipOnlyPixel(b).x, chipOnlyPixel(b).y));

    exitButton(b);
    EXPECT_FLOAT_EQ(b.getHoverAmount(), 0.0f);
    EXPECT_TRUE(isIdentity(b.getPartTransform(0)));
    EXPECT_EQ(b.getIconLift(), 0.0f);
}

TEST_F(ToolbarButtonTest, EachIconMovesOnlyItsOwnPartOnHover) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(false);
    auto& arrange = make(Icon::ActionAutoArrange, ToolbarGroup::Edit, "Auto Arrange");
    enter(arrange);
    // The right-hand tiles part: the top one up, the bottom one down.
    EXPECT_LT(juce::Point<float>().transformedBy(arrange.getPartTransform(0)).y, 0.0f);
    EXPECT_GT(juce::Point<float>().transformedBy(arrange.getPartTransform(1)).y, 0.0f);

    auto& load = make(Icon::ActionLoad, ToolbarGroup::File, "Load");
    enter(load);
    EXPECT_FLOAT_EQ(juce::Point<float>().transformedBy(load.getPartTransform(0)).y, 2.0f) << "the arrow drops 2";
    EXPECT_TRUE(isIdentity(load.getPartTransform(1)));
}

TEST_F(ToolbarButtonTest, PressingSquashesTheChip) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(false);
    auto& b = make(Icon::ActionSave, ToolbarGroup::File, "Save");
    enter(b);
    press(b);
    EXPECT_FLOAT_EQ(b.getPressAmount(), 1.0f);
    const auto squash = b.getChipSquash();
    EXPECT_FLOAT_EQ(squash.x, 0.92f);
    EXPECT_FLOAT_EQ(squash.y, 0.86f);
}

TEST_F(ToolbarButtonTest, ReduceMotionKeepsEverythingStillButColoursStillChange) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(true);
    auto& b = make(Icon::ActionSettings, ToolbarGroup::Housekeeping, "Settings");
    const auto chipPixel = chipOnlyPixel(b);
    const auto hue = theme().colors.hueViolet;
    const auto bg = theme().colors.bg0;
    EXPECT_TRUE(pixelNear(render(b), chipPixel, bg.overlaidWith(hue.withAlpha(0.16f))));

    enter(b);
    EXPECT_FLOAT_EQ(b.getHoverAmount(), 1.0f);
    EXPECT_FALSE(b.isMotionEnabled());
    EXPECT_TRUE(isIdentity(b.getPartTransform(0)));
    EXPECT_EQ(b.getIconLift(), 0.0f);
    press(b);
    EXPECT_EQ(b.getChipSquash(), juce::Point<float>(1.0f, 1.0f));

    // The colours still move to their hover values: the chip strengthens (over the hover ground).
    const auto img = render(b);
    const auto ground = bg.overlaidWith(theme().colors.surfaceHi.withAlpha(0.85f));
    EXPECT_TRUE(pixelNear(img, chipPixel, ground.overlaidWith(hue.withAlpha(0.26f))));
}
