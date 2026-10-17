// BottomDockDetachIconTests.cpp -- the detach icon on every docked bottom-panel tab, left of its swap icon: where it
// sits, which tabs have one, that a click detaches THAT tab's panel, the Mixer's "both places" second view, Option+
// Return on the strip, and the tooltip and screen reader name (docs/mixer/panel.md#placement-and-detachable-windows).
#include "../Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "BottomDockMainAreaFixture.h"
#include "UI/Layout/IconButton.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/IconGlyphs.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <optional>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using synth::ui::IconButton;
using Tab = BottomDockComponent::Tab;

constexpr auto kAlt = juce::ModifierKeys::altModifier;

} // namespace

class BottomDockDetachIconTest : public BottomDockMainAreaTest {
protected:
    void SetUp() override {
        keysGuard_.emplace(juce::StringArray{"detachedPanelBothPlaces"});
        BottomDockMainAreaTest::SetUp();
        setBothPlaces("move");
        if (!mc_->isBottomDockConfiguredVisible())
            mc_->simulateToggleBottomPanelClick();
        mc_->resized();
    }
    void TearDown() override {
        BottomDockMainAreaTest::TearDown();
        keysGuard_.reset();
    }
    void setBothPlaces(const char* value) {
        mc_->getAppPropertiesForTest().getUserSettings()->setValue("detachedPanelBothPlaces", value);
        dock().applyDetachBothPlacesPreference();
    }
    // The real press path of a button: the mouse goes down and up inside it.
    static void click(juce::Component& icon) {
        const auto centre = icon.getLocalBounds().getCentre().toFloat();
        icon.mouseDown(makeClickEvent(icon, centre, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
        icon.mouseUp(makeClickEvent(icon, centre));
    }
    bool pressOptionReturn() {
        return dock().getTabStripFocus().keyPressed(juce::KeyPress(juce::KeyPress::returnKey, kAlt, 0));
    }
    void useMirrorPreference() { setBothPlaces("both"); }
    std::vector<Tab> strip() {
        std::vector<Tab> offered;
        for (auto& stripTab : dock().getStripTabs())
            for (Tab t : {Tab::Timeline, Tab::Mixer, Tab::MidiRemote, Tab::Canvas})
                if (stripTab.name == BottomDockComponent::describeTab(t, Tab::Canvas).title)
                    offered.push_back(t);
        return offered;
    }

    std::optional<synth::test::PersistedKeysGuard> keysGuard_;
};

TEST_F(BottomDockDetachIconTest, EveryDockedTabHasADetachIconImmediatelyLeftOfItsSwapIcon) {
    for (Tab view : {Tab::Timeline, Tab::Mixer, Tab::MidiRemote}) {
        auto* detach = dock().getDetachIcon(view);
        ASSERT_NE(detach, nullptr);
        auto& swap = dock().getSwapIcon(view);
        auto* tab = detach->getParentComponent();
        ASSERT_NE(tab, nullptr);
        EXPECT_EQ(swap.getParentComponent(), tab) << "one icon row per tab";
        EXPECT_TRUE(detach->isVisible());
        EXPECT_TRUE(detach->isEnabled());
        EXPECT_EQ(detach->getGlyph(), synth::theme::Glyph::Detach);
        EXPECT_EQ(detach->getWidth(), 22);
        EXPECT_EQ(detach->getHeight(), tab->getHeight());
        EXPECT_EQ(detach->getRight() + 2, swap.getX()) << "the detach icon sits right against the swap icon";
        EXPECT_LE(swap.getRight(), tab->getWidth());
        const int reserved = tab->getProperties()[synth::theme::AppLookAndFeel::kTextInsetRightProperty];
        EXPECT_GE(reserved, swap.getRight() - detach->getX()) << "the label stops before the whole row";
        EXPECT_LE(tab->getWidth() - reserved, detach->getX());
    }
}

TEST_F(BottomDockDetachIconTest, TheCanvasTabHasNoDetachIcon) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    EXPECT_EQ(dock().getDetachIcon(Tab::Canvas), nullptr);
    auto& swap = dock().getSwapIcon(Tab::Canvas);
    EXPECT_TRUE(swap.isVisible());
    const int reserved =
        swap.getParentComponent()->getProperties()[synth::theme::AppLookAndFeel::kTextInsetRightProperty];
    EXPECT_EQ(reserved, swap.getParentComponent()->getWidth() - swap.getX())
        << "the Canvas tab reserves room for its swap icon only";
}

TEST_F(BottomDockDetachIconTest, TheViewInTheMainAreaHasNoTabSoNoIcon) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Mixer));
    EXPECT_FALSE(dock().getDetachIcon(Tab::Mixer)->getParentComponent()->isVisible());
    EXPECT_EQ(strip(), (std::vector<Tab>{Tab::Timeline, Tab::Canvas, Tab::MidiRemote}))
        << "the Canvas took the Mixer's slot";
}

TEST_F(BottomDockDetachIconTest, TheOldDockLevelButtonIsGoneSoTheTabsRunToTheDocksRightEdge) {
    const auto buttons = dock().getTabButtons();
    int rightmost = 0;
    for (auto* button : buttons)
        if (button->isVisible())
            rightmost = std::max(rightmost, button->getRight());
    EXPECT_EQ(rightmost, dock().getWidth());
}

TEST_F(BottomDockDetachIconTest, TheIconIsWhatAMouseClickInItsPlaceHits) {
    auto* detach = dock().getDetachIcon(Tab::Mixer);
    const auto point = dock().getLocalPoint(detach, detach->getLocalBounds().getCentre());
    EXPECT_EQ(dock().getComponentAt(point), detach) << "not the tab underneath it";
}

TEST_F(BottomDockDetachIconTest, ClickingAnIconDetachesThatTabsPanelNotTheActiveOne) {
    dock().setActiveTab(Tab::Mixer);

    click(*dock().getDetachIcon(Tab::Timeline));

    EXPECT_TRUE(dock().getTimelineHost().isDetached());
    EXPECT_FALSE(dock().getMixerHost().isDetached()) << "the active tab's panel was not touched";
    EXPECT_FALSE(dock().getMidiRemoteHost().isDetached());
    EXPECT_EQ(dock().getActiveTab(), Tab::Mixer);
    EXPECT_EQ(strip(), (std::vector<Tab>{Tab::Mixer, Tab::MidiRemote}));
    EXPECT_TRUE(dock().getTimelineHost().getDetachedWindowForTest() != nullptr);
}

TEST_F(BottomDockDetachIconTest, ClickingTheActiveTabsIconMovesTheActiveTabToTheNextOne) {
    dock().setActiveTab(Tab::Timeline);

    click(*dock().getDetachIcon(Tab::Timeline));

    EXPECT_TRUE(dock().getTimelineHost().isDetached());
    EXPECT_EQ(dock().getActiveTab(), Tab::Mixer);
}

TEST_F(BottomDockDetachIconTest, AClosedWindowBringsTheTabBackWithAnIconThatDetachesItAgain) {
    click(*dock().getDetachIcon(Tab::MidiRemote));
    ASSERT_TRUE(dock().getMidiRemoteHost().isDetached());

    dock().getMidiRemoteHost().getDetachedWindowForTest()->closeButtonPressed();
    EXPECT_FALSE(dock().getMidiRemoteHost().isDetached());
    EXPECT_EQ(strip(), (std::vector<Tab>{Tab::Timeline, Tab::Mixer, Tab::MidiRemote}));

    click(*dock().getDetachIcon(Tab::MidiRemote));
    EXPECT_TRUE(dock().getMidiRemoteHost().isDetached());
}

TEST_F(BottomDockDetachIconTest, TheIconOfADetachedTabLeavesTheStripWithItsTab) {
    click(*dock().getDetachIcon(Tab::Mixer));
    EXPECT_FALSE(dock().getDetachIcon(Tab::Mixer)->getParentComponent()->isVisible());
    dock().getDetachIcon(Tab::Mixer)->onClick();
    EXPECT_TRUE(dock().getMixerHost().isDetached()) << "a stray press on a tab that is gone does not redock it";
}

TEST_F(BottomDockDetachIconTest, WithBothPlacesOnTheMixerIconOpensAndClosesASecondViewInsteadOfMovingIt) {
    useMirrorPreference();
    auto* icon = dock().getDetachIcon(Tab::Mixer);
    EXPECT_EQ(icon->getTitle(), "Detach Mixer to its own window");

    click(*icon);
    EXPECT_TRUE(dock().isMixerMirrorOpenForTest());
    EXPECT_FALSE(dock().getMixerHost().isDetached()) << "the tab stays in the strip";
    EXPECT_EQ(icon->getTitle(), "Close the second Mixer window");
    EXPECT_TRUE(icon->getTooltip().startsWith("Close the second Mixer window"));

    click(*icon);
    EXPECT_FALSE(dock().isMixerMirrorOpenForTest());
    EXPECT_EQ(icon->getTitle(), "Detach Mixer to its own window");
    click(*dock().getDetachIcon(Tab::Timeline));
    EXPECT_TRUE(dock().getTimelineHost().isDetached()) << "the other tabs still move to their window";
}

TEST_F(BottomDockDetachIconTest, OptionReturnOnTheStripPressesTheActiveTabsDetachIcon) {
    dock().setActiveTab(Tab::Mixer);
    EXPECT_TRUE(pressOptionReturn());

    EXPECT_TRUE(dock().getMixerHost().isDetached());
    EXPECT_FALSE(dock().getTimelineHost().isDetached());
    EXPECT_NE(dock().getActiveTab(), Tab::Mixer);
}

TEST_F(BottomDockDetachIconTest, OptionReturnOnTheCanvasTabIsConsumedAndDoesNothing) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    dock().setActiveTab(Tab::Canvas);

    EXPECT_TRUE(pressOptionReturn());
    EXPECT_FALSE(dock().getMixerHost().isDetached());
    EXPECT_FALSE(dock().getMidiRemoteHost().isDetached());
}

TEST_F(BottomDockDetachIconTest, PlainReturnAndShiftReturnStillMeanWhatTheyDid) {
    dock().setPanelFocusHookForTest([](juce::Component&) {});
    EXPECT_TRUE(dock().getTabStripFocus().keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_FALSE(dock().getTimelineHost().isDetached());
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_TRUE(dock().getTabStripFocus().keyPressed(
        juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_FALSE(dock().getTimelineHost().isDetached());
    EXPECT_NE(dock().getMainView(), Tab::Canvas) << "Shift+Return is still the swap";
}

TEST_F(BottomDockDetachIconTest, TheTooltipAndTheScreenReaderNameNameTheTabAndTheKey) {
    struct Case {
        Tab tab;
        const char* name;
    };
    const auto key = ShortcutManager::keyPressToDisplayString(juce::KeyPress(juce::KeyPress::returnKey, kAlt, 0));
    for (const Case c :
         {Case{Tab::Timeline, "Timeline"}, Case{Tab::Mixer, "Mixer"}, Case{Tab::MidiRemote, "Controllers"}}) {
        const juce::String expected = juce::String("Detach ") + c.name + " to its own window";
        EXPECT_EQ(BottomDockComponent::describeTab(c.tab, Tab::Canvas).detachTitle, expected);
        auto* icon = dock().getDetachIcon(c.tab);
        EXPECT_EQ(icon->getTitle(), expected);
        EXPECT_EQ(icon->getTooltip(), synth::ui::formatShortcutHint(expected, key));
        EXPECT_TRUE(icon->isAccessible());
    }
    EXPECT_TRUE(BottomDockComponent::describeTab(Tab::Canvas, Tab::Canvas).detachTitle.isEmpty());
}

TEST_F(BottomDockDetachIconTest, TheIconIsAButtonInsideTheTabAndTheStripStaysOneTabStop) {
    EXPECT_TRUE(dock().getTabStripFocus().getWantsKeyboardFocus());
    for (Tab view : {Tab::Timeline, Tab::Mixer, Tab::MidiRemote}) {
        auto* icon = dock().getDetachIcon(view);
        EXPECT_FALSE(icon->getWantsKeyboardFocus()) << "the strip is the one Tab stop; Option+Return presses it";
        EXPECT_TRUE(icon->getParentComponent()->isAccessible());
    }
}

// The glyph is a path, so it draws in the headless build too: a window outline with an arrow leaving its top-right
// corner, which is a different picture from the swap arrows.
TEST(BottomDockDetachGlyph, DrawsAWindowWithAnArrowLeavingItsTopRightCorner) {
    const auto render = [](synth::theme::Glyph glyph) {
        juce::Image image(juce::Image::ARGB, 22, 19, true);
        juce::Graphics g(image);
        synth::theme::paintGlyph(g, glyph, image.getBounds().toFloat(), juce::Colours::white);
        return image;
    };
    const auto detach = render(synth::theme::Glyph::Detach);
    const auto swap = render(synth::theme::Glyph::Swap);
    int inked = 0;
    int differing = 0;
    for (int y = 0; y < detach.getHeight(); ++y)
        for (int x = 0; x < detach.getWidth(); ++x) {
            inked += detach.getPixelAt(x, y).getAlpha() > 100 ? 1 : 0;
            differing += detach.getPixelAt(x, y) != swap.getPixelAt(x, y) ? 1 : 0;
        }
    EXPECT_GT(inked, 15);
    EXPECT_GT(differing, 10);
    EXPECT_GT(detach.getPixelAt(15, 3).getAlpha(), 0) << "the arrow head, top right";
    EXPECT_EQ(detach.getPixelAt(1, 1).getAlpha(), 0) << "the top-left corner is empty";
}
