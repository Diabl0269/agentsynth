// BottomDockTabMotionTests.cpp -- the bottom dock's tabs animate when one leaves the strip (its panel opens in its
// own window) and when it comes back: the leaving tab shrinks and its neighbours close up, the returning tab opens a
// gap and grows (docs/layout/animation.md#bottom-dock-tabs). The first group pins the pure placement maths, the
// second the dock itself: real detach and redock, stepped by hand since no VBlank reaches an off-screen strip.
#include "BottomDockMainAreaFixture.h"
#include "FadeVisibilityTestGuard.h"
#include "UI/Layout/BottomDockComponent/BottomDockTabMotion.h"
#include "UI/Layout/ExitEnterTimeline.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using synth::ui::BottomDockTabMotion;
using synth::ui::ExitEnterTimeline;
using Tab = BottomDockComponent::Tab;
using Tabs = std::vector<Tab>;

const juce::Rectangle<int> kStrip{0, 5, 900, 19};

const BottomDockTabMotion::Placement& of(const std::vector<BottomDockTabMotion::Placement>& placements, Tab tab) {
    return *std::find_if(placements.begin(), placements.end(), [tab](const auto& p) { return p.tab == tab; });
}

// The slots run left to right, touching, and fill the strip.
void expectFillsStrip(const std::vector<BottomDockTabMotion::Placement>& placements, double atMs) {
    int edge = kStrip.getX();
    for (const auto& p : placements) {
        EXPECT_EQ(p.bounds.getX(), edge) << "gap before a slot at " << atMs << " ms";
        edge = p.bounds.getRight();
    }
    EXPECT_EQ(edge, kStrip.getRight()) << "at " << atMs << " ms";
}

} // namespace

TEST(BottomDockTabMotion, NothingRunsForTheSameSetOrAnEmptyOne) {
    BottomDockTabMotion motion;
    EXPECT_FALSE(motion.begin({Tab::Timeline, Tab::Mixer}, {Tab::Timeline, Tab::Mixer}, false));
    EXPECT_FALSE(motion.begin({Tab::Timeline}, {}, false));
    EXPECT_FALSE(motion.isRunning());
    EXPECT_TRUE(motion.placements(kStrip).empty());
}

TEST(BottomDockTabMotion, ATabThatLeavesShrinksFirstThenItsNeighboursCloseUp) {
    BottomDockTabMotion motion;
    ASSERT_TRUE(motion.begin({Tab::Timeline, Tab::Mixer, Tab::MidiRemote}, {Tab::Timeline, Tab::MidiRemote}, false));

    auto at = [&](double ms) {
        motion.applyAtMs(ms);
        return motion.placements(kStrip);
    };
    auto start = at(0.0);
    ASSERT_EQ(start.size(), 3u);
    for (const auto& p : start) {
        EXPECT_EQ(p.bounds.getWidth(), 300);
        EXPECT_EQ(p.scale, 1.0f);
    }

    const auto midExit = at(ExitEnterTimeline::kExitMs * 0.5);
    EXPECT_GT(of(midExit, Tab::Mixer).scale, 0.0f);
    EXPECT_LT(of(midExit, Tab::Mixer).scale, 1.0f);
    EXPECT_EQ(of(midExit, Tab::Mixer).bounds.getWidth(), 300) << "its slot is still open while it shrinks";
    EXPECT_EQ(of(midExit, Tab::Timeline).bounds.getWidth(), 300);
    EXPECT_EQ(of(midExit, Tab::Timeline).scale, 1.0f);

    const auto endExit = at(ExitEnterTimeline::kExitMs);
    EXPECT_EQ(of(endExit, Tab::Mixer).scale, 0.0f);
    EXPECT_FALSE(of(endExit, Tab::Mixer).visible);
    EXPECT_EQ(of(endExit, Tab::MidiRemote).bounds.getWidth(), 300) << "nothing has moved yet";

    const auto midGap = at(ExitEnterTimeline::kExitMs + ExitEnterTimeline::kGapMs * 0.5);
    EXPECT_GT(of(midGap, Tab::Mixer).bounds.getWidth(), 0);
    EXPECT_LT(of(midGap, Tab::Mixer).bounds.getWidth(), 300);
    EXPECT_GT(of(midGap, Tab::Timeline).bounds.getWidth(), 300);
    EXPECT_LT(of(midGap, Tab::Timeline).bounds.getWidth(), 450);

    const auto done = at(motion.totalMs());
    EXPECT_TRUE(motion.applyAtMs(motion.totalMs()));
    EXPECT_EQ(of(done, Tab::Mixer).bounds.getWidth(), 0);
    EXPECT_EQ(of(done, Tab::Timeline).bounds.getWidth(), 450);
    EXPECT_EQ(of(done, Tab::MidiRemote).bounds.getWidth(), 450);
}

TEST(BottomDockTabMotion, ATabThatReturnsOpensItsGapThenGrowsInItsOldSlot) {
    BottomDockTabMotion motion;
    ASSERT_TRUE(motion.begin({Tab::Timeline, Tab::MidiRemote}, {Tab::Timeline, Tab::Mixer, Tab::MidiRemote}, false));
    auto at = [&](double ms) {
        motion.applyAtMs(ms);
        return motion.placements(kStrip);
    };

    const auto start = at(0.0);
    ASSERT_EQ(start.size(), 3u);
    EXPECT_EQ(start[1].tab, Tab::Mixer) << "between its old neighbours";
    EXPECT_EQ(start[1].bounds.getWidth(), 0);
    EXPECT_EQ(start[0].bounds.getWidth(), 450);

    const auto midGap = at(ExitEnterTimeline::kGapMs * 0.5);
    EXPECT_GT(of(midGap, Tab::Mixer).bounds.getWidth(), 0) << "the neighbours make room";
    EXPECT_LT(of(midGap, Tab::Timeline).bounds.getWidth(), 450);
    EXPECT_EQ(of(midGap, Tab::Mixer).scale, 0.0f) << "and nothing grows until the room is there";

    const auto gapDone = at(ExitEnterTimeline::kGapMs);
    EXPECT_EQ(of(gapDone, Tab::Mixer).bounds.getWidth(), 300);
    EXPECT_EQ(of(gapDone, Tab::Mixer).scale, 0.0f);

    const auto midGrow = at(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs * 0.5);
    EXPECT_GT(of(midGrow, Tab::Mixer).scale, 0.0f);
    EXPECT_LT(of(midGrow, Tab::Mixer).scale, 1.0f);
    EXPECT_EQ(of(midGrow, Tab::Mixer).bounds.getWidth(), 300);

    const auto done = at(motion.totalMs());
    EXPECT_EQ(of(done, Tab::Mixer).scale, 1.0f);
    EXPECT_TRUE(of(done, Tab::Mixer).visible);
    EXPECT_EQ(motion.totalMs(), ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs) << "no outline phase for a tab";
}

TEST(BottomDockTabMotion, TheSlotsAlwaysTouchAndFillTheStrip) {
    for (const auto& change :
         {std::pair<Tabs, Tabs>{{Tab::Timeline, Tab::Mixer, Tab::MidiRemote, Tab::Canvas},
                                {Tab::Timeline, Tab::MidiRemote, Tab::Canvas}},
          {{Tab::Timeline, Tab::MidiRemote}, {Tab::Timeline, Tab::Mixer, Tab::MidiRemote}},
          {{Tab::Timeline, Tab::Mixer, Tab::MidiRemote}, {Tab::Timeline, Tab::Canvas, Tab::MidiRemote}}}) {
        BottomDockTabMotion motion;
        ASSERT_TRUE(motion.begin(change.first, change.second, false));
        for (double ms = 0.0; ms <= motion.totalMs(); ms += 17.0) {
            motion.applyAtMs(ms);
            expectFillsStrip(motion.placements(kStrip), ms);
        }
    }
}

TEST(BottomDockTabMotion, ASwapShrinksTheLeavingTabAndGrowsTheArrivingOneInItsSlot) {
    BottomDockTabMotion motion;
    ASSERT_TRUE(motion.begin({Tab::Timeline, Tab::Mixer, Tab::MidiRemote},
                             {Tab::Timeline, Tab::Canvas, Tab::MidiRemote}, false));
    motion.applyAtMs(0.0);
    const auto start = motion.placements(kStrip);
    ASSERT_EQ(start.size(), 4u);
    EXPECT_EQ(start[1].tab, Tab::Canvas);
    EXPECT_EQ(start[2].tab, Tab::Mixer);
    EXPECT_EQ(motion.totalMs(), ExitEnterTimeline::kExitMs + ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs);
    motion.applyAtMs(motion.totalMs());
    const auto done = motion.placements(kStrip);
    EXPECT_EQ(of(done, Tab::Mixer).bounds.getWidth(), 0);
    EXPECT_EQ(of(done, Tab::Canvas).bounds.getWidth(), 300);
    EXPECT_EQ(of(done, Tab::Canvas).scale, 1.0f);
}

TEST(BottomDockTabMotion, ReduceMotionFadesInsteadOfScaling) {
    BottomDockTabMotion motion;
    ASSERT_TRUE(motion.begin({Tab::Timeline, Tab::Mixer, Tab::MidiRemote}, {Tab::Timeline, Tab::MidiRemote}, true));
    motion.applyAtMs(ExitEnterTimeline::kExitMs * 0.5);
    const auto mid = motion.placements(kStrip);
    EXPECT_EQ(of(mid, Tab::Mixer).scale, 1.0f);
    EXPECT_GT(of(mid, Tab::Mixer).alpha, 0.0f);
    EXPECT_LT(of(mid, Tab::Mixer).alpha, 1.0f);
    EXPECT_EQ(of(mid, Tab::Timeline).alpha, 1.0f);
}

TEST(BottomDockTabMotion, FinishingLeavesNothingRunning) {
    BottomDockTabMotion motion;
    ASSERT_TRUE(motion.begin({Tab::Timeline, Tab::Mixer}, {Tab::Timeline}, false));
    EXPECT_EQ(motion.getTarget(), Tabs{Tab::Timeline});
    motion.finish();
    EXPECT_FALSE(motion.isRunning());
    EXPECT_TRUE(motion.placements(kStrip).empty());
}

// ---- The dock: a real detach and redock -------------------------------------------------------------------

class BottomDockTabMotionDockTest : public BottomDockMainAreaTest {
protected:
    void SetUp() override {
        BottomDockMainAreaTest::SetUp();
        if (!mc_->isBottomDockConfiguredVisible())
            mc_->simulateToggleBottomPanelClick();
        mc_->resized();
    }
    juce::Component& tabButton(Tab tab) { return *dock().getSwapIcon(tab).getParentComponent(); }
    int tabWidth(Tab tab) { return tabButton(tab).getWidth(); }
};

TEST_F(BottomDockTabMotionDockTest, OffScreenADetachLandsAtOnce) {
    dock().getMixerHost().setDetached(true);

    EXPECT_FALSE(dock().isTabMotionRunningForTest());
    EXPECT_FALSE(tabButton(Tab::Mixer).isVisible());
    EXPECT_EQ(tabWidth(Tab::Timeline), dock().getWidth() / 2);
}

TEST_F(BottomDockTabMotionDockTest, ADetachedTabShrinksAwayAndItsNeighboursCloseUp) {
    FadeAnimateGuard guard;
    const int before = tabWidth(Tab::Timeline);

    dock().getMixerHost().setDetached(true);

    ASSERT_TRUE(dock().isTabMotionRunningForTest());
    EXPECT_TRUE(tabButton(Tab::Mixer).isVisible()) << "it is still drawn while it shrinks";
    EXPECT_NEAR(tabWidth(Tab::Timeline), before, 1);
    EXPECT_FALSE(interceptsClicks(tabButton(Tab::Mixer))) << "a tab on its way out cannot be clicked";
    EXPECT_TRUE(interceptsClicks(tabButton(Tab::Timeline)));

    dock().stepTabMotionForTest(ExitEnterTimeline::kExitMs * 0.5);
    EXPECT_FALSE(tabButton(Tab::Mixer).getTransform().isIdentity()) << "shrinking about its centre";
    EXPECT_NEAR(tabWidth(Tab::Timeline), before, 1);

    dock().stepTabMotionForTest(ExitEnterTimeline::kExitMs + ExitEnterTimeline::kGapMs * 0.5);
    EXPECT_GT(tabWidth(Tab::Timeline), before) << "the neighbours are closing up";
    EXPECT_LT(tabWidth(Tab::Timeline), dock().getWidth() / 2);

    dock().stepTabMotionForTest(1.0e6);
    EXPECT_FALSE(dock().isTabMotionRunningForTest()) << "settled: no more frames";
    EXPECT_FALSE(tabButton(Tab::Mixer).isVisible());
    EXPECT_TRUE(tabButton(Tab::Mixer).getTransform().isIdentity());
    EXPECT_EQ(tabButton(Tab::Mixer).getAlpha(), 1.0f);
    EXPECT_TRUE(interceptsClicks(tabButton(Tab::Mixer))) << "back to normal for the day it returns";
    EXPECT_EQ(tabWidth(Tab::Timeline), dock().getWidth() / 2);
    EXPECT_EQ(tabButton(Tab::MidiRemote).getRight(), dock().getWidth());
}

TEST_F(BottomDockTabMotionDockTest, AClosedWindowGrowsTheTabBackInItsOldSlotAndTheNeighboursOpenAGap) {
    dock().getMixerHost().setDetached(true);
    ASSERT_FALSE(dock().isTabMotionRunningForTest());
    FadeAnimateGuard guard;

    dock().getMixerHost().getDetachedWindowForTest()->closeButtonPressed();

    ASSERT_FALSE(dock().getMixerHost().isDetached());
    ASSERT_TRUE(dock().isTabMotionRunningForTest());
    EXPECT_EQ(tabWidth(Tab::Mixer), 0) << "its slot starts closed";
    EXPECT_NEAR(tabWidth(Tab::Timeline), dock().getWidth() / 2, 1);

    dock().stepTabMotionForTest(ExitEnterTimeline::kGapMs * 0.5);
    EXPECT_GT(tabWidth(Tab::Mixer), 0);
    EXPECT_LT(tabWidth(Tab::Timeline), dock().getWidth() / 2) << "the neighbours make room";
    EXPECT_LT(tabButton(Tab::Timeline).getX(), tabButton(Tab::Mixer).getX());
    EXPECT_LT(tabButton(Tab::Mixer).getX(), tabButton(Tab::MidiRemote).getX()) << "in its old place";

    dock().stepTabMotionForTest(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs * 0.5);
    EXPECT_TRUE(tabButton(Tab::Mixer).isVisible());
    EXPECT_FALSE(tabButton(Tab::Mixer).getTransform().isIdentity()) << "growing";

    dock().stepTabMotionForTest(1.0e6);
    EXPECT_FALSE(dock().isTabMotionRunningForTest());
    EXPECT_TRUE(tabButton(Tab::Mixer).getTransform().isIdentity());
    EXPECT_NEAR(tabWidth(Tab::Mixer), dock().getWidth() / 3, 1);
    EXPECT_EQ(tabButton(Tab::MidiRemote).getRight(), dock().getWidth());
}

TEST_F(BottomDockTabMotionDockTest, TheRealIconStartsTheSameMotion) {
    FadeAnimateGuard guard;
    dock().getDetachIcon(Tab::Timeline)->onClick();
    EXPECT_TRUE(dock().isTabMotionRunningForTest());
    EXPECT_TRUE(dock().getTimelineHost().isDetached());
}

TEST_F(BottomDockTabMotionDockTest, UnderReduceMotionTheTabFadesWithoutScaling) {
    FadeAnimateGuard guard(synth::ui::AnimationMode::reduced);
    dock().getMixerHost().setDetached(true);
    ASSERT_TRUE(dock().isTabMotionRunningForTest());

    dock().stepTabMotionForTest(ExitEnterTimeline::kExitMs * 0.5);
    EXPECT_TRUE(tabButton(Tab::Mixer).getTransform().isIdentity());
    EXPECT_LT(tabButton(Tab::Mixer).getAlpha(), 1.0f);
    EXPECT_GT(tabButton(Tab::Mixer).getAlpha(), 0.0f);
}

TEST_F(BottomDockTabMotionDockTest, WithAnimationsOffTheChangeIsInstant) {
    FadeAnimateGuard guard(synth::ui::AnimationMode::off);
    dock().getMixerHost().setDetached(true);

    EXPECT_FALSE(dock().isTabMotionRunningForTest());
    EXPECT_FALSE(tabButton(Tab::Mixer).isVisible());
    EXPECT_EQ(tabWidth(Tab::Timeline), dock().getWidth() / 2);
}

TEST_F(BottomDockTabMotionDockTest, ASecondChangeMidMotionLandsTheFirstAndMovesOn) {
    FadeAnimateGuard guard;
    dock().getMixerHost().setDetached(true);
    dock().stepTabMotionForTest(ExitEnterTimeline::kExitMs * 0.5);

    dock().getMidiRemoteHost().setDetached(true);

    ASSERT_TRUE(dock().isTabMotionRunningForTest());
    EXPECT_FALSE(tabButton(Tab::Mixer).isVisible()) << "the first one has landed";
    dock().stepTabMotionForTest(1.0e6);
    EXPECT_EQ(tabWidth(Tab::Timeline), dock().getWidth());
    EXPECT_FALSE(dock().isTabMotionRunningForTest());
}

TEST_F(BottomDockTabMotionDockTest, ASwapToTheMainAreaPlaysTheSameMotion) {
    FadeAnimateGuard guard;
    ASSERT_TRUE(dock().swapTabToMain(Tab::Mixer));

    ASSERT_TRUE(dock().isTabMotionRunningForTest());
    EXPECT_TRUE(tabButton(Tab::Mixer).isVisible()) << "the Mixer's tab shrinks away";
    dock().stepTabMotionForTest(1.0e6);
    EXPECT_FALSE(tabButton(Tab::Mixer).isVisible());
    EXPECT_TRUE(tabButton(Tab::Canvas).isVisible()) << "and the Canvas tab has grown in";
    EXPECT_NEAR(tabWidth(Tab::Canvas), dock().getWidth() / 3, 1);
}
