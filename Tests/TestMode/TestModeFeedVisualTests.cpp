// feedVisual: fills a module's analysis buffers with a sine, noise or silence so a display can be filmed without
// audio (docs/development/test-mode.md#feedvisual).

#include "../Macros/MacroContainer/MacroContainerTestHelpers.h"
#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "TestMode/TestModeFeedVisual.h"
#include "TestModeRig.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace synth::testmode;
using namespace testmode_rig;

namespace {

float peakOf(const VisualBuffer& buffer) {
    std::vector<float> samples((size_t)buffer.getSize());
    buffer.copyTo(samples);
    float peak = 0.0f;
    for (float s : samples)
        peak = std::max(peak, std::abs(s));
    return peak;
}

struct FeedRig : Rig {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    FeedRig() {
        undo.setGraphEditor(&editor);
        addAndMakeVisible(editor);
        editor.setBounds(0, 0, 1200, 800);
        setSize(1200, 800);
    }
};

} // namespace

TEST(TestModeFeedVisual, ASineFillsTheFilterSpectrumBufferAndSilenceClearsIt) {
    FilterModule filter;
    EXPECT_EQ(peakOf(filter.getSpectrumBuffer()), 0.0f);
    EXPECT_EQ(feedVisualBuffers(filter, FeedKind::sine, 440.0, 0.5f, 4410, 44100.0), 2); // the scope and the spectrum
    EXPECT_NEAR(peakOf(filter.getSpectrumBuffer()), 0.5f, 0.02f);
    feedVisualBuffers(filter, FeedKind::silence, 440.0, 0.5f, 4410, 44100.0);
    EXPECT_EQ(peakOf(filter.getSpectrumBuffer()), 0.0f);
}

TEST(TestModeFeedVisual, NoiseIsBoundedByItsAmplitudeAndRepeatsAcrossRuns) {
    FilterModule a, b;
    feedVisualBuffers(a, FeedKind::noise, 0.0, 0.25f, 4410, 44100.0);
    feedVisualBuffers(b, FeedKind::noise, 0.0, 0.25f, 4410, 44100.0);
    EXPECT_GT(peakOf(a.getSpectrumBuffer()), 0.1f);
    EXPECT_LE(peakOf(a.getSpectrumBuffer()), 0.25f);
    EXPECT_EQ(peakOf(a.getSpectrumBuffer()), peakOf(b.getSpectrumBuffer()));
}

TEST(TestModeFeedVisual, AModuleWithNoVisualBufferWritesNothing) {
    LFOModule lfo;
    EXPECT_EQ(feedVisualBuffers(lfo, FeedKind::sine, 440.0, 0.5f, 1024, 44100.0), lfo.getVisualBuffer() ? 1 : 0);
}

TEST(TestModeFeedVisual, TheCommandFeedsTheCardsModuleAndRefusesBadArguments) {
    FeedRig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto id = addModuleAt(rig.editor, rig.engine, std::make_unique<FilterModule>(), 300, 200);
    auto* card = findComponent(rig.editor, id);
    ASSERT_NE(card, nullptr);
    auto* filter = dynamic_cast<FilterModule*>(rig.engine.getGraph().getNodeForId(id)->getProcessor());
    ASSERT_NE(filter, nullptr);

    const auto fed = run("feedVisual", args({{"id", card->getComponentID()}, {"kind", "sine"}, {"ms", 100}}));
    ASSERT_EQ(fed.state, StepResult::State::done) << fed.error;
    EXPECT_GE(static_cast<int>(fed.value["buffers"]), 1);
    EXPECT_GT(peakOf(filter->getSpectrumBuffer()), 0.4f);

    ASSERT_EQ(run("feedVisual", args({{"id", card->getComponentID()}, {"kind", "silence"}})).state,
              StepResult::State::done);
    EXPECT_EQ(peakOf(filter->getSpectrumBuffer()), 0.0f);

    EXPECT_EQ(run("feedVisual", args({{"id", card->getComponentID()}, {"kind", "square"}})).state,
              StepResult::State::failed);
    EXPECT_EQ(run("feedVisual", args({{"kind", "sine"}})).state, StepResult::State::failed);
    EXPECT_EQ(run("feedVisual", args({{"id", "nothing-called-this"}, {"kind", "sine"}})).state,
              StepResult::State::failed);
}
