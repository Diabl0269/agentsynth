// CanvasToolbarTests.cpp -- the canvas's own toolbar (docs/layout/chrome.md#canvas-toolbar): where it sits as the
// canvas's top row in the main area and in the bottom panel, what it holds, its one-Tab-stop keyboard, the canvas keys
// that reveal a hidden canvas, and the "Show button labels" switch that takes the captions off it and the top bar.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestSettingsHelpers.h"
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Chrome/CanvasToolbar/CanvasToolbar.h"
#include "UI/Chrome/CanvasToolbar/ToolbarLabelsPreference.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using synth::ui::CanvasToolbar;
using Tab = BottomDockComponent::Tab;

bool press(CanvasToolbar& toolbar, int keyCode, juce::ModifierKeys mods = {}) {
    return toolbar.keyPressed(juce::KeyPress(keyCode, mods, 0));
}

// ---- The component alone ------------------------------------------------------------------------------------------

class CanvasToolbarKeysTest : public ::testing::Test {
protected:
    CanvasToolbarKeysTest() {
        toolbar_.setBounds(0, 0, 400, CanvasToolbar::kHeight);
        for (int slot = 0; slot < CanvasToolbar::NumSlots; ++slot)
            toolbar_.buttonAt((CanvasToolbar::Slot)slot).onClick = [this, slot] { clicked_.push_back(slot); };
    }

    CanvasToolbar toolbar_;
    std::vector<int> clicked_;
};

} // namespace

TEST_F(CanvasToolbarKeysTest, TheRingStartsOnTheFirstButtonAndArrowsMoveItWithoutWrapping) {
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)CanvasToolbar::AutoArrange);
    EXPECT_TRUE(press(toolbar_, juce::KeyPress::rightKey));
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)CanvasToolbar::Minimap);
    EXPECT_TRUE(press(toolbar_, juce::KeyPress::rightKey));
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)CanvasToolbar::Matrix);
    EXPECT_TRUE(press(toolbar_, juce::KeyPress::rightKey));
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)CanvasToolbar::Matrix) << "no wrap at the right end";
    EXPECT_TRUE(press(toolbar_, juce::KeyPress::homeKey));
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)CanvasToolbar::AutoArrange);
    EXPECT_TRUE(press(toolbar_, juce::KeyPress::leftKey));
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)CanvasToolbar::AutoArrange) << "no wrap at the left end";
    EXPECT_TRUE(press(toolbar_, juce::KeyPress::endKey));
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)CanvasToolbar::Matrix);
}

TEST_F(CanvasToolbarKeysTest, SpaceAndReturnPressTheRingedButton) {
    press(toolbar_, juce::KeyPress::rightKey);
    EXPECT_TRUE(press(toolbar_, juce::KeyPress::returnKey));
    EXPECT_TRUE(press(toolbar_, juce::KeyPress::spaceKey));
    EXPECT_EQ(clicked_, (std::vector<int>{CanvasToolbar::Minimap, CanvasToolbar::Minimap}));
}

TEST_F(CanvasToolbarKeysTest, ADisabledButtonIsSkippedAndModifiedChordsBubbleUp) {
    toolbar_.minimapButton().setEnabled(false);
    press(toolbar_, juce::KeyPress::rightKey);
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)CanvasToolbar::Matrix) << "the greyed Minimap is not a stop";
    EXPECT_FALSE(press(toolbar_, juce::KeyPress::leftKey, juce::ModifierKeys::commandModifier));
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)CanvasToolbar::Matrix);
}

TEST_F(CanvasToolbarKeysTest, TheButtonsGiveUpFocusSoTheToolbarIsTheOnlyTabStop) {
    EXPECT_TRUE(toolbar_.getWantsKeyboardFocus());
    for (int slot = 0; slot < CanvasToolbar::NumSlots; ++slot)
        EXPECT_FALSE(toolbar_.buttonAt((CanvasToolbar::Slot)slot).getWantsKeyboardFocus());
    EXPECT_FALSE(toolbar_.getTitle().isEmpty());
}

TEST_F(CanvasToolbarKeysTest, IconOnlyNarrowsTheButtons) {
    toolbar_.resized();
    const int labelled = toolbar_.autoArrangeButton().getWidth();
    toolbar_.setIconOnly(true);
    EXPECT_LT(toolbar_.autoArrangeButton().getWidth(), labelled);
    toolbar_.setIconOnly(false);
    EXPECT_EQ(toolbar_.autoArrangeButton().getWidth(), labelled);
}

// ---- Inside the window ---------------------------------------------------------------------------------------------

namespace {

class CanvasToolbarWindowTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        guard_.emplace(juce::StringArray{synth::ui::kShowToolbarLabelsKey});
        tabGuard_.emplace();
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        mc_->newPatchForTest();
    }
    void TearDown() override {
        mc_.reset();
        tabGuard_.reset();
        guard_.reset();
        MainComponentTest::TearDown();
    }

    BottomDockComponent& dock() { return mc_->getBottomDock(); }
    CanvasToolbar& strip() { return mc_->getCanvasToolbar(); }
    GraphEditor& canvas() { return mc_->getGraphEditor(); }

    void setShowLabels(bool shown) {
        auto* settings = mc_->getAppPropertiesForTest().getUserSettings();
        settings->setValue(synth::ui::kShowToolbarLabelsKey, shown);
        settings->sendSynchronousChangeMessage(); // what the app's settings broadcast does, without the message loop
    }

    juce::Array<juce::Button*> everyButton() {
        juce::Array<juce::Button*> all;
        for (auto* parent : std::initializer_list<juce::Component*>{mc_.get(), &strip()})
            for (auto* child : parent->getChildren())
                if (auto* b = dynamic_cast<synth::ui::ToolbarButton*>(child))
                    all.add(b);
        return all;
    }

    std::optional<synth::test::PersistedKeysGuard> guard_;
    std::optional<BottomDockActiveTabResetGuardMDT> tabGuard_;
    std::unique_ptr<MainComponent> mc_;
};

} // namespace

TEST_F(CanvasToolbarWindowTest, TheThreeButtonsAreOnTheCanvasToolbarAndNotOnTheTopBar) {
    for (const char* id : {"autoArrangeButton", "toggleMinimap", "toggleModMatrix"}) {
        EXPECT_NE(strip().findChildWithID(id), nullptr) << id << " is on the canvas toolbar";
        EXPECT_EQ(mc_->findChildWithID(id), nullptr) << id << " left the window's own children";
    }
    EXPECT_EQ(mc_->getToolbar().getFocusedSlot(), (int)ToolbarComponent::Library);
}

TEST_F(CanvasToolbarWindowTest, InTheMainAreaItIsTheCanvasTopRow) {
    ASSERT_TRUE(strip().isVisible());
    EXPECT_EQ(strip().getHeight(), CanvasToolbar::kHeight);
    EXPECT_EQ(strip().getX(), canvas().getX());
    EXPECT_EQ(strip().getWidth(), canvas().getWidth());
    EXPECT_EQ(strip().getBottom(), canvas().getY()) << "the canvas starts right under the strip";
    EXPECT_GT(strip().getY(), mc_->getToolbar().getY()) << "under the top bar";
}

TEST_F(CanvasToolbarWindowTest, ItTravelsWithTheCanvasIntoTheBottomPanel) {
    mc_->setMainViewForTest(Tab::Timeline);
    EXPECT_FALSE(strip().isVisible()) << "hidden with the canvas while the panel is closed";

    mc_->ensureCanvasShowing();
    ASSERT_TRUE(canvas().isVisible());
    ASSERT_TRUE(strip().isVisible());
    const auto body = dock().getCanvasBodyBounds() + dock().getPosition();
    EXPECT_EQ(strip().getBounds(), body.withHeight(CanvasToolbar::kHeight)) << "the body's top row";
    EXPECT_EQ(canvas().getBounds(), body.withTrimmedTop(CanvasToolbar::kHeight));

    dock().setActiveTab(Tab::Mixer);
    EXPECT_FALSE(strip().isVisible()) << "another tab: the canvas and its toolbar are both gone";

    mc_->setMainViewForTest(Tab::Canvas);
    EXPECT_TRUE(strip().isVisible());
    EXPECT_EQ(strip().getBottom(), canvas().getY());
}

TEST_F(CanvasToolbarWindowTest, TheCanvasKeysBringAHiddenCanvasIntoViewFirst) {
    struct Key {
        juce::CommandID command;
        const char* name;
    };
    for (const auto& key : {Key{AppCommands::autoArrange, "Cmd+L"}, Key{AppCommands::toggleMinimap, "Cmd+K"},
                            Key{AppCommands::toggleModMatrix, "Cmd+M"}}) {
        mc_->setMainViewForTest(Tab::Timeline);
        if (mc_->isBottomDockConfiguredVisible())
            mc_->simulateToggleBottomPanelClick();
        ASSERT_FALSE(canvas().isVisible()) << key.name;
        ASSERT_TRUE(mc_->getCommandManager().invokeDirectly(key.command, false)) << key.name;
        EXPECT_TRUE(canvas().isVisible()) << key.name << " revealed the canvas";
        EXPECT_EQ(dock().getActiveTab(), Tab::Canvas) << key.name;
        EXPECT_TRUE(strip().isVisible()) << key.name;
        mc_->setMainViewForTest(Tab::Canvas);
    }
}

TEST_F(CanvasToolbarWindowTest, AddingAModuleFromTheLibraryRevealsAHiddenCanvas) {
    mc_->setMainViewForTest(Tab::Timeline);
    ASSERT_FALSE(canvas().isVisible());
    ASSERT_TRUE(static_cast<bool>(mc_->getModuleLibrary().onModuleActivated));
    mc_->getModuleLibrary().onModuleActivated("Oscillator");
    EXPECT_TRUE(canvas().isVisible());
    EXPECT_EQ(dock().getActiveTab(), Tab::Canvas);
}

TEST_F(CanvasToolbarWindowTest, TurningOffShowButtonLabelsLeavesEveryTopBarAndCanvasButtonIconOnly) {
    mc_->setSize(1600, 900);
    ASSERT_FALSE(mc_->getToolbar().isNarrowMode());
    const auto buttons = everyButton();
    ASSERT_EQ(buttons.size(), 14) << "eleven on the top bar, three on the canvas toolbar";
    for (auto* b : buttons)
        EXPECT_FALSE(b->getButtonText().isEmpty()) << b->getComponentID() << " starts with a caption";
    const int labelledWidth = strip().autoArrangeButton().getWidth();

    setShowLabels(false);
    for (auto* b : buttons) {
        EXPECT_TRUE(b->getButtonText().isEmpty()) << b->getComponentID() << " is icon only";
        EXPECT_FALSE(b->getTitle().isEmpty()) << b->getComponentID() << " keeps its screen-reader name";
        EXPECT_FALSE(b->getTooltip().isEmpty()) << b->getComponentID() << " keeps its tooltip";
    }
    EXPECT_LT(strip().autoArrangeButton().getWidth(), labelledWidth);
    EXPECT_NE(strip().autoArrangeButton().getTooltip().indexOfIgnoreCase("Cmd"), -1)
        << strip().autoArrangeButton().getTooltip() << " still names the key";

    setShowLabels(true);
    for (auto* b : buttons)
        EXPECT_FALSE(b->getButtonText().isEmpty()) << b->getComponentID() << " has its caption back";
    EXPECT_EQ(strip().autoArrangeButton().getWidth(), labelledWidth);
}

TEST_F(CanvasToolbarWindowTest, ANarrowWindowStaysIconOnlyWhateverThePreferenceSays) {
    setShowLabels(true);
    mc_->setSize(480, 400);
    ASSERT_TRUE(mc_->getToolbar().isNarrowMode());
    for (auto* b : everyButton())
        EXPECT_TRUE(b->getButtonText().isEmpty()) << b->getComponentID();
    EXPECT_TRUE(strip().isIconOnly());
}

TEST_F(CanvasToolbarWindowTest, TheGreyedMinimapKeepsItsExplanationAndIsNotATabStop) {
    mc_->setMainViewForTest(Tab::Timeline);
    mc_->ensureCanvasShowing();
    dock().setActiveTab(Tab::Canvas);
    mc_->setSize(1400, 420);
    if (!canvas().isMinimapSuppressedBySize())
        GTEST_SKIP() << "the canvas is tall enough here to keep its minimap";
    EXPECT_FALSE(strip().minimapButton().isEnabled());
    EXPECT_EQ(strip().minimapButton().getTooltip(), "Needs a taller canvas");
    press(strip(), juce::KeyPress::rightKey);
    EXPECT_NE(strip().getFocusedSlot(), (int)CanvasToolbar::Minimap);
}

TEST_F(CanvasToolbarWindowTest, TheToolbarIsOneTabStopBetweenTheLibraryAndTheCanvas) {
    auto& regions = mc_->getFocusRegionsForTest();
    const auto* region = regions.findById("canvasToolbar");
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region->root, &strip());
    EXPECT_TRUE(region->isCurrentlyOpen());
    EXPECT_EQ(regions.nextOpenRegionId("canvasToolbar", true), "canvas");

    mc_->setMainViewForTest(Tab::Timeline);
    // The swap re-registers every region, so the earlier pointer is stale.
    region = regions.findById("canvasToolbar");
    ASSERT_NE(region, nullptr);
    EXPECT_FALSE(region->isCurrentlyOpen()) << "no stop while the canvas is hidden";
}
