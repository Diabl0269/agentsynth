// BottomDockSwapIconTests.cpp -- the swap icon at the right edge of every bottom-panel tab: where it sits and when
// it is enabled, what a click does (the view trades places with the one in the main area), the tooltip and screen
// reader name, Shift+Return on the strip, and the label giving way before the icon on a narrow tab
// (docs/mixer/panel.md#the-tab-strip).
#include "BottomDockMainAreaFixture.h"
#include "UI/Layout/IconButton.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using synth::ui::IconButton;
using Tab = BottomDockComponent::Tab;

bool pressShiftReturn(BottomDockComponent& dock) {
    return dock.getTabStripFocus().keyPressed(
        juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier, 0));
}

} // namespace

// Every test below starts with the panel open, so the tab strip is laid out.
class BottomDockSwapIconTest : public BottomDockMainAreaTest {
protected:
    // The tooltip is the name plus the swap action's live key.
    juce::String tooltipWithKey(Tab view, const juce::String& name) {
        const auto key = ShortcutManager::keyPressToDisplayString(
            mc_->getShortcutManager().getBinding(BottomDockComponent::swapActionIdForTab(view)));
        return synth::ui::formatShortcutHint(name, key);
    }
    void SetUp() override {
        BottomDockMainAreaTest::SetUp();
        openPanel();
    }
    void openPanel() {
        if (!mc_->isBottomDockConfiguredVisible())
            mc_->simulateToggleBottomPanelClick();
        mc_->resized();
    }
};

TEST_F(BottomDockSwapIconTest, EveryOfferedTabHasAnEnabledSwapIconAtItsRightEdge) {
    for (Tab view : {Tab::Timeline, Tab::Mixer, Tab::MidiRemote}) {
        auto& icon = dock().getSwapIcon(view);
        auto* tab = icon.getParentComponent();
        ASSERT_NE(tab, nullptr);
        EXPECT_TRUE(tab->isVisible());
        EXPECT_TRUE(icon.isVisible());
        EXPECT_TRUE(icon.isEnabled());
        EXPECT_EQ(icon.getGlyph(), synth::theme::Glyph::Swap);
        EXPECT_EQ(icon.getWidth(), 22);
        EXPECT_EQ(icon.getHeight(), tab->getHeight());
        EXPECT_GT(icon.getX(), tab->getWidth() / 2) << "at the right edge";
        EXPECT_LE(icon.getRight(), tab->getWidth());
    }
}

TEST_F(BottomDockSwapIconTest, TheCanvasTabHasASwapIconOnceItIsInThePanelAndNoDetachButton) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    auto& icon = dock().getSwapIcon(Tab::Canvas);
    EXPECT_TRUE(icon.getParentComponent()->isVisible());
    EXPECT_TRUE(icon.isVisible());
    EXPECT_TRUE(icon.isEnabled());

    dock().setActiveTab(Tab::Canvas);
    EXPECT_FALSE(dock().getDetachButtonForTest().isVisible()) << "the canvas cannot open in its own window";
}

TEST_F(BottomDockSwapIconTest, ClickingAnIconPutsThatViewInTheMainAreaAndTheCanvasReturnsItOnTheCanvasIcon) {
    dock().getSwapIcon(Tab::Mixer).onClick();

    EXPECT_EQ(dock().getMainView(), Tab::Mixer);
    EXPECT_EQ(dock().getMixerHost().getParentComponent(), &holder());
    EXPECT_TRUE(holder().isVisible()) << "the main area was laid out again without anyone calling resized()";
    EXPECT_FALSE(holder().getBounds().isEmpty());
    EXPECT_EQ(dock().getActiveTab(), Tab::Canvas) << "the canvas that came down is what the panel shows";
    EXPECT_EQ(dock().getTabOrderForTest()[1], Tab::Canvas) << "and it took the Mixer's slot";

    dock().getSwapIcon(Tab::Canvas).onClick();
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_EQ(dock().getMixerHost().getParentComponent(), &dock());
    EXPECT_FALSE(holder().isVisible());
    EXPECT_EQ(dock().getActiveTab(), Tab::Mixer) << "the Mixer is back, and shown";
    EXPECT_EQ(dock().getTabOrderForTest(), (std::vector<Tab>{Tab::Timeline, Tab::Mixer, Tab::MidiRemote, Tab::Canvas}));
}

TEST_F(BottomDockSwapIconTest, ClickingAnInactiveTabsIconSwapsItAndTheCanvasBecomesTheActiveTab) {
    dock().setActiveTab(Tab::Timeline);
    dock().getSwapIcon(Tab::MidiRemote).onClick();
    EXPECT_EQ(dock().getMainView(), Tab::MidiRemote);
    EXPECT_EQ(dock().getActiveTab(), Tab::Canvas);

    dock().getSwapIcon(Tab::Timeline).onClick();
    EXPECT_EQ(dock().getMainView(), Tab::Timeline);
    EXPECT_EQ(dock().getActiveTab(), Tab::MidiRemote) << "the view that was in main comes down and is shown";
    EXPECT_EQ(dock().getMidiRemoteHost().getParentComponent(), &dock());
}

TEST_F(BottomDockSwapIconTest, TheSwapIsRememberedAcrossARestart) {
    dock().getSwapIcon(Tab::Timeline).onClick();
    EXPECT_EQ(mc_->getAppPropertiesForTest().getUserSettings()->getValue("mainView"), "timeline");

    mc_.reset();
    build();
    openPanel();
    EXPECT_EQ(dock().getMainView(), Tab::Timeline);
    EXPECT_EQ(dock().getTimelineHost().getParentComponent(), &holder());
    EXPECT_TRUE(dock().getSwapIcon(Tab::Canvas).isEnabled());
}

TEST_F(BottomDockSwapIconTest, ATabInItsOwnWindowHasADisabledIconAndNothingHappensOnTheSwap) {
    dock().getTimelineHost().setDetached(true);
    auto& icon = dock().getSwapIcon(Tab::Timeline);

    EXPECT_FALSE(icon.isEnabled());
    EXPECT_EQ(icon.getTooltip(), "Timeline is open in its own window");
    EXPECT_EQ(icon.getTitle(), "Show Timeline in the main area") << "the name does not change, only the state";
    EXPECT_FALSE(dock().swapTabToMain(Tab::Timeline));
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);

    dock().getTimelineHost().setDetached(false);
    EXPECT_TRUE(icon.isEnabled());
    EXPECT_EQ(icon.getTooltip(), tooltipWithKey(Tab::Timeline, "Show Timeline in the main area"));
}

TEST_F(BottomDockSwapIconTest, TheMixerCannotSwapOutsideTheTabPlacement) {
    dock().setMixerTabEnabled(false);
    EXPECT_FALSE(dock().swapTabToMain(Tab::Mixer));
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_FALSE(dock().getSwapIcon(Tab::Mixer).getParentComponent()->isVisible()) << "it has no tab at all";
}

TEST_F(BottomDockSwapIconTest, TheTooltipAndTheScreenReaderNameNameTheView) {
    struct Case {
        Tab tab;
        const char* name;
    };
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    for (const Case c : {Case{Tab::Timeline, "Timeline"}, Case{Tab::Mixer, "Mixer"},
                         Case{Tab::MidiRemote, "Controllers"}, Case{Tab::Canvas, "Canvas"}}) {
        const juce::String expected = juce::String("Show ") + c.name + " in the main area";
        EXPECT_EQ(BottomDockComponent::describeTab(c.tab, Tab::Canvas).swapTitle, expected);
        auto& icon = dock().getSwapIcon(c.tab);
        EXPECT_EQ(icon.getTooltip(), tooltipWithKey(c.tab, expected));
        EXPECT_EQ(icon.getTitle(), expected);
        EXPECT_TRUE(icon.isAccessible());
    }
}

TEST_F(BottomDockSwapIconTest, TheIconIsAButtonInsideTheTabRadioAndTheStripStaysOneTabStop) {
    for (Tab view : {Tab::Timeline, Tab::Mixer, Tab::MidiRemote}) {
        auto& icon = dock().getSwapIcon(view);
        EXPECT_FALSE(icon.getWantsKeyboardFocus()) << "the strip is the one Tab stop";
        EXPECT_TRUE(icon.isAccessible());
        EXPECT_TRUE(icon.getParentComponent()->isAccessible()) << "the button sits inside the tab's radio element";
    }
}

TEST_F(BottomDockSwapIconTest, ShiftReturnOnTheStripPressesTheActiveTabsIcon) {
    dock().setActiveTab(Tab::Mixer);
    EXPECT_TRUE(pressShiftReturn(dock()));
    EXPECT_EQ(dock().getMainView(), Tab::Mixer);
    EXPECT_EQ(dock().getActiveTab(), Tab::Canvas);

    EXPECT_TRUE(pressShiftReturn(dock())) << "the canvas tab's icon returns it";
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_EQ(dock().getActiveTab(), Tab::Mixer);
}

TEST_F(BottomDockSwapIconTest, PlainReturnStillOnlyMovesFocusIntoThePanel) {
    dock().setPanelFocusHookForTest([](juce::Component&) {});
    EXPECT_TRUE(dock().getTabStripFocus().keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
}

TEST_F(BottomDockSwapIconTest, ALabelEllipsisesBeforeTheIconGoesOnANarrowStrip) {
    for (const int width : {1400, 240}) {
        dock().setBounds(0, 0, width, 220);
        for (Tab view : {Tab::Timeline, Tab::Mixer, Tab::MidiRemote}) {
            auto& icon = dock().getSwapIcon(view);
            auto* tab = icon.getParentComponent();
            EXPECT_TRUE(icon.isVisible()) << width;
            EXPECT_EQ(icon.getWidth(), 22) << width;
            EXPECT_GE(icon.getX(), 0) << width;
            EXPECT_LE(icon.getRight(), tab->getWidth()) << width;
            const int reserved = tab->getProperties()[synth::theme::AppLookAndFeel::kTextInsetRightProperty];
            EXPECT_GE(reserved, icon.getWidth()) << "the label area stops before the icon, at " << width;
            EXPECT_LE(tab->getWidth() - reserved, icon.getX()) << "the label area ends where the icon starts";
        }
    }
}

// The look-and-feel keeps a label out of the reserved right edge: a label too long for the tab paints exactly
// what an empty one does there.
TEST(BottomDockSwapIconLabel, ALongLabelNeverPaintsIntoTheReservedEdge) {
    synth::theme::AppLookAndFeel lookAndFeel;
    constexpr int width = 100;
    constexpr int reserved = 24;
    const auto render = [&](const juce::String& text) {
        juce::TextButton button(text);
        button.setLookAndFeel(&lookAndFeel);
        button.getProperties().set(synth::theme::AppLookAndFeel::kTextInsetRightProperty, reserved);
        button.setBounds(0, 0, width, 19);
        juce::Image image(juce::Image::ARGB, width, 19, true);
        juce::Graphics g(image);
        button.paintEntireComponent(g, false);
        button.setLookAndFeel(nullptr);
        return image;
    };
    const auto withText = render("A label far too long to fit in this tab");
    const auto empty = render("");

    bool textInsideLabelArea = false;
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < width; ++x) {
            if (x >= width - reserved)
                EXPECT_EQ(withText.getPixelAt(x, y), empty.getPixelAt(x, y)) << x << "," << y;
            else if (withText.getPixelAt(x, y) != empty.getPixelAt(x, y))
                textInsideLabelArea = true;
        }
    EXPECT_TRUE(textInsideLabelArea) << "the label is drawn in the area that is left";
}
