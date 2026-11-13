// The mini map eases in and out when toggled (docs/layout/animation.md): fade plus a small slide from its corner,
// reversible mid-flight, a plain fade under Reduce Motion, and synchronous when nothing is on screen.
#include "../../../fixtures/saved-project/SavedProjectFixture.h"
#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MinimapComponent.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <vector>

namespace {
struct ReducedMotionGuard {
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};
} // namespace

// Off screen, a toggle lands on its final state before it returns (no message pump).
TEST_F(GraphEditorTest, MinimapToggleLandsSynchronouslyOffScreen) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    editor.toggleMinimapVisibility();
    EXPECT_FALSE(editor.getMinimap().isVisible());
    EXPECT_FLOAT_EQ(editor.getMinimapSlideProgress(), 0.0f);
    editor.toggleMinimapVisibility();
    EXPECT_TRUE(editor.getMinimap().isVisible());
    EXPECT_FLOAT_EQ(editor.getMinimapSlideProgress(), 1.0f);
    EXPECT_FLOAT_EQ(editor.getMinimap().getAlpha(), 1.0f);
}

// Closing stays visible and fading until the animation ends, then hides; the map slides toward its corner.
TEST_F(GraphEditorTest, MinimapCloseFadesAndSlidesToItsCornerThenHides) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setMinimapAnimateOffScreenForTest(true);
    synth::ui::setReducedMotionForTest(false);
    ReducedMotionGuard guard;

    const auto rest = editor.getMinimap().getBounds();
    editor.setMinimapVisible(false);
    EXPECT_FALSE(editor.isMinimapVisible());
    EXPECT_TRUE(editor.getMinimap().isVisible()) << "stays up until the fade ends";
    EXPECT_TRUE(editor.isMinimapSlideMovingForTest());

    editor.advanceMinimapSlideForTest(0.5f);
    EXPECT_TRUE(editor.getMinimap().isVisible());
    EXPECT_GT(editor.getMinimap().getAlpha(), 0.0f);
    EXPECT_LT(editor.getMinimap().getAlpha(), 1.0f);
    EXPECT_LT(editor.getMinimap().getX(), rest.getX());
    EXPECT_GT(editor.getMinimap().getY(), rest.getY());

    editor.finishMinimapSlideForTest();
    EXPECT_FALSE(editor.getMinimap().isVisible());

    // And opening emerges from the corner: visible from frame 0 at alpha 0, ending at rest.
    editor.setMinimapVisible(true);
    EXPECT_TRUE(editor.getMinimap().isVisible());
    EXPECT_FLOAT_EQ(editor.getMinimap().getAlpha(), 0.0f);
    editor.finishMinimapSlideForTest();
    EXPECT_EQ(editor.getMinimap().getBounds(), rest);
    EXPECT_FLOAT_EQ(editor.getMinimap().getAlpha(), 1.0f);
}

// A re-toggle mid-flight reverses from where the map is, not from an extreme.
TEST_F(GraphEditorTest, MinimapRetoggleMidFlightReversesFromCurrentValue) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setMinimapAnimateOffScreenForTest(true);

    editor.setMinimapVisible(false);
    editor.advanceMinimapSlideForTest(0.6f);
    const float mid = editor.getMinimapSlideProgress();
    ASSERT_GT(mid, 0.0f);
    ASSERT_LT(mid, 1.0f);

    editor.setMinimapVisible(true);
    EXPECT_FLOAT_EQ(editor.getMinimapSlideProgress(), mid);
    editor.finishMinimapSlideForTest();
    EXPECT_FLOAT_EQ(editor.getMinimapSlideProgress(), 1.0f);
    EXPECT_TRUE(editor.getMinimap().isVisible());
}

// Reduce Motion: a plain fade, the map does not move.
TEST_F(GraphEditorTest, MinimapUnderReduceMotionFadesWithoutMoving) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setMinimapAnimateOffScreenForTest(true);
    synth::ui::setReducedMotionForTest(true);
    ReducedMotionGuard guard;

    const auto rest = editor.getMinimap().getBounds();
    editor.setMinimapVisible(false);
    editor.advanceMinimapSlideForTest(0.5f);
    EXPECT_EQ(editor.getMinimap().getBounds(), rest);
    EXPECT_LT(editor.getMinimap().getAlpha(), 1.0f);
    editor.finishMinimapSlideForTest();
    EXPECT_FALSE(editor.getMinimap().isVisible());
}

// The map draws what the canvas shows: needs the saved-project fixture (a fresh test graph has no folded macro).
namespace {

class MinimapSavedProject : public ::testing::Test {
protected:
    void SetUp() override {
        mc = project.open();
        ASSERT_TRUE(project.wasSaved());
        ASSERT_NE(mc, nullptr);
        mc->getGraphEditor().setSize(1000, 600);
    }

    // What the canvas draws as cards right now: every visible module card and every visible folded-macro card.
    std::vector<juce::Rectangle<float>> shownCards() {
        std::vector<juce::Rectangle<float>> shown;
        auto& editor = mc->getGraphEditor();
        for (auto* module : editor.getModuleComponents())
            if (module != nullptr && module->isVisible())
                shown.push_back(module->getBounds().toFloat());
        for (const auto& macro : editor.getMacros().getAll())
            if (auto* card = editor.getMacroController().getMacroCard(macro.id); card != nullptr && card->isVisible())
                shown.push_back(card->getBounds().toFloat());
        return shown;
    }

    synth::test::SavedProjectFixture project;
    std::unique_ptr<MainComponent> mc;
};

} // namespace

TEST_F(MinimapSavedProject, TheMapDrawsExactlyTheCardsTheCanvasShows) {
    auto& editor = mc->getGraphEditor();
    editor.updateComponents();
    ASSERT_TRUE(editor.getMacros().find(synth::test::SavedProjectFixture::trackMacroId(*mc, 0))->collapsed);

    const auto shown = shownCards();
    const auto model = editor.buildMinimapModel();
    ASSERT_FALSE(shown.empty());
    EXPECT_EQ(model.nodes.size(), shown.size()) << "a folded macro's members are not on the canvas, so not on the map";
    for (const auto& node : model.nodes) {
        bool found = false;
        for (const auto& card : shown)
            found = found || card == node.bounds;
        EXPECT_TRUE(found) << "a map node at " << node.bounds.toString() << " has no card on the canvas";
    }
}

TEST_F(MinimapSavedProject, ClickingAnyMapNodeBringsACardIntoView) {
    auto& editor = mc->getGraphEditor();
    editor.updateComponents();
    const auto model = editor.buildMinimapModel();
    ASSERT_FALSE(model.nodes.empty());
    for (const auto& node : model.nodes) {
        ASSERT_TRUE(static_cast<bool>(editor.getMinimap().onNavigate));
        editor.getMinimap().onNavigate(node.bounds.getCentre());
        const auto visible = editor.getVisibleCanvasRect();
        bool cardInView = false;
        for (const auto& card : shownCards())
            cardInView = cardInView || visible.intersects(card);
        EXPECT_TRUE(cardInView) << "the canvas is empty after a click on the node at " << node.bounds.toString();
    }
}
