// AddListMainComponentTests.cpp -- the Add list through the real window: the canvas toolbar's Add and the Timeline's
// Add open it with their own group first, a pick adds exactly what the sidebar's Return or the old Add Track picker
// added, and Cmd+T opens the list from whichever view has focus.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestSettingsHelpers.h"
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "Modules/OscillatorModule.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/AddList/AddList.h"
#include "UI/Chrome/CanvasToolbar/CanvasToolbar.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Timeline/TimelinePanelComponent/AddTrackPicker.h"
#include <gtest/gtest.h>
#include <memory>
#include <optional>

namespace {

using synth::ui::ModMatrixPicker;

class AddListWindowTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        guard_.emplace(juce::StringArray{"librarySidebarVisible", "bottomDockVisible", "aiPanelVisible"});
        tabGuard_.emplace();
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        mc_->newPatchForTest();
        if (auto* ws = mc_->getWelcomeScreenForTest())
            ws->setVisible(false);
        synth::ui::test_hooks::addTrackPickerHookForTest() = [this](std::unique_ptr<ModMatrixPicker> p,
                                                                    juce::Rectangle<int> anchor) {
            picker_ = std::move(p);
            anchor_ = anchor;
            ++opened_;
        };
    }
    void TearDown() override {
        synth::ui::test_hooks::addTrackPickerHookForTest() = nullptr;
        picker_.reset();
        mc_.reset();
        tabGuard_.reset();
        guard_.reset();
        MainComponentTest::TearDown();
    }

    // The first header the open picker shows (every row sits under one).
    juce::String firstHeader() const { return picker_->getVisibleRowNamesForTest().front(); }
    // Picks the row `text` (under `underHeader` when given) by walking headers and items in display order.
    void choose(const juce::String& text, const juce::String& underHeader = {}) {
        const auto names = picker_->getVisibleRowNamesForTest();
        const auto items = picker_->getVisibleItemTextsForTest();
        size_t item = 0;
        juce::String header;
        for (const auto& name : names) {
            if (item >= items.size() || name != items[item]) {
                header = name;
                continue;
            }
            if (name == text && (underHeader.isEmpty() || header == underHeader)) {
                picker_->chooseVisibleItemForTest((int)item);
                return;
            }
            ++item;
        }
        FAIL() << "no row " << text;
    }
    int oscillatorCards() {
        int n = 0;
        for (auto* c : mc_->getGraphEditor().getModuleComponents())
            if (dynamic_cast<OscillatorModule*>(c->getModule()) != nullptr)
                ++n;
        return n;
    }

    std::optional<synth::test::PersistedKeysGuard> guard_;
    std::optional<BottomDockActiveTabResetGuardMDT> tabGuard_;
    std::unique_ptr<MainComponent> mc_;
    std::unique_ptr<ModMatrixPicker> picker_;
    juce::Rectangle<int> anchor_;
    int opened_ = 0;
};

} // namespace

TEST_F(AddListWindowTest, TheCanvasToolbarsAddOpensAListWithModulesFirstAndPickingOscillatorAddsACard) {
    auto& add = mc_->getCanvasToolbar().addButton();
    ASSERT_TRUE(add.onClick);
    const int before = oscillatorCards();
    add.onClick();
    ASSERT_EQ(opened_, 1);
    ASSERT_NE(picker_, nullptr);
    EXPECT_EQ(anchor_, add.getScreenBounds());
    EXPECT_TRUE(firstHeader().startsWith("Modules")) << firstHeader();

    choose("Oscillator", firstHeader());
    EXPECT_EQ(oscillatorCards(), before + 1);
}

TEST_F(AddListWindowTest, TheTimelinesAddOpensAListWithTracksFirstAndPickingMidiTrackAddsATrack) {
    mc_->simulateToggleBottomPanelClick();
    auto& add = mc_->getTimelinePanel().getAddTrackButton();
    EXPECT_EQ(add.getButtonText(), "Add");
    ASSERT_TRUE(add.onClick);
    const auto tracksBefore = mc_->getTimelineDoc().getTracks().size();
    add.onClick();
    ASSERT_NE(picker_, nullptr);
    EXPECT_EQ(firstHeader(), "Tracks");

    choose("MIDI Track");
    EXPECT_EQ(mc_->getTimelineDoc().getTracks().size(), tracksBefore + 1);
}

TEST_F(AddListWindowTest, ATrackRowPickedFromTheCanvasListStillAddsTheTrack) {
    const auto tracksBefore = mc_->getTimelineDoc().getTracks().size();
    mc_->getCanvasToolbar().addButton().onClick();
    ASSERT_NE(picker_, nullptr);
    choose("Audio Track");
    EXPECT_EQ(mc_->getTimelineDoc().getTracks().size(), tracksBefore + 1);
}

TEST_F(AddListWindowTest, TheListIsNamedAddAndSearchesTracksModulesAndPresets) {
    mc_->getCanvasToolbar().addButton().onClick();
    ASSERT_NE(picker_, nullptr);
    EXPECT_EQ(picker_->getSearchPlaceholderForTest(), "Search tracks, modules and presets");
    EXPECT_EQ(mc_->getCanvasToolbar().addButton().getTitle(), "Add");
    EXPECT_TRUE(mc_->getCanvasToolbar().addButton().getTooltip().startsWith("Add a track, module, preset or marker"));
    EXPECT_TRUE(mc_->getTimelinePanel().getAddTrackButton().getTooltip().startsWith("Add a track, module"));
}

TEST_F(AddListWindowTest, CommandTWithCanvasFocusOpensTheCanvasListAndWithoutItTheTimelineOne) {
    mc_->setFocusedComponentOverrideForTest(&mc_->getGraphEditor());
    ASSERT_TRUE(mc_->getCommandManager().invokeDirectly(AppCommands::openAddTrackMenu, false));
    ASSERT_NE(picker_, nullptr);
    EXPECT_EQ(anchor_, mc_->getCanvasToolbar().addButton().getScreenBounds());
    EXPECT_TRUE(firstHeader().startsWith("Modules")) << firstHeader();

    picker_.reset();
    mc_->setFocusedComponentOverrideForTest(nullptr);
    ASSERT_TRUE(mc_->getCommandManager().invokeDirectly(AppCommands::openAddTrackMenu, false));
    ASSERT_NE(picker_, nullptr);
    EXPECT_EQ(anchor_, mc_->getTimelinePanel().getAddTrackButton().getScreenBounds());
    EXPECT_EQ(firstHeader(), "Tracks");
}

TEST_F(AddListWindowTest, TheShortcutActionIsCalledAdd) {
    const auto* info = mc_->getCommandManager().getCommandForID(AppCommands::openAddTrackMenu);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->shortName, "Add...");
}
