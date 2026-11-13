// CommandPaletteMainComponentTests.cpp -- the command palette through the real window: Cmd+K and the top bar's Search
// button open it, a pick runs what the same thing runs elsewhere (a card is selected and centred, the Timeline shows,
// a "+" row adds what the Add list adds), an on/off row flips in place, Escape hands focus back, the bar remembers
// where it was dragged, and a plugin track's row has a window button reached from the keyboard.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestSettingsHelpers.h"
#include "../../TestWait.h"
#include "../Accessibility/AccessibilityAudit.h"
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "MainComponent/MainComponentCommandPalette.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/CommandPalette/CommandPaletteComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <memory>
#include <optional>

namespace {

using synth::ui::CommandPaletteComponent;
using synth::ui::ModMatrixPicker;

juce::MouseEvent mouseAt(juce::Component& comp, juce::Point<float> position) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys(), 0.0f,
                            0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), position,
                            juce::Time::getCurrentTime(), 1, false);
}

class CommandPaletteWindowTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        guard_.emplace(juce::StringArray{
            "librarySidebarVisible", "bottomDockVisible", "aiPanelVisible", synth::kCommandPaletteRecentSettingKey,
            synth::kCommandPalettePositionSettingKey, MainComponent::kNaturalScrollingKey});
        tabGuard_.emplace();
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        mc_->newPatchForTest();
        if (auto* ws = mc_->getWelcomeScreenForTest())
            ws->setVisible(false);
    }
    void TearDown() override {
        mc_.reset();
        tabGuard_.reset();
        guard_.reset();
        MainComponentTest::TearDown();
    }

    MainComponentCommandPalette& palette() { return MainComponentCommandPalette::of(*mc_); }
    ModMatrixPicker& picker() { return palette().getPaletteForTest()->getPicker(); }
    void openByKey() { ASSERT_TRUE(mc_->getCommandManager().invokeDirectly(AppCommands::openCommandPalette, false)); }
    // Opens the palette, types `query` and picks the first row.
    void run(const juce::String& query) {
        if (!palette().isOpen())
            openByKey();
        ASSERT_TRUE(palette().isOpen());
        picker().setSearchTextForTest(query);
        ASSERT_FALSE(picker().getVisibleItemTextsForTest().empty()) << query;
        picker().chooseVisibleItemForTest(0);
    }
    template <typename Module>
    std::vector<ModuleComponent*> cards() {
        std::vector<ModuleComponent*> found;
        for (auto* c : mc_->getGraphEditor().getModuleComponents())
            if (dynamic_cast<Module*>(c->getModule()) != nullptr)
                found.push_back(c);
        return found;
    }

    std::optional<synth::test::PersistedKeysGuard> guard_;
    std::optional<BottomDockActiveTabResetGuardMDT> tabGuard_;
    std::unique_ptr<MainComponent> mc_;
};

} // namespace

TEST_F(CommandPaletteWindowTest, CmdKOpensThePaletteAndPressingItAgainClosesIt) {
    openByKey();
    ASSERT_TRUE(palette().isOpen());
    auto* bar = palette().getPaletteForTest();
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->getParentComponent(), mc_.get());
    EXPECT_EQ(picker().getSearchPlaceholderForTest(), "Search everything, or type + to add");
    EXPECT_EQ(bar->getX(), mc_->getWidth() / 2 - CommandPaletteComponent::kWidth / 2) << "centred by default";
    EXPECT_GT(bar->getY(), mc_->getToolbar().getBottom() - 1) << "under the top bar";

    openByKey();
    EXPECT_FALSE(palette().isOpen());
    ASSERT_TRUE(synth::test::waitUntil([&] { return palette().getPaletteForTest() == nullptr; }));
}

TEST_F(CommandPaletteWindowTest, TheTopBarSearchButtonOpensItAndBothHintsMentionThePlus) {
    auto* search = dynamic_cast<synth::ui::ToolbarButton*>(mc_->findChildWithID("searchButton"));
    ASSERT_NE(search, nullptr);
    EXPECT_EQ(search->getTitle(), "Search");
    EXPECT_TRUE(search->getTooltip().contains("type + to add")) << search->getTooltip();
    EXPECT_TRUE(search->getTooltip().contains(
        ShortcutManager::keyPressToDisplayString(mc_->getShortcutManager().getBinding("openCommandPalette"))))
        << search->getTooltip();
    ASSERT_TRUE(search->onClick);
    search->onClick();
    ASSERT_TRUE(palette().isOpen());

    auto& info = palette().getPaletteForTest()->getInfoHintForTest();
    auto* tip = dynamic_cast<juce::TooltipClient*>(&info);
    ASSERT_NE(tip, nullptr);
    EXPECT_TRUE(tip->getTooltip().contains("Type + to add")) << tip->getTooltip();
    EXPECT_FALSE(info.getTitle().isEmpty());
}

TEST_F(CommandPaletteWindowTest, APlusRowAddsWhatTheAddListAdds) {
    const auto before = cards<OscillatorModule>().size();
    openByKey();
    picker().setSearchTextForTest("+ osc");
    const auto names = picker().getVisibleRowNamesForTest();
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names.front(), "Top match");
    EXPECT_EQ(picker().getVisibleItemTextsForTest().front(), "Oscillator");
    picker().chooseVisibleItemForTest(0);
    EXPECT_EQ(cards<OscillatorModule>().size(), before + 1);
    EXPECT_FALSE(palette().isOpen()) << "a pick closes the palette";
}

// Regression test for FRO733: "lfo6" found no "LFO 6", since a typed word had to match one word of the label whole.
TEST_F(CommandPaletteWindowTest, FocusingAModuleSelectsItsCardAndCentresTheViewOnIt) {
    run("+ lfo");
    run("+ lfo"); // the second is numbered: "LFO 2"
    const auto lfos = cards<LFOModule>();
    ASSERT_EQ(lfos.size(), 2u) << "each + row added an LFO";
    auto& editor = mc_->getGraphEditor();
    ModuleComponent* card = nullptr;
    juce::String title;
    // Two of a kind are renumbered ("LFO 1", "LFO 2") on the engine's next naming pass, not at the add.
    ASSERT_TRUE(synth::test::waitUntil([&] {
        for (auto* candidate : lfos)
            if (auto* node = mc_->getAudioEngine().getGraph().getNodeForId(candidate->getNodeId()))
                if (synth::moduleTitle(*node).containsChar(' ')) {
                    card = candidate;
                    title = synth::moduleTitle(*node);
                    return true;
                }
        return false;
    })) << "one LFO has a numbered title";
    editor.centreViewOn({-4000.0f, -4000.0f});
    editor.clearSelection();

    openByKey();
    picker().setSearchTextForTest(title.removeCharacters(" ").toLowerCase()); // "lfo1" for "LFO 1"
    ASSERT_FALSE(picker().getVisibleItemTextsForTest().empty());
    EXPECT_EQ(picker().getVisibleItemTextsForTest().front(), title);
    picker().chooseVisibleItemForTest(0);

    const auto selected = editor.getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front(), card->getNodeId());
    const auto centre = editor.getLocalArea(card, card->getLocalBounds()).getCentre();
    EXPECT_NEAR(centre.x, editor.getLocalBounds().getCentreX(), 2);
    EXPECT_NEAR(centre.y, editor.getLocalBounds().getCentreY(), 2);
}

TEST_F(CommandPaletteWindowTest, PickingTimelineShowsTheTimeline) {
    if (mc_->isBottomDockConfiguredVisible())
        mc_->simulateToggleBottomPanelClick();
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());
    openByKey();
    picker().setSearchTextForTest("timeline");
    EXPECT_EQ(picker().getVisibleRowNamesForTest().front(), "Top match");
    EXPECT_EQ(picker().getVisibleItemTextsForTest().front(), "Timeline");
    picker().chooseVisibleItemForTest(0);
    ASSERT_TRUE(synth::test::waitUntil([&] {
        return mc_->isBottomDockConfiguredVisible() &&
               mc_->getBottomDock().getActiveTab() == synth::ui::BottomDockComponent::Tab::Timeline;
    }));
}

TEST_F(CommandPaletteWindowTest, AnOnOffRowFlipsInPlaceAndThePaletteStaysOpen) {
    auto* settings = mc_->getAppPropertiesForTest().getUserSettings();
    const bool before = settings->getBoolValue(MainComponent::kNaturalScrollingKey, true);
    openByKey();
    picker().setSearchTextForTest("natural scrolling");
    ASSERT_EQ(picker().getVisibleItemTextsForTest().front(), "Natural scrolling");
    ASSERT_EQ(picker().getVisibleItemSwitchForTest(0), std::optional<bool>(before));

    picker().chooseVisibleItemForTest(0);
    EXPECT_TRUE(palette().isOpen());
    EXPECT_EQ(picker().getVisibleItemSwitchForTest(0), std::optional<bool>(!before));
    EXPECT_EQ(settings->getBoolValue(MainComponent::kNaturalScrollingKey, true), !before);

    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
    EXPECT_TRUE(palette().isOpen()) << "Return flips it back, still open";
    EXPECT_EQ(settings->getBoolValue(MainComponent::kNaturalScrollingKey, true), before);
}

TEST_F(CommandPaletteWindowTest, EscapeClosesAndHandsFocusBackToWhatHadIt) {
    auto& editor = mc_->getGraphEditor();
    mc_->setFocusedComponentOverrideForTest(&editor);
    openByKey();
    mc_->setFocusedComponentOverrideForTest(nullptr);
    ASSERT_TRUE(palette().isOpen());
    EXPECT_TRUE(picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(palette().isOpen());
    EXPECT_EQ(palette().getFocusRestoredForTest(), &editor);
    ASSERT_TRUE(synth::test::waitUntil([&] { return palette().getPaletteForTest() == nullptr; }));
}

TEST_F(CommandPaletteWindowTest, AnEmptyQueryListsRecentPicksFirstThenTheViews) {
    run("timeline");
    ASSERT_TRUE(synth::test::waitUntil([&] { return palette().getPaletteForTest() == nullptr; }));
    openByKey();
    const auto names = picker().getVisibleRowNamesForTest();
    ASSERT_GE(names.size(), 3u);
    EXPECT_EQ(names[0], "Recent");
    EXPECT_EQ(names[1], "Timeline");
    EXPECT_EQ(names[2], "Views");
}

// Regression test for FRO733: the list opened scrolled one header down, hiding "Recent", since its rows went in while
// the picker still had its empty-list height.
TEST_F(CommandPaletteWindowTest, TheListOpensAtItsTopWithTheFirstHeaderInView) {
    run("timeline");
    ASSERT_TRUE(synth::test::waitUntil([&] { return palette().getPaletteForTest() == nullptr; }));
    openByKey();
    EXPECT_EQ(picker().getViewTopForTest(), 0) << "\"Recent\" sits above the first row";
}

TEST_F(CommandPaletteWindowTest, DraggingTheFrameMovesTheBarAndTheNextOpenStartsThere) {
    openByKey();
    auto* bar = palette().getPaletteForTest();
    const auto start = bar->getPosition();
    bar->mouseDown(mouseAt(*bar, {20.0f, 6.0f}));
    bar->mouseDrag(mouseAt(*bar, {60.0f, 46.0f}));
    bar->mouseUp(mouseAt(*bar, {60.0f, 46.0f}));
    const auto moved = bar->getPosition();
    EXPECT_EQ(moved, start + juce::Point<int>(40, 40));

    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::escapeKey));
    ASSERT_TRUE(synth::test::waitUntil([&] { return palette().getPaletteForTest() == nullptr; }));
    openByKey();
    EXPECT_EQ(palette().getPaletteForTest()->getPosition(), moved);
}

TEST_F(CommandPaletteWindowTest, APluginTracksRowHasAWindowButtonReachedByTabAndPressedByReturn) {
    const auto tracksBefore = mc_->getTimelineDoc().getTracks().size();
    run("+ midi track");
    ASSERT_TRUE(synth::test::waitUntil([&] { return mc_->getTimelineDoc().getTracks().size() == tracksBefore + 1; }));
    const auto& track = mc_->getTimelineDoc().getTracks().back();
    const auto trackId = track.id;
    const auto name = track.name;
    std::vector<synth::TrackId> toggled;
    palette().isPluginTrackForTest = [trackId](synth::TrackId id) { return id == trackId; };
    palette().togglePluginWindowForTest = [&toggled](synth::TrackId id) { toggled.push_back(id); };

    openByKey();
    picker().setSearchTextForTest(name);
    ASSERT_EQ(picker().getVisibleItemTextsForTest().front(), name);
    auto* button = picker().getVisibleItemActionButtonForTest(0);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->getTitle(), "Show plugin window, " + name);
    auto* tip = dynamic_cast<juce::TooltipClient*>(button);
    ASSERT_NE(tip, nullptr);
    const auto chord = ShortcutManager::keyPressToDisplayString(
        mc_->getShortcutManager().getBinding("timelineToggleFocusedTrackPluginWindow"));
    EXPECT_TRUE(tip->getTooltip().contains(chord)) << tip->getTooltip();

    EXPECT_TRUE(picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::tabKey)));
    EXPECT_TRUE(picker().isActionSelectedForTest());
    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
    ASSERT_EQ(toggled.size(), 1u);
    EXPECT_EQ(toggled.front(), trackId);
    EXPECT_FALSE(palette().isOpen());
}

TEST_F(CommandPaletteWindowTest, EveryControlInThePaletteHasANameAndATooltip) {
    openByKey();
    const auto gaps = synth::test::auditAccessibility(*palette().getPaletteForTest());
    for (const auto& gap : gaps)
        ADD_FAILURE() << (gap.kind == synth::test::Gap::Kind::MissingName ? "no name: " : "no tooltip: ") << gap.path;
}
