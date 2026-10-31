// One readout per mod dot gesture: while a mod dot drag or key step shows its "LFO 1 . +42%" readout, no hover tooltip
// of the knob (or anything the pointer passes over) shows beside it, and the knob's own hover tooltip is back as soon
// as the readout goes. A real AppTooltipWindow decides what a hover would show.

#include "ModDotTestFixture.h"
#include "UI/Layout/AppTooltipWindow.h"

namespace {

using synth::ui::AppTooltipWindow;

// juce gives no hover tip outside the foreground app, and a headless Linux test process never is. A hidden automation
// process counts as foreground, so this lets the real tip path run on every platform.
struct CountsAsForeground {
    CountsAsForeground() { juce::ComponentPeer::hideWindowsForAutomation = true; }
    ~CountsAsForeground() { juce::ComponentPeer::hideWindowsForAutomation = previous; }
    const bool previous = juce::ComponentPeer::hideWindowsForAutomation;
};

// How many readouts the user sees now: the mod dot's own, plus every hover tip the tooltip window would show for the
// components the pointer is over or passes over during the gesture (the knob the dot sits on, the dot's Tab-stop
// button, and the card around them).
int visibleReadouts(Fixture& f, AppTooltipWindow& window) {
    const int readout = f.editor->getModDot().getTooltip().isShown() ? 1 : 0;
    bool hoverTip = false; // the pointer is over one component at a time, so any hover tip is one tooltip
    for (juce::Component* over : {static_cast<juce::Component*>(f.gainKnob),
                                  static_cast<juce::Component*>(f.vcaCard->getModDotButton(f.gainChannel)),
                                  static_cast<juce::Component*>(f.vcaCard)})
        if (over != nullptr && window.getTipFor(*over).isNotEmpty())
            hoverTip = true;
    return readout + (hoverTip ? 1 : 0);
}

} // namespace

// Regression test for FRO1189: a second tooltip (the knob's hover tip) showed under the knob next to the mod dot's
// readout while the dot was dragged.
TEST_F(ModuleComponentTest, DraggingTheDotShowsExactlyOneReadoutAndTheKnobsOwnTipComesBack) {
    CountsAsForeground foreground;
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    AppTooltipWindow window(f.editor.get());
    ASSERT_TRUE(window.getTipFor(*f.gainKnob).isNotEmpty()) << "the knob has a hover tip when nothing else is up";
    EXPECT_EQ(visibleReadouts(f, window), 1) << "only the knob's hover tip before the gesture";

    const auto dot = f.dotInKnob();
    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, dot, kPlain));
    for (const int dy : {-4, -12, 6, -30}) {
        f.gainKnob->mouseDrag(makeModuleClickWithMods(*f.gainKnob, dot + juce::Point<int>(0, dy), kPlain));
        EXPECT_EQ(visibleReadouts(f, window), 1) << "drag step " << dy;
    }
    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, dot, kPlain));

    EXPECT_FALSE(f.editor->getModDot().getTooltip().isShown());
    EXPECT_TRUE(window.getTipFor(*f.gainKnob).isNotEmpty()) << "the knob's normal hover tooltip appears afterwards";
}

TEST_F(ModuleComponentTest, SteppingTheDotWithTheKeyboardShowsExactlyOneReadout) {
    CountsAsForeground foreground;
    Fixture f;
    auto* button = f.vcaCard->getModDotButton(f.gainChannel);
    ASSERT_NE(button, nullptr);
    AppTooltipWindow window(f.editor.get());

    EXPECT_TRUE(button->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(visibleReadouts(f, window), 1);
    EXPECT_TRUE(button->keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(visibleReadouts(f, window), 1);

    f.editor->getModDot().getTooltip().hide(); // what the key step's hide timer does
    EXPECT_TRUE(window.getTipFor(*f.gainKnob).isNotEmpty());
}

TEST(AppTooltipWindowBlock, BlocksAreCountedSoTwoReadoutsNeedTwoReleases) {
    juce::Component host;
    AppTooltipWindow window(&host);
    EXPECT_FALSE(AppTooltipWindow::hoverTipsBlocked());
    AppTooltipWindow::blockHoverTips(true);
    AppTooltipWindow::blockHoverTips(true);
    AppTooltipWindow::blockHoverTips(false);
    EXPECT_TRUE(AppTooltipWindow::hoverTipsBlocked());
    AppTooltipWindow::blockHoverTips(false);
    EXPECT_FALSE(AppTooltipWindow::hoverTipsBlocked());
    AppTooltipWindow::blockHoverTips(false); // an unmatched release never goes below zero
    AppTooltipWindow::blockHoverTips(true);
    EXPECT_TRUE(AppTooltipWindow::hoverTipsBlocked());
    AppTooltipWindow::blockHoverTips(false);
}
