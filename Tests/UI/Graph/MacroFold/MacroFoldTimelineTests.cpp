// MacroFoldTimelineTests.cpp
//
// The pure timing and geometry of a macro fold (MacroFoldTimeline.h): the stagger, the 320 ms cap, the reverse order of
// an expand, and a border that always holds every module.

#include "UI/Graph/MacroFoldAnimator/MacroFoldTimeline.h"
#include "UI/Macros/MacroCardComponent/MacroPreviewLayout.h"
#include <gtest/gtest.h>

namespace mf = synth::ui::macro_fold;

TEST(MacroFoldTimeline, TwoModulesUseTheFullStagger) {
    mf::Timeline t{2, true, false};
    EXPECT_DOUBLE_EQ(t.staggerMs(), 35.0);
    EXPECT_DOUBLE_EQ(t.startMs(0), 0.0);
    EXPECT_DOUBLE_EQ(t.startMs(1), 35.0);
    EXPECT_DOUBLE_EQ(t.modulesEndMs(), 35.0 + mf::kFlightMs);
}

TEST(MacroFoldTimeline, ABigMacroShrinksTheStaggerSoTheLastModuleLandsByTheCap) {
    for (int n : {5, 12, 40, 200}) {
        mf::Timeline t{n, true, false};
        EXPECT_LE(t.staggerMs(), mf::kStaggerMs);
        EXPECT_LE(t.modulesEndMs(), mf::kTotalMs + 1e-9) << n << " modules";
        EXPECT_LE(t.landMs(n - 1), mf::kTotalMs + 1e-9);
    }
    const mf::Timeline twelve{12, true, false};
    EXPECT_DOUBLE_EQ(twelve.modulesEndMs(), mf::kTotalMs);
}

TEST(MacroFoldTimeline, ExpandingIsTheExactReverseOfCollapsing) {
    mf::Timeline in{4, true, false}, out{4, false, false};
    for (int i = 0; i < 4; ++i)
        EXPECT_DOUBLE_EQ(out.startMs(i), in.startMs(3 - i)) << "the module that left last comes out first";
}

TEST(MacroFoldTimeline, ExpandingBouncesEightPercentPastItsPlaceAndSettlesExactly) {
    mf::Timeline out{3, false, false};
    float peak = 0.0f;
    for (double ms = 0.0; ms <= out.totalMs(); ms += 1.0)
        peak = std::max(peak, out.eased(0, ms));
    EXPECT_NEAR(peak, 1.08f, 0.005f);
    EXPECT_FLOAT_EQ(out.eased(0, out.totalMs()), 1.0f);

    mf::Timeline in{3, true, false};
    for (double ms = 0.0; ms <= in.totalMs(); ms += 1.0)
        EXPECT_LE(in.eased(0, ms), 1.0f) << "folding in never overshoots";
}

TEST(MacroFoldTimeline, ACableFinishesAfterTheLastModuleLandedOnlyWhenExpanding) {
    EXPECT_DOUBLE_EQ(mf::Timeline(3, false, true).totalMs(),
                     mf::Timeline(3, false, true).modulesEndMs() + mf::kCableMs);
    EXPECT_DOUBLE_EQ(mf::Timeline(3, true, true).totalMs(), mf::Timeline(3, true, true).modulesEndMs());
    EXPECT_FLOAT_EQ(mf::Timeline::cableDraw(100.0, 100.0), 0.0f);
    EXPECT_FLOAT_EQ(mf::Timeline::cableDraw(100.0, 100.0 + mf::kCableMs), 1.0f);
}

TEST(MacroFoldTimeline, TheBorderHoldsEveryModuleInEveryFrame) {
    const juce::Rectangle<float> hull(0, 0, 900, 600), card(40, 40, 200, 120);
    for (bool collapsing : {true, false}) {
        mf::Timeline t{6, collapsing, false};
        for (double ms = 0.0; ms <= t.totalMs(); ms += 2.0) {
            std::vector<juce::Rectangle<float>> members;
            for (int i = 0; i < 6; ++i) {
                const juce::Rectangle<float> open(100.0f + 120.0f * static_cast<float>(i), 50.0f, 90.0f, 200.0f);
                const juce::Rectangle<float> box(60.0f + 18.0f * static_cast<float>(i), 80.0f, 12.0f, 30.0f);
                members.push_back(collapsing ? mf::lerpRect(open, box, t.eased(i, ms))
                                             : mf::lerpRect(box, open, t.eased(i, ms)));
            }
            const auto outline = mf::outlineRect(hull, card, t.outlineToCard(ms), members);
            for (const auto& m : members)
                EXPECT_TRUE(outline.contains(m)) << "t=" << ms << (collapsing ? " collapse" : " expand");
        }
    }
}

TEST(MacroFoldTimeline, TheBorderRunsAheadOfTheModulesWhenExpanding) {
    mf::Timeline out{6, false, false};
    EXPECT_FLOAT_EQ(out.outlineToCard(0.0), 1.0f);
    EXPECT_NEAR(out.outlineToCard(out.modulesEndMs() * 0.6), 0.0f, 1e-4f);
    EXPECT_LT(out.outlineToCard(out.modulesEndMs() * 0.3), 0.5f);
}

TEST(MacroPreviewLayout, BoxesAreTheUnionScaledUniformlyAndCentred) {
    const std::vector<juce::Rectangle<int>> members{{100, 100, 200, 100}, {400, 100, 100, 100}};
    const auto boxes = macro_preview::boxes(members, {0, 0, 200, 100});
    ASSERT_EQ(boxes.size(), 2u);
    // Union is 400x100, scale 0.5, so it is 200x50, centred vertically.
    EXPECT_FLOAT_EQ(boxes[0].getX(), 0.0f);
    EXPECT_FLOAT_EQ(boxes[0].getY(), 25.0f);
    EXPECT_FLOAT_EQ(boxes[0].getWidth(), 100.0f);
    EXPECT_FLOAT_EQ(boxes[1].getX(), 150.0f);
    EXPECT_TRUE(macro_preview::boxes({}, {0, 0, 10, 10}).empty());
}
