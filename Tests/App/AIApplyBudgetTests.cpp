// AIApplyBudgetTests.cpp
//
// The main-thread-stall guard for the AI edit plan (an answer once froze the app for 34 s on a real multi-track
// project). A full-song answer is previewed when it arrives and applied on the card's Apply, both on the message
// thread, against the shared real saved project (macros open, folded and nested, modulators, a hosted plugin), grown
// with extra instrument tracks so the live graph is the size of a real session. Each step is timed against a budget
// with bench::timeAgainstBudget; the budgets are generous (a loaded CI machine) and catch growth in kind, not noise.
// The disabled bench prints the real times: ./Tests --gtest_also_run_disabled_tests
// --gtest_filter='AIApplyBudget.DISABLED_*'

#include "../fixtures/saved-project/SavedProjectFixture.h"
#include "StallProbe.h"
#include "TestPaths.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <cstdio>

namespace {

juce::var fullSongPlan() {
    const auto file = juce::File(TESTS_ROOT_DIR).getChildFile("fixtures/ai-plans/full-song-answer.json");
    return juce::JSON::parse(file.loadFileAsString());
}

void growProject(MainComponent& mc, int extraTracks) {
    for (int i = 0; i < extraTracks; ++i)
        mc.getTimelinePanel().applyAddTrackMenuChoice(
            synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
}

struct Times {
    bench::BudgetResult preview, apply, undo, redo;
};

Times measure(int extraTracks) {
    synth::test::SavedProjectFixture project;
    auto mc = project.open();
    EXPECT_NE(mc, nullptr);
    Times times;
    if (mc == nullptr)
        return times;
    growProject(*mc, extraTracks);
    const juce::var plan = fullSongPlan();
    auto& service = mc->getAiServiceForTest();
    bool previewOk = false, applyOk = false;
    times.preview = bench::timeAgainstBudget("previewing a full-song answer", 1500,
                                             [&] { previewOk = service.previewProjectEdit(plan).ok; });
    times.apply = bench::timeAgainstBudget("applying a full-song answer", 2000,
                                           [&] { applyOk = service.applyProjectEdit(plan).ok; });
    EXPECT_TRUE(previewOk);
    EXPECT_TRUE(applyOk);
    times.undo = bench::timeAgainstBudget("undoing the applied answer", 1500, [&] { mc->getUndoManager().undo(); });
    times.redo = bench::timeAgainstBudget("redoing the applied answer", 1500, [&] { mc->getUndoManager().redo(); });
    return times;
}

} // namespace

TEST(AIApplyBudget, AFullSongAnswerPreviewsAppliesUndoesAndRedoesWithinTheMessageThreadBudget) {
    const Times times = measure(12);
    for (const auto* step : {&times.preview, &times.apply, &times.undo, &times.redo})
        EXPECT_FALSE(step->exceeded) << step->report();
}

TEST(AIApplyBudget, TheBudgetHelperFlagsACallThatRunsPastItsBudget) {
    const auto slow = bench::timeAgainstBudget("a slow action", 5, [] { juce::Thread::sleep(30); });
    EXPECT_TRUE(slow.exceeded);
    EXPECT_GE(slow.ms, 30.0);
    EXPECT_NE(slow.report().find("a slow action"), std::string::npos);
    const auto quick = bench::timeAgainstBudget("a quick action", 1000, [] {});
    EXPECT_FALSE(quick.exceeded);
}

TEST(AIApplyBudget, DISABLED_PrintsWhatAFullSongAnswerCostsAtSeveralProjectSizes) {
    for (const int extra : {0, 12, 40}) {
        const Times t = measure(extra);
        std::printf("extra tracks %2d: preview %6.0f ms, apply %6.0f ms, undo %6.0f ms, redo %6.0f ms\n", extra,
                    t.preview.ms, t.apply.ms, t.undo.ms, t.redo.ms);
    }
}
