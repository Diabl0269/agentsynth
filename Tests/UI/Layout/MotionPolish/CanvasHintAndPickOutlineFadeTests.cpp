#include "../../Graph/ModDot/ModDotTestFixture.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/OscillatorModule.h"
#include "MotionStep.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotCanvasPicker.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Topic: the empty-canvas first-run hint fades in and out, and the pick-on-canvas layer's outline fades in and out
// (docs/layout/animation.md#fading-things-in-and-out). Headless: the animated path is forced with FadeAnimateGuard
// and stepped by hand.

namespace {
using synth::ui::AnimationMode;
using synth::ui::ModDotCanvasPicker;

// How far the editor's painted pixels differ from `baseline` (the largest per-channel difference).
int maxDifference(const juce::Image& a, const juce::Image& b) {
    int worst = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x) {
            const auto pa = a.getPixelAt(x, y);
            const auto pb = b.getPixelAt(x, y);
            worst = juce::jmax(worst, std::abs((int)pa.getRed() - (int)pb.getRed()),
                               std::abs((int)pa.getGreen() - (int)pb.getGreen()),
                               std::abs((int)pa.getBlue() - (int)pb.getBlue()));
        }
    return worst;
}

juce::Image render(GraphEditor& editor) {
    juce::Image image(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
    juce::Graphics g(image);
    editor.paintEntireComponent(g, false);
    return image;
}
} // namespace

TEST(EmptyCanvasHintFade, TheHintFadesInAndOutAsTheCanvasFillsAndEmpties) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    EXPECT_FLOAT_EQ(editor.emptyCanvasHint.value(), 1.0f) << "an empty canvas starts with its hint";

    FadeAnimateGuard guard;
    auto node = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    node->properties.set("x", 100);
    node->properties.set("y", 100);
    editor.updateComponents();
    EXPECT_TRUE(editor.emptyCanvasHint.isFading()) << "the first module starts the hint's fade out";
    EXPECT_FLOAT_EQ(editor.emptyCanvasHint.value(), 1.0f) << "nothing has moved at frame 0";

    stepMotion(0.5f);
    EXPECT_GT(editor.emptyCanvasHint.value(), 0.0f);
    EXPECT_LT(editor.emptyCanvasHint.value(), 1.0f);
    stepMotion(1.0f);
    EXPECT_FLOAT_EQ(editor.emptyCanvasHint.value(), 0.0f);
    EXPECT_FALSE(editor.emptyCanvasHint.isFading());

    engine.getGraph().removeNode(node->nodeID);
    editor.updateComponents();
    EXPECT_TRUE(editor.emptyCanvasHint.isFading()) << "an emptied canvas fades its hint back in";
    stepMotion(1.0f);
    EXPECT_FLOAT_EQ(editor.emptyCanvasHint.value(), 1.0f);
}

TEST(EmptyCanvasHintFade, ThePaintedTextFollowsTheFade) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.emptyCanvasHint.snapTo(false);
    const auto bare = render(editor);
    editor.emptyCanvasHint.snapTo(true);
    const auto full = render(editor);
    const int fullDelta = maxDifference(full, bare);
    ASSERT_GT(fullDelta, 0) << "the hint is painted";

    FadeAnimateGuard guard;
    editor.emptyCanvasHint.setShown(false);
    stepMotion(0.5f);
    const int midDelta = maxDifference(render(editor), bare);
    EXPECT_GT(midDelta, 0);
    EXPECT_LT(midDelta, fullDelta) << "half-way through the fade the text is fainter";
    stepMotion(1.0f);
    EXPECT_EQ(maxDifference(render(editor), bare), 0) << "and then it is gone";
}

TEST(EmptyCanvasHintFade, ReduceMotionFadesAndOffAndOffScreenAreInstant) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.emptyCanvasHint.setShown(false); // not on screen and not forced
    EXPECT_FALSE(editor.emptyCanvasHint.isFading());
    EXPECT_FLOAT_EQ(editor.emptyCanvasHint.value(), 0.0f);
    {
        FadeAnimateGuard reduced(AnimationMode::reduced);
        editor.emptyCanvasHint.setShown(true);
        EXPECT_TRUE(editor.emptyCanvasHint.isFading());
        stepMotion(1.0f);
    }
    FadeAnimateGuard off(AnimationMode::off);
    editor.emptyCanvasHint.setShown(false);
    EXPECT_FALSE(editor.emptyCanvasHint.isFading());
    EXPECT_FLOAT_EQ(editor.emptyCanvasHint.value(), 0.0f);
}

TEST_F(ModuleComponentTest, ThePickOutlineFadesInOnAModuleAndOutWhenThePointerLeavesAndWhenTheLayerEnds) {
    Fixture f;
    auto lfo2 = f.engine.getGraph().addNode(std::make_unique<LFOModule>())->nodeID;
    f.refresh();
    auto* card = findModuleComp(*f.editor, f.engine.getGraph().getNodeForId(lfo2)->getProcessor());
    ASSERT_NE(card, nullptr);

    ModDotCanvasPicker picker(*f.editor, [](juce::AudioProcessorGraph::NodeID) { return true; });
    picker.begin();
    const auto at = picker.getLocalPoint(card, card->getLocalBounds().getCentre());
    EXPECT_FLOAT_EQ(picker.getOutlineAlphaForTest(), 0.0f);

    FadeAnimateGuard guard;
    picker.mouseMove(makeModuleClickWithMods(picker, at, kPlain));
    EXPECT_TRUE(picker.isOutlineFadingForTest());
    EXPECT_FLOAT_EQ(picker.getOutlineAlphaForTest(), 0.0f) << "frame 0 is the first fade frame";
    stepMotion(0.5f);
    EXPECT_GT(picker.getOutlineAlphaForTest(), 0.0f);
    EXPECT_LT(picker.getOutlineAlphaForTest(), 1.0f);
    stepMotion(1.0f);
    EXPECT_FLOAT_EQ(picker.getOutlineAlphaForTest(), 1.0f);

    picker.mouseExit(makeModuleClickWithMods(picker, at, kPlain));
    EXPECT_TRUE(picker.isOutlineFadingForTest());
    stepMotion(0.5f);
    EXPECT_GT(picker.getOutlineAlphaForTest(), 0.0f);
    EXPECT_LT(picker.getOutlineAlphaForTest(), 1.0f);
    stepMotion(1.0f);
    EXPECT_FLOAT_EQ(picker.getOutlineAlphaForTest(), 0.0f);

    // Ending the layer with an outline up fades it out, then the layer leaves the canvas.
    picker.mouseMove(makeModuleClickWithMods(picker, at, kPlain));
    stepMotion(1.0f);
    picker.end();
    EXPECT_NE(picker.getParentComponent(), nullptr) << "it stays up while the outline fades";
    EXPECT_FALSE(interceptsClicks(picker)) << "but it takes no press from the first frame";
    stepMotion(0.5f);
    EXPECT_GT(picker.getOutlineAlphaForTest(), 0.0f);
    EXPECT_LT(picker.getOutlineAlphaForTest(), 1.0f);
    stepMotion(1.0f);
    EXPECT_EQ(picker.getParentComponent(), nullptr);
}

TEST_F(ModuleComponentTest, ThePickOutlineLandsAtOnceUnderReduceMotionNothingAndOffScreen) {
    Fixture f;
    auto lfo2 = f.engine.getGraph().addNode(std::make_unique<LFOModule>())->nodeID;
    f.refresh();
    auto* card = findModuleComp(*f.editor, f.engine.getGraph().getNodeForId(lfo2)->getProcessor());
    ASSERT_NE(card, nullptr);
    ModDotCanvasPicker picker(*f.editor, [](juce::AudioProcessorGraph::NodeID) { return true; });
    picker.begin();
    const auto at = picker.getLocalPoint(card, card->getLocalBounds().getCentre());

    picker.mouseMove(makeModuleClickWithMods(picker, at, kPlain)); // not on screen and not forced
    EXPECT_FALSE(picker.isOutlineFadingForTest());
    EXPECT_FLOAT_EQ(picker.getOutlineAlphaForTest(), 1.0f);
    {
        FadeAnimateGuard reduced(AnimationMode::reduced);
        picker.mouseExit(makeModuleClickWithMods(picker, at, kPlain));
        EXPECT_TRUE(picker.isOutlineFadingForTest()) << "Reduce Motion is a short fade";
        stepMotion(1.0f);
        EXPECT_FLOAT_EQ(picker.getOutlineAlphaForTest(), 0.0f);
    }
    FadeAnimateGuard off(AnimationMode::off);
    picker.mouseMove(makeModuleClickWithMods(picker, at, kPlain));
    EXPECT_FALSE(picker.isOutlineFadingForTest());
    EXPECT_FLOAT_EQ(picker.getOutlineAlphaForTest(), 1.0f);
    picker.end();
    EXPECT_EQ(picker.getParentComponent(), nullptr) << "Animations Off ends the layer at once";
}
