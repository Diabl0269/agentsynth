// TimelineSectionStripSettleTests.cpp -- a dropped block settles into the real block without a visible jump: from the
// release, the frames around the moment the real block takes over differ by no more than easing alone does, the
// selection outline and the focus ring fade in instead of appearing at once, and the last frame is the picture of a
// strip that was never dragged. Every frame is a real paint, 16 ms apart, into a software image.
#include "TimelineSectionStripThreeBlocksFixture.h"
#include "UI/Layout/FocusRing.h"
#include <functional>
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

constexpr double kFrameMs = 16.0;
constexpr double kSettleMs = 400.0;
constexpr double kRestMs = 1000.0;
// Once the lift is below this the block has all but landed and only its look still changes. Easing alone moves a
// channel by less than kEasingStep between two frames then; a ring or outline that appears at once moves it by about
// the accent's full strength.
constexpr float kNearlyLanded = 0.02f;
constexpr int kEasingStep = 32;

struct FocusRingsShown {
    FocusRingsShown() { setFocusRingsShownForTest(true); }
    ~FocusRingsShown() { setFocusRingsShownForTest(false); }
};

// The part of the strip the settling block covers, a little wider for its outline and ring.
juce::Rectangle<int> landedArea(ThreeBlocks& f) {
    juce::Rectangle<float> area;
    for (const auto id : f.strip.getSelectedPlacements())
        if (const auto* placement = f.doc.getPlacement(id))
            if (const auto* section = f.doc.getSection(placement->section))
                area = area.getUnion({f.x(placement->startBeat), 0.0f,
                                      f.x(placement->startBeat + section->lengthBeats) - f.x(placement->startBeat),
                                      (float)f.strip.getHeight()});
    return area.toNearestInt().expanded(6, 0).getIntersection(f.strip.getLocalBounds());
}

int maxGap(const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area) {
    int gap = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            gap = std::max(gap, channelGap(a.getPixelAt(x, y), b.getPixelAt(x, y)));
    return gap;
}

struct SettleRun {
    int handoverGap = 0; // the frame the lift reaches 0 against the one before it
    int windowGap = 0;   // the largest between consecutive frames once the block is all but landed
    int restGap = 0;     // the last frame against the same strip after a long rest
    bool handedOver = false;
};

// Releases with `drop`, renders every 16 ms frame to `kSettleMs`, then the rest picture.
SettleRun settle(ThreeBlocks& f, const std::function<void()>& drop) {
    SettleRun run;
    drop();
    const auto area = landedArea(f);
    stepStrip(f.strip, 0.0);
    auto before = renderStrip(f.strip);
    for (double ms = kFrameMs; ms <= kSettleMs; ms += kFrameMs) {
        stepStrip(f.strip, ms);
        const auto now = renderStrip(f.strip);
        const int gap = maxGap(before, now, area);
        if (f.strip.getMotionForTest().lift() < kNearlyLanded)
            run.windowGap = std::max(run.windowGap, gap);
        if (!run.handedOver && f.strip.getMotionForTest().lift() <= 0.0f) {
            run.handedOver = true;
            run.handoverGap = gap;
        }
        before = now;
    }
    stepStrip(f.strip, kRestMs);
    run.restGap = maxGap(before, renderStrip(f.strip), area);
    return run;
}

} // namespace

// Regression test for FRO1751: the selection outline and the focus ring appeared at once on the frame the dropped
// block took over from its settling ghost.
TEST(TimelineSectionStripSettleTest, AMoveSettlesWithoutAJumpAtTheHandover) {
    FullMotion motion;
    FocusRingsShown rings;
    ThreeBlocks f;
    const auto run = settle(f, [&] { f.dragBlockLifted(0.0, 16.0); });
    ASSERT_TRUE(run.handedOver);
    printf("[measure] move handover=%d window=%d rest=%d\n", run.handoverGap, run.windowGap, run.restGap);
    EXPECT_LT(run.handoverGap, kEasingStep);
    EXPECT_LT(run.windowGap, kEasingStep);
    EXPECT_EQ(run.restGap, 0) << "the last frame is the picture at rest";
}

TEST(TimelineSectionStripSettleTest, ACoverDropSettlesWithoutAJumpAtTheHandover) {
    FullMotion motion;
    FocusRingsShown rings;
    ThreeBlocks f;
    const auto run = settle(f, [&] { f.dragBlockLifted(0.0, 8.0, kShiftFlag); });
    ASSERT_TRUE(run.handedOver);
    printf("[measure] cover handover=%d window=%d rest=%d\n", run.handoverGap, run.windowGap, run.restGap);
    EXPECT_LT(run.handoverGap, kEasingStep);
    EXPECT_LT(run.windowGap, kEasingStep);
    EXPECT_EQ(run.restGap, 0);
}

TEST(TimelineSectionStripSettleTest, ACopyDropSettlesWithoutAJumpAtTheHandover) {
    FullMotion motion;
    FocusRingsShown rings;
    ThreeBlocks f;
    const auto run = settle(f, [&] { f.dragBlockLifted(0.0, 16.0, kAltFlag); });
    ASSERT_TRUE(run.handedOver);
    printf("[measure] copy handover=%d window=%d rest=%d\n", run.handoverGap, run.windowGap, run.restGap);
    EXPECT_LT(run.handoverGap, kEasingStep);
    EXPECT_LT(run.windowGap, kEasingStep);
    EXPECT_EQ(run.restGap, 0);
}

TEST(TimelineSectionStripSettleTest, AGroupDropSettlesWithoutAJumpAtTheHandover) {
    FullMotion motion;
    FocusRingsShown rings;
    ThreeBlocks f;
    f.strip.selectPlacements({f.intro, f.verse}, f.intro);
    const auto run = settle(f, [&] { f.dragBlockLifted(0.0, 16.0); });
    ASSERT_TRUE(run.handedOver);
    printf("[measure] group handover=%d window=%d rest=%d\n", run.handoverGap, run.windowGap, run.restGap);
    EXPECT_LT(run.handoverGap, kEasingStep);
    EXPECT_LT(run.windowGap, kEasingStep);
    EXPECT_EQ(run.restGap, 0);
}

TEST(TimelineSectionStripSettleTest, ADropUnderReducedMotionAndAnimationsOffEndsOnTheRestPicture) {
    for (const auto mode : {AnimationMode::reduced, AnimationMode::off}) {
        FullMotion motion;
        setAnimationMode(mode);
        FocusRingsShown rings;
        ThreeBlocks f;
        const auto run = settle(f, [&] { f.dragBlockLifted(0.0, 16.0); });
        EXPECT_EQ(run.restGap, 0) << "mode " << (int)mode;
    }
}
