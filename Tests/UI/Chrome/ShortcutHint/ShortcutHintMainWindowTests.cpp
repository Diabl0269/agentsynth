// Concern: the shortcut hints as MainComponent wires them -- which real buttons are labelled with the
// dock open and hidden, and that a tab moved to its own window leaves the hidden-dock row.
#include "../../../App/MainComponent/MainComponentTestFixture.h"
#include "../../Layout/BottomDockActiveTabResetGuard.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintOverlay.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintText.h"
#include "UI/Layout/BottomDockComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/EditTool.h"
#include <algorithm>

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

namespace {

// True when a bubble with `text` sits over (or right beside) `button`: centred on it within its width.
bool bubbleOver(ShortcutHintOverlay& overlay, juce::Component& button, const juce::String& text) {
    const auto bounds = overlay.getLocalArea(&button, button.getLocalBounds());
    for (const auto& e : overlay.getEntries())
        if (!e.isPill && e.keyText == text &&
            std::abs(e.bounds.getCentreX() - bounds.getCentreX()) <= bounds.getWidth())
            return true;
    return false;
}

} // namespace

TEST_F(ShortcutHintMainWindowTest, TheTimelineListsEveryShortcutButtonItOwnsAsAHintTarget) {
    MainComponent mc(std::make_unique<MockProvider>());
    auto& panel = mc.getTimelinePanel();
    const auto targets = panel.getShortcutHintTargets();

    std::vector<juce::String> ids;
    for (const auto& [component, actionId] : targets) {
        EXPECT_NE(component, nullptr) << actionId;
        EXPECT_TRUE(mc.getShortcutManager().getBinding(actionId).isValid()) << actionId << " has no default key";
        ids.push_back(actionId);
    }
    for (const char* expected :
         {"timelineToolSelect", "timelineToolRange", "timelineToolSplit", "timelineToolGlue", "timelineToolErase",
          "timelineToolMute", "timelineToolDraw", "timelineSnapToggle", "timelineFollowPlayheadToggle",
          "timelineShapeFree", "timelineShapeLine", "timelineShapeSine", "timelineShapeTriangle", "timelineShapeSaw",
          "timelineShapeSquare"})
        EXPECT_NE(std::find(ids.begin(), ids.end(), juce::String(expected)), ids.end()) << expected;
    EXPECT_EQ(ids.size(), 15u);
    for (auto tool : synth::ui::kAllEditTools)
        EXPECT_NE(std::find_if(targets.begin(), targets.end(),
                               [&](const auto& t) { return t.first == panel.getToolButton(tool); }),
                  targets.end());
}

TEST_F(ShortcutHintMainWindowTest, HoldingCmdLabelsTheTimelineEditToolsSnapFollowPlayheadAndLoop) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.newPatchForTest();
    setDockOpen(mc, true);
    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);

    // Draw is the tool, so its shape strip is out and every target the panel lists is on screen.
    mc.getTimelinePanel().setActiveTool(synth::ui::EditTool::Draw);
    auto* overlay = findOverlay(mc);
    ASSERT_NE(overlay, nullptr);
    double now = 0.0;
    overlay->setClockForTest([&] { return now; });
    holdCmd(*overlay, now);
    ASSERT_TRUE(overlay->areHintsShowing());

    auto& panel = mc.getTimelinePanel();
    for (const auto& [component, actionId] : panel.getShortcutHintTargets()) {
        ASSERT_TRUE(component->isVisible()) << actionId;
        EXPECT_TRUE(bubbleOver(*overlay, *component, keyText(mc, actionId.toRawUTF8()))) << actionId;
    }
    // The loop button's key is the timeline's bare L while the transport action is unbound.
    EXPECT_TRUE(bubbleOver(*overlay, panel.getTransportBar().getLoopButton(), keyText(mc, "timelineToggleLoop")));
    overlay->modifierKeysChanged(juce::ModifierKeys());
}

TEST_F(ShortcutHintMainWindowTest, HoldingShiftLabelsTheDrawShapesAndNotTheBareToolKeys) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.newPatchForTest();
    setDockOpen(mc, true);
    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);
    auto& panel = mc.getTimelinePanel();
    panel.setActiveTool(synth::ui::EditTool::Draw);

    auto* overlay = findOverlay(mc);
    ASSERT_NE(overlay, nullptr);
    double now = 0.0;
    overlay->setClockForTest([&] { return now; });
    const juce::ModifierKeys shift(juce::ModifierKeys::shiftModifier);
    overlay->modifierKeysChanged(shift);
    now += ShortcutHintOverlay::kShowDelayMs;
    overlay->modifierKeysChanged(shift);
    ASSERT_TRUE(overlay->areHintsShowing());

    for (auto shape : synth::ui::kAllDrawShapes) {
        const auto id = "timelineShape" + juce::String(synth::ui::drawShapeName(shape));
        EXPECT_TRUE(bubbleOver(*overlay, *panel.getDrawShapeStrip().getButton(shape), keyText(mc, id.toRawUTF8())))
            << id;
    }
    EXPECT_FALSE(bubbleOver(*overlay, *panel.getToolButton(synth::ui::EditTool::Draw), keyText(mc, "timelineToolDraw")))
        << "a bare digit does not use Shift";
    overlay->modifierKeysChanged(juce::ModifierKeys());
}

TEST_F(ShortcutHintMainWindowTest, HoldingCtrlLabelsRecordAndMetronomeOnTheTransportBar) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.newPatchForTest();
    setDockOpen(mc, true);
    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);

    auto* overlay = findOverlay(mc);
    ASSERT_NE(overlay, nullptr);
    double now = 0.0;
    overlay->setClockForTest([&] { return now; });
    const juce::ModifierKeys ctrl(juce::ModifierKeys::ctrlModifier);
    overlay->modifierKeysChanged(ctrl);
    now += ShortcutHintOverlay::kShowDelayMs;
    overlay->modifierKeysChanged(ctrl);
    ASSERT_TRUE(overlay->areHintsShowing());

    auto& bar = mc.getTimelinePanel().getTransportBar();
    EXPECT_TRUE(bubbleOver(*overlay, bar.getRecordButton(), keyText(mc, "transportRecord")));
    EXPECT_TRUE(bubbleOver(*overlay, bar.getMetronomeButton(), keyText(mc, "transportToggleMetronome")));
    // Ctrl shows every target, like Cmd: the bare-key tools are labelled too.
    EXPECT_TRUE(bubbleOver(*overlay, *mc.getTimelinePanel().getToolButton(synth::ui::EditTool::Draw),
                           keyText(mc, "timelineToolDraw")));
    overlay->modifierKeysChanged(juce::ModifierKeys());
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

// The pill's label is drawn without ellipsis into the width left after the key cap, so a width rounded
// DOWN from the measured text clips the last letter ("Timelin").
TEST_F(ShortcutHintMainWindowTest, HiddenDockPillsLeaveRoomForTheWholeLabel) {
    synth::theme::AppLookAndFeel lookAndFeel; // outlives mc: the real, themed face the pills are drawn in
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setLookAndFeel(&lookAndFeel);
    mc.setSize(1600, 900);
    mc.newPatchForTest();
    setDockOpen(mc, false);

    auto* overlay = findOverlay(mc);
    ASSERT_NE(overlay, nullptr);
    double now = 0.0;
    overlay->setClockForTest([&] { return now; });
    holdCmd(*overlay, now);
    ASSERT_TRUE(overlay->areHintsShowing());

    const auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&overlay->getLookAndFeel());
    ASSERT_NE(lf, nullptr);
    const juce::Font font(
        juce::FontOptions(lf->getTheme().type.uiFamily, lf->getTheme().type.label + 1.0f, juce::Font::plain));
    int pills = 0;
    bool sawTimeline = false;
    for (const auto& e : overlay->getEntries()) {
        if (!e.isPill)
            continue;
        ++pills;
        sawTimeline = sawTimeline || e.label == "Timeline";
        // The rectangle ShortcutHintOverlay::paint() draws the label into.
        const auto text = e.bounds.withTrimmedLeft(e.cap.getRight() - e.bounds.getX() + 6).withTrimmedRight(8);
        EXPECT_GE((float)text.getWidth(), juce::GlyphArrangement::getStringWidth(font, e.label))
            << "label '" << e.label << "' would be clipped";
    }
    EXPECT_GE(pills, 3);
    EXPECT_TRUE(sawTimeline);

    overlay->modifierKeysChanged(juce::ModifierKeys());
    setDockOpen(mc, true);
    mc.setLookAndFeel(nullptr);
}
