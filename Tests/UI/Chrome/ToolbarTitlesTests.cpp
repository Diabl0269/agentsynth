// ToolbarTitlesTests.cpp -- every toolbar button carries a stable screen-reader name at every width,
// and the toolbar's roving focus is wired to the real buttons (docs/layout/chrome.md#toolbar-keyboard-access).
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

// One or two acceptable names: a toggle is named for what a press does, so its name depends on the
// persisted panel state this test does not pin. The theme toggle depends on the active theme.
struct ExpectedTitle {
    const char* id;
    const char* title;
    const char* orTitle;
};

const std::vector<ExpectedTitle>& expectedTitles() {
    static const std::vector<ExpectedTitle> titles = {
        {"toggleLibrary", "Hide Library", "Show Library"},
        {"newButton", "New", nullptr},
        {"saveButton", "Save", nullptr},
        {"loadButton", "Load", nullptr},
        {"settingsButton", "Settings", nullptr},
        {"feedbackButton", "Feedback", nullptr},
        {"undoButton", "Undo", nullptr},
        {"redoButton", "Redo", nullptr},
        {"autoArrangeButton", "Auto Arrange", nullptr},
        {"toggleMinimap", "Hide Minimap", "Show Minimap"},
        {"toggleModMatrix", "Hide Matrix", "Show Matrix"},
        {"toggleAiPanel", "Hide AI", "Show AI"},
        {"toggleBottomPanel", "Hide Panel", "Show Panel"},
        {"themeToggle", "Light Mode", "Dark Mode"},
    };
    return titles;
}

juce::Button* findToolbarButton(MainComponent& mc, const char* id) {
    return dynamic_cast<juce::Button*>(mc.findChildWithID(id));
}

void expectEveryButtonNamed(MainComponent& mc) {
    for (const auto& e : expectedTitles()) {
        auto* b = findToolbarButton(mc, e.id);
        ASSERT_NE(b, nullptr) << e.id;
        EXPECT_FALSE(b->getTitle().isEmpty()) << e.id << " has no screen-reader name";
        EXPECT_NE(b->getTitle(), b->getName()) << e.id << " would be read by its internal component name";
        EXPECT_TRUE(b->getTitle() == e.title || (e.orTitle != nullptr && b->getTitle() == e.orTitle))
            << e.id << " is named \"" << b->getTitle() << "\"";
        EXPECT_FALSE(b->getTooltip().isEmpty()) << e.id << " has no tooltip";
    }
}

} // namespace

TEST_F(MainComponentTest, EveryToolbarButtonHasAHumanNameInWideMode) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    expectEveryButtonNamed(mc);
}

TEST_F(MainComponentTest, ToolbarButtonsKeepTheirNamesWhenNarrowModeDropsTheVisibleText) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(480, 400);
    ASSERT_TRUE(mc.getToolbar().isNarrowMode());
    expectEveryButtonNamed(mc);
    EXPECT_TRUE(findToolbarButton(mc, "saveButton")->getButtonText().isEmpty()) << "icon-only, yet still named";
}

TEST_F(MainComponentTest, ToggleButtonNamesFollowTheStateTheirTextDoes) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(480, 400);
    auto* library = findToolbarButton(mc, "toggleLibrary");
    ASSERT_NE(library, nullptr);
    ASSERT_EQ(library->getTitle(), "Hide Library");
    library->onClick();
    EXPECT_EQ(library->getTitle(), "Show Library") << "even while narrow mode hides the visible text";
}

TEST_F(MainComponentTest, ToolbarRovingFocusDrivesTheRealButtons) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    auto& toolbar = mc.getToolbar();
    ASSERT_TRUE(mc.getGraphEditor().isMinimapVisible()) << "minimap starts shown";

    // From the ring's first stop to the minimap toggle: End is the theme toggle, then four Lefts back.
    toolbar.keyPressed(juce::KeyPress(juce::KeyPress::endKey));
    for (int i = 0; i < 4; ++i)
        toolbar.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
    ASSERT_EQ(toolbar.getFocusedSlot(), (int)ToolbarComponent::ToggleMinimap);
    EXPECT_EQ(toolbar.getFocusedButtonName(), "Hide Minimap");

    EXPECT_TRUE(toolbar.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_FALSE(mc.getGraphEditor().isMinimapVisible()) << "Return pressed the minimap toggle";
    EXPECT_EQ(toolbar.getFocusedButtonName(), "Show Minimap");
}

TEST_F(MainComponentTest, ToolbarSkipsTheDisabledUndoAndRedoButtons) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    auto& toolbar = mc.getToolbar();
    ASSERT_FALSE(findToolbarButton(mc, "undoButton")->isEnabled());

    // Library -> New, Save, Load, Settings, Feedback, then straight past Undo/Redo to Auto Arrange.
    for (int i = 0; i < 5; ++i)
        toolbar.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    ASSERT_EQ(toolbar.getFocusedSlot(), (int)ToolbarComponent::Feedback);
    toolbar.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    EXPECT_EQ(toolbar.getFocusedSlot(), (int)ToolbarComponent::AutoArrange);
}
