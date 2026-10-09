#include "MotionStep.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Topic: the module library's help popover fades out when its own close asks it to, and a detached panel window fades
// in and out like any other popup window (docs/layout/animation.md#fading-things-in-and-out, #popup-windows).
// Headless: the animated path is forced with FadeAnimateGuard / PopupMotion::setAnimateOffScreenForTest and stepped by
// hand (or by pumping the message loop for a window).

namespace {
using synth::ui::AnimationMode;
using synth::ui::DetachedPanelWindow;
using synth::ui::PopupMotion;

// Reaches showHelpPopover() (protected) without launching a real call-out box.
class HelpLibrary : public ModuleLibraryComponent {
public:
    void show() { showHelpPopover(); }

protected:
    void launchHelpCallOutBox() override {}
};
} // namespace

TEST(HelpPopoverClose, ThePinnedPanelFadesOutBeforeItLeavesAndIsStillPinnedWhileItDoes) {
    FadeAnimateGuard guard;
    HelpLibrary library;
    library.setSize(200, 600);
    auto* popup = library.createHelpPopupForTest();
    library.setHelpPopoverPinnedForTest(true);
    ASSERT_NE(popup->getParentComponent(), nullptr);

    library.closeHelpPopoverForTest();
    EXPECT_TRUE(popup->isVisible()) << "it stays on screen while it fades";
    EXPECT_NE(popup->getParentComponent(), nullptr);
    EXPECT_TRUE(library.isHelpPopoverPinnedForTest());
    EXPECT_FALSE(interceptsClicks(*popup)) << "a leaving panel takes no clicks";

    stepMotion(0.5f);
    EXPECT_GT(popup->getAlpha(), 0.0f);
    EXPECT_LT(popup->getAlpha(), 1.0f);

    stepMotion(1.0f);
    EXPECT_FALSE(popup->isVisible());
    EXPECT_EQ(popup->getParentComponent(), nullptr);
    EXPECT_FALSE(library.isHelpPopoverPinnedForTest());
    EXPECT_FLOAT_EQ(popup->getAlpha(), 1.0f);
}

TEST(HelpPopoverClose, OpeningItAgainMidFadeTurnsBackFromTheCurrentOpacity) {
    FadeAnimateGuard guard;
    HelpLibrary library;
    library.setSize(200, 600);
    auto* popup = library.createHelpPopupForTest();
    library.setHelpPopoverPinnedForTest(true);

    library.closeHelpPopoverForTest();
    stepMotion(0.5f);
    const float mid = popup->getAlpha();
    ASSERT_GT(mid, 0.0f);

    library.show(); // still pinned, so it is the panel that comes back
    EXPECT_NEAR(popup->getAlpha(), mid, 0.01f);
    stepMotion(1.0f);
    EXPECT_TRUE(popup->isVisible());
    EXPECT_FLOAT_EQ(popup->getAlpha(), 1.0f);
    EXPECT_NE(popup->getParentComponent(), nullptr);
    EXPECT_TRUE(library.isHelpPopoverPinnedForTest());
}

TEST(HelpPopoverClose, ReduceMotionFadesAndOffAndOffScreenCloseAtOnce) {
    {
        FadeAnimateGuard reduced(AnimationMode::reduced);
        HelpLibrary library;
        library.setSize(200, 600);
        auto* popup = library.createHelpPopupForTest();
        library.setHelpPopoverPinnedForTest(true);
        library.closeHelpPopoverForTest();
        EXPECT_TRUE(popup->isVisible()) << "Reduce Motion is a short fade, not a jump";
        stepMotion(1.0f);
        EXPECT_FALSE(popup->isVisible());
    }
    {
        FadeAnimateGuard off(AnimationMode::off);
        HelpLibrary library;
        library.setSize(200, 600);
        auto* popup = library.createHelpPopupForTest();
        library.setHelpPopoverPinnedForTest(true);
        library.closeHelpPopoverForTest();
        EXPECT_FALSE(popup->isVisible());
        EXPECT_EQ(popup->getParentComponent(), nullptr);
    }
    HelpLibrary library;
    library.setSize(200, 600);
    auto* popup = library.createHelpPopupForTest();
    library.setHelpPopoverPinnedForTest(true);
    library.closeHelpPopoverForTest(); // not on screen and not forced
    EXPECT_FALSE(popup->isVisible());
    EXPECT_EQ(popup->getParentComponent(), nullptr);
}

TEST(DetachedPanelWindowMotion, TheWindowFadesInAndOutLikeAnyPopupAndNeverSlides) {
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        GTEST_SKIP() << "no display available";
    juce::Component panel;
    juce::DrawableButton button{"detach", juce::DrawableButton::ImageFitted};
    juce::Label title;
    ShortcutManager shortcuts;
    DetachedPanelWindow window(panel, button, title, "motionPolishWindowBounds", nullptr, nullptr, &shortcuts);
    EXPECT_TRUE(window.getProperties().contains("synthPopupMotion")) << "the window is attached to PopupMotion";

    int closes = 0;
    window.onCloseRequested = [&] { ++closes; };
    window.closeButtonPressed();
    EXPECT_EQ(closes, 1) << "a window that is not on screen closes before the call returns";

    PopupMotion::setAnimateOffScreenForTest(true);
    window.setVisible(true);
    window.closeButtonPressed();
    EXPECT_EQ(closes, 1) << "the live window fades out first";
    EXPECT_TRUE(PopupMotion::isDismissing(window));
    window.closeButtonPressed(); // a second press while it fades does nothing
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (closes < 2 && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    PopupMotion::setAnimateOffScreenForTest(false);
    EXPECT_EQ(closes, 2) << "the host redocks one turn after the fade ends";
}
