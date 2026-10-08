// ModDotSourceRemovalMotionTests.cpp: a source removed from the mod dot's panel shrinks toward its centre (180 ms) and
// then the rows below close the gap (200 ms); Cmd+Z makes room, grows the row back and fades an outline around it. The
// shared ExitEnterTimeline is stepped by hand with ModDotSourcesPage::applyTimelineAtMs.

#include "ModDotTestFixture.h"

#include "UI/Layout/ReducedMotion.h"

namespace {

using synth::ui::ExitEnterTimeline;
using synth::ui::ModDotSourceRow;
using synth::ui::ModDotSourcesPage;

struct ReducedMotionGuard {
    explicit ReducedMotionGuard(bool reduced) { synth::ui::setReducedMotionForTest(reduced); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// The panel opened on a knob with two sources, free to animate although it is not on screen.
struct TwoSources {
    Fixture f;
    juce::AudioProcessorGraph::NodeID first, second;
    ModDotSourcesPage* page = nullptr;

    TwoSources() {
        second = f.addSecondLfo();
        first = f.attenId;
        auto* panel = f.clickDot();
        page = &panel->sourcesPage();
        page->setForceAnimateForTest(true);
    }
    int rowY(juce::AudioProcessorGraph::NodeID id) { return page->animatingRowFor(id)->getY(); }
};

bool isScaled(const juce::Component& c) { return !c.getTransform().isIdentity(); }

} // namespace

TEST_F(ModuleComponentTest, RemovingASourceShrinksItsRowInPlaceThenTheRowsBelowCloseTheGap) {
    ReducedMotionGuard guard(false);
    TwoSources s;
    ASSERT_EQ(s.page->rowCount(), 2);
    const int firstY = s.rowY(s.first);
    const int secondOld = s.rowY(s.second);
    ASSERT_EQ(secondOld, firstY + ModDotSourceRow::kHeight);

    clickNow(s.page->rowAt(0)->removeButton());

    EXPECT_TRUE(s.page->isAnimating());
    EXPECT_TRUE(s.page->timeline().hasExit);
    EXPECT_TRUE(s.page->timeline().hasGap);
    EXPECT_EQ(s.page->rowCount(), 1) << "the model is already one source down";
    ASSERT_NE(s.page->animatingRowFor(s.first), nullptr) << "its row is still on screen, shrinking";
    EXPECT_FALSE(s.page->animatingRowFor(s.first)->isEnabled()) << "a row on its way out is not a control";

    s.page->applyTimelineAtMs(ExitEnterTimeline::kExitMs * 0.5);
    EXPECT_TRUE(isScaled(*s.page->animatingRowFor(s.first))) << "it shrinks toward its centre";
    EXPECT_EQ(s.page->slotHeightFor(s.first), ModDotSourceRow::kHeight)
        << "no slow collapse: the slot holds while the row shrinks";
    EXPECT_EQ(s.rowY(s.second), secondOld) << "the row below waits for the exit";

    s.page->applyTimelineAtMs(ExitEnterTimeline::kExitMs);
    EXPECT_EQ(s.rowY(s.second), secondOld);
    s.page->applyTimelineAtMs(ExitEnterTimeline::kExitMs + ExitEnterTimeline::kGapMs * 0.5);
    EXPECT_LT(s.rowY(s.second), secondOld);
    EXPECT_GT(s.rowY(s.second), firstY);

    s.page->applyTimelineAtMs(s.page->timeline().totalMs());
    EXPECT_EQ(s.rowY(s.second), firstY);
}

TEST_F(ModuleComponentTest, TheRemovalTakesTheSharedExitAndGapTimes) {
    ReducedMotionGuard guard(false);
    TwoSources s;
    clickNow(s.page->rowAt(1)->removeButton());
    EXPECT_DOUBLE_EQ(s.page->timeline().totalMs(), ExitEnterTimeline::kExitMs + ExitEnterTimeline::kGapMs);
    EXPECT_DOUBLE_EQ(s.page->timeline().totalMs(), 380.0);
}

TEST_F(ModuleComponentTest, UndoOpensTheGapThenGrowsTheRowBackThenFadesAnOutline) {
    ReducedMotionGuard guard(false);
    TwoSources s;
    const int firstY = s.rowY(s.first);
    clickNow(s.page->rowAt(0)->removeButton());
    s.page->applyTimelineAtMs(s.page->timeline().totalMs());
    EXPECT_EQ(s.rowY(s.second), firstY);

    ASSERT_TRUE(s.f.undo.undo());
    s.f.refresh(); // the editor's tick follows the graph

    ASSERT_EQ(s.page->rowCount(), 2);
    ASSERT_NE(s.page->animatingRowFor(s.first), nullptr) << "the row is back";
    EXPECT_TRUE(s.page->timeline().hasEnter);
    EXPECT_FALSE(s.page->timeline().hasExit);
    EXPECT_EQ(s.page->slotHeightFor(s.first), 0) << "the gap opens first";
    EXPECT_FALSE(s.page->animatingRowFor(s.first)->isVisible());
    EXPECT_EQ(s.rowY(s.second), firstY);

    s.page->applyTimelineAtMs(ExitEnterTimeline::kGapMs);
    EXPECT_EQ(s.page->slotHeightFor(s.first), ModDotSourceRow::kHeight);
    EXPECT_EQ(s.rowY(s.second), firstY + ModDotSourceRow::kHeight);
    EXPECT_FALSE(s.page->animatingRowFor(s.first)->isVisible()) << "nothing grows until the gap is open";

    s.page->applyTimelineAtMs(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs * 0.5);
    EXPECT_TRUE(s.page->animatingRowFor(s.first)->isVisible());
    EXPECT_TRUE(isScaled(*s.page->animatingRowFor(s.first)));
    EXPECT_FLOAT_EQ(s.page->outlineAlphaFor(s.first), 0.0f);

    s.page->applyTimelineAtMs(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs +
                              ExitEnterTimeline::kOutlineMs * 0.5);
    EXPECT_FALSE(isScaled(*s.page->animatingRowFor(s.first)));
    EXPECT_NEAR(s.page->outlineAlphaFor(s.first), 0.5f, 0.01f);

    s.page->applyTimelineAtMs(s.page->timeline().totalMs());
    EXPECT_FLOAT_EQ(s.page->outlineAlphaFor(s.first), 0.0f);
}

TEST_F(ModuleComponentTest, ReduceMotionFadesTheRowInPlaceInsteadOfShrinkingIt) {
    ReducedMotionGuard guard(true);
    TwoSources s;
    clickNow(s.page->rowAt(0)->removeButton());

    EXPECT_TRUE(s.page->isAnimating());
    s.page->applyTimelineAtMs(ExitEnterTimeline::kExitMs * 0.5);
    auto* row = s.page->animatingRowFor(s.first);
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(isScaled(*row));
    EXPECT_LT(row->getAlpha(), 1.0f);
    EXPECT_GT(row->getAlpha(), 0.0f);
}

TEST_F(ModuleComponentTest, OffScreenARemovedRowIsGoneAtOnce) {
    ReducedMotionGuard guard(false);
    Fixture f;
    f.addSecondLfo();
    auto& page = f.clickDot()->sourcesPage();
    clickNow(page.rowAt(0)->removeButton());
    EXPECT_FALSE(page.isAnimating());
    EXPECT_EQ(page.rowCount(), 1);
    EXPECT_EQ(page.animatingRowFor(f.attenId), nullptr);
}

TEST_F(ModuleComponentTest, ASourceTheUserAddsGrowsInWithNoRestoreOutline) {
    ReducedMotionGuard guard(false);
    Fixture f;
    auto& page = f.clickDot()->sourcesPage();
    const auto added = f.addSecondLfo();
    ASSERT_EQ(page.rowCount(), 2);
    EXPECT_FALSE(page.timeline().hasEnter) << "an outline means restored";
    EXPECT_FLOAT_EQ(page.outlineAlphaFor(added), 0.0f);
}
