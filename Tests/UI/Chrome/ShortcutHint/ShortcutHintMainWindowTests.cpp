// Concern: the shortcut hints as MainComponent wires them -- which real buttons are labelled with the
// dock open and hidden, and that a tab moved to its own window leaves the hidden-dock row.
#include "../../../App/MainComponent/MainComponentTestFixture.h"
#include "../../Layout/BottomDockActiveTabResetGuard.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintOverlay.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintText.h"
#include "UI/Layout/BottomDockComponent.h"

namespace {

using synth::ui::ShortcutHintOverlay;

ShortcutHintOverlay* findOverlay(MainComponent& mc) {
    for (int i = 0; i < mc.getNumChildComponents(); ++i)
        if (auto* overlay = dynamic_cast<ShortcutHintOverlay*>(mc.getChildComponent(i)))
            return overlay;
    return nullptr;
}

void setDockOpen(MainComponent& mc, bool open) {
    if (mc.isBottomDockConfiguredVisible() != open)
        mc.simulateToggleBottomPanelClick();
    ASSERT_EQ(mc.isBottomDockConfiguredVisible(), open);
}

juce::String keyText(MainComponent& mc, const char* action) {
    return synth::ui::hint::formatKeyCapTextForPlatform(mc.getShortcutManager().getBinding(action));
}

class ShortcutHintMainWindowTest : public ::testing::Test {
protected:
    BottomDockActiveTabResetGuardMDT tabGuard_;
};

// Holds Cmd for the delay through the overlay's real modifier handler.
void holdCmd(ShortcutHintOverlay& overlay, double& now) {
    const juce::ModifierKeys cmd(juce::ModifierKeys::commandModifier);
    overlay.modifierKeysChanged(cmd);
    now += ShortcutHintOverlay::kShowDelayMs;
    overlay.modifierKeysChanged(cmd);
}

} // namespace

TEST_F(ShortcutHintMainWindowTest, OpenDockLabelsTheToolbarAndEveryDockTab) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.newPatchForTest();
    setDockOpen(mc, true);

    auto* overlay = findOverlay(mc);
    ASSERT_NE(overlay, nullptr);
    double now = 0.0;
    overlay->setClockForTest([&] { return now; });
    holdCmd(*overlay, now);
    ASSERT_TRUE(overlay->areHintsShowing());

    auto& dock = mc.getBottomDock();
    const auto tabs = dock.getStripTabs();
    ASSERT_GE(tabs.size(), 2u);
    for (const auto& tab : tabs) {
        const auto tabBounds = overlay->getLocalArea(tab.button, tab.button->getLocalBounds());
        bool labelled = false;
        for (const auto& e : overlay->getEntries())
            if (!e.isPill && tabBounds.contains(e.bounds)) {
                labelled = true;
                EXPECT_EQ(e.keyText, keyText(mc, tab.actionId.toRawUTF8()));
            }
        EXPECT_TRUE(labelled) << tab.name;
    }

    // The Show/Hide Panel toolbar button carries Cmd+T's bubble, below the toolbar and clear of the dock.
    bool panelToggleLabelled = false;
    for (const auto& e : overlay->getEntries())
        if (!e.isPill && e.keyText == keyText(mc, "toggleBottomPanel") && e.bounds.getY() < 100)
            panelToggleLabelled = true;
    EXPECT_TRUE(panelToggleLabelled);
    for (const auto& e : overlay->getEntries())
        EXPECT_FALSE(e.isPill);

    // Cancelling on a real key press.
    overlay->keyPressed(juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0), &mc);
    EXPECT_FALSE(overlay->areHintsShowing());
}

TEST_F(ShortcutHintMainWindowTest, TheMixerTabLabelsItsSidePaneButtonWithTheToggleShortcut) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.newPatchForTest();
    setDockOpen(mc, true);
    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);

    auto* overlay = findOverlay(mc);
    ASSERT_NE(overlay, nullptr);
    double now = 0.0;
    overlay->setClockForTest([&] { return now; });
    holdCmd(*overlay, now);
    ASSERT_TRUE(overlay->areHintsShowing());

    auto& button = mc.getBottomDock().getMixerPanel().getSidePaneButton();
    ASSERT_TRUE(button.isVisible());
    const auto buttonBounds = overlay->getLocalArea(&button, button.getLocalBounds());
    // A toolbar-style bubble sits beside its button rather than inside it: the one carrying the toggle's
    // key and centred over the button is the button's own.
    bool labelled = false;
    for (const auto& e : overlay->getEntries())
        if (!e.isPill && e.keyText == keyText(mc, "toggleSidePane") &&
            std::abs(e.bounds.getCentreX() - buttonBounds.getCentreX()) <= buttonBounds.getWidth())
            labelled = true;
    EXPECT_TRUE(labelled);
    overlay->modifierKeysChanged(juce::ModifierKeys());
}

TEST_F(ShortcutHintMainWindowTest, HiddenDockShowsTheTabsAsPillsAndOmitsADetachedTab) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.newPatchForTest();
    auto& dock = mc.getBottomDock();
    dock.getMixerHost().setDetached(true);
    setDockOpen(mc, false);

    auto* overlay = findOverlay(mc);
    ASSERT_NE(overlay, nullptr);
    double now = 0.0;
    overlay->setClockForTest([&] { return now; });
    holdCmd(*overlay, now);
    ASSERT_TRUE(overlay->areHintsShowing());

    std::vector<juce::String> labels;
    for (const auto& e : overlay->getEntries())
        if (e.isPill) {
            labels.push_back(e.label);
            EXPECT_EQ(e.bounds.getBottom(), mc.getStatusBar().getY() - synth::ui::hint::kRowBottomMargin);
        }
    ASSERT_EQ(labels.size(), 3u);
    EXPECT_EQ(labels[0], "Show Panel");
    EXPECT_EQ(labels[1], "Timeline");
    EXPECT_EQ(labels[2], "Controllers") << "the detached Mixer is left out";

    overlay->modifierKeysChanged(juce::ModifierKeys());
    dock.getMixerHost().setDetached(false);
    // The open/closed state is persisted to the shared settings file: leave it open like the default.
    setDockOpen(mc, true);
}
