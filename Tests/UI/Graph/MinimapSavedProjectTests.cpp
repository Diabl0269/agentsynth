// MinimapSavedProjectTests.cpp -- the minimap in a saved project with folded macros: the map shows what the canvas
// shows (a folded macro is one card, its members are not drawn), so a click on the map lands on cards
// (docs/layout/minimap.md#what-it-draws). Needs the saved-project fixture: a fresh test graph has no folded macro.
#include "../../fixtures/saved-project/SavedProjectFixture.h"
#include "UI/Graph/MinimapComponent.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>
#include <vector>

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
