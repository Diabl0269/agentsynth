// Synthetic input travels the ComponentPeer path: a click reaches the right child's real mouseDown/mouseUp, a drag
// delivers mouseDrag, a key reaches a keyPressed handler -- and the OS cursor never moves.

#include "TestMode/TestModeInput.h"
#include "TestModeRig.h"

using namespace synth::testmode;
using testmode_rig::Rig;

TEST(TestModeInput, KeyTextBecomesAKeyPress) {
    juce::String error;
    const auto undo = keyPressFromText("cmd+z", error);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(undo.getKeyCode(), static_cast<int>('Z'));
    EXPECT_TRUE(undo.getModifiers().isCommandDown());

    EXPECT_EQ(keyPressFromText("space", error).getKeyCode(), static_cast<int>(juce::KeyPress::spaceKey));
    const auto back = keyPressFromText("shift+tab", error);
    EXPECT_EQ(back.getKeyCode(), static_cast<int>(juce::KeyPress::tabKey));
    EXPECT_TRUE(back.getModifiers().isShiftDown());
    EXPECT_EQ(keyPressFromText("esc", error).getKeyCode(), static_cast<int>(juce::KeyPress::escapeKey));
    EXPECT_EQ(keyPressFromText("up", error).getKeyCode(), static_cast<int>(juce::KeyPress::upKey));
    EXPECT_TRUE(error.isEmpty());

    // A plain character is typed as itself, so a text field would receive it.
    EXPECT_EQ(keyPressFromText("a", error).getTextCharacter(), L'a');
}

TEST(TestModeInput, BadKeyAndModifierTextIsAnError) {
    juce::String error;
    keyPressFromText("", error);
    EXPECT_TRUE(error.isNotEmpty());
    error.clear();
    keyPressFromText("hyper+z", error);
    EXPECT_NE(error.indexOf("hyper"), -1);
    error.clear();
    keyPressFromText("cmd+nonsensekey", error);
    EXPECT_TRUE(error.isNotEmpty());
}

TEST(TestModeInput, ModifierNamesMapToModifierKeys) {
    juce::String error;
    const auto mods = modifiersFromNames({"cmd", "shift", "alt", "ctrl"}, error);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_TRUE(mods.isCommandDown() && mods.isShiftDown() && mods.isAltDown() && mods.isCtrlDown());
}

TEST(TestModeInput, AClickReachesTheChildUnderThePoint) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    juce::String error;
    ASSERT_TRUE(click(rig.right, centreOf(rig.right), Button::left, 1, juce::ModifierKeys(), error)) << error;
    EXPECT_EQ(rig.right.downs, 1);
    EXPECT_EQ(rig.right.ups, 1);
    EXPECT_EQ(rig.left.downs, 0);
    EXPECT_TRUE(rig.right.downMods.isLeftButtonDown());
}

TEST(TestModeInput, ModifiersAndTheRightButtonArriveWithTheClick) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    juce::String error;
    ASSERT_TRUE(
        click(rig.left, {20, 20}, Button::right, 1, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), error));
    EXPECT_EQ(rig.left.downs, 1);
    EXPECT_TRUE(rig.left.downMods.isRightButtonDown());
    EXPECT_TRUE(rig.left.downMods.isShiftDown());
}

TEST(TestModeInput, TwoClicksCountAsADoubleClick) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    juce::String error;
    ASSERT_TRUE(click(rig.left, centreOf(rig.left), Button::left, 2, juce::ModifierKeys(), error));
    EXPECT_EQ(rig.left.downs, 2);
    EXPECT_EQ(rig.left.doubleClicks, 1);
}

TEST(TestModeInput, AClickDoesNotMoveTheRealCursor) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto before = juce::Desktop::getInstance().getMousePosition();
    juce::String error;
    ASSERT_TRUE(click(rig.left, centreOf(rig.left), Button::left, 1, juce::ModifierKeys(), error));
    EXPECT_EQ(juce::Desktop::getInstance().getMousePosition(), before);
}

TEST(TestModeInput, ADragDeliversMouseDragToThePressedChild) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    juce::String error;
    MouseStep step{&rig.left, {50, 50}, juce::ModifierKeys(), false, Button::left};
    ASSERT_TRUE(sendMouse(step, error));
    step.buttonDown = true;
    ASSERT_TRUE(sendMouse(step, error));
    for (int i = 1; i <= 6; ++i) {
        step.local = {50 + i * 20, 50};
        ASSERT_TRUE(sendMouse(step, error));
    }
    step.buttonDown = false;
    ASSERT_TRUE(sendMouse(step, error));
    EXPECT_GE(rig.left.drags, 3);
    EXPECT_EQ(rig.left.ups, 1);
    EXPECT_EQ(rig.right.downs, 0);
}

TEST(TestModeInput, AKeyReachesAKeyPressedHandler) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    rig.left.setWantsKeyboardFocus(true);
    rig.left.grabKeyboardFocus();
    juce::String error;
    bool handled = false;
    ASSERT_TRUE(pressKey(rig.left, keyPressFromText("x", error), handled, error)) << error;
    // The focused probe takes it; if this environment gave the window no focus, the window's own handler does.
    EXPECT_TRUE(handled);
    EXPECT_EQ(rig.left.keys + rig.keys, 1);
}

TEST(TestModeInput, ANonNativeComponentIsAnError) {
    juce::Component loose;
    loose.setSize(10, 10);
    juce::String error;
    EXPECT_FALSE(sendMouse({&loose, {1, 1}, juce::ModifierKeys(), true, Button::left}, error));
    EXPECT_NE(error.indexOf("native"), -1);
    bool handled = false;
    EXPECT_FALSE(pressKey(loose, juce::KeyPress('a'), handled, error));
}

TEST(TestModeInput, AClickOutsideTheWindowIsAnErrorNotASilentNoOp) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    juce::String error;
    EXPECT_FALSE(click(rig.right, {2000, 2000}, Button::left, 1, juce::ModifierKeys(), error));
    EXPECT_NE(error.indexOf("outside the window"), -1);
    EXPECT_EQ(rig.right.downs, 0);
}

// The window-order patch covers the macOS and Windows peers; the Linux peer still hit-tests by stacking order.
#if JUCE_MAC || JUCE_WINDOWS
TEST(TestModeInput, AWindowBehindAnotherStillReceivesTheClick) {
    Rig behind;
    if (!behind.show())
        GTEST_SKIP() << "no native window in this environment";
    Rig front; // same size, same position, shown after, so it covers `behind`
    front.show();
    front.toFront(false);
    testmode_rig::pump(100);
    juce::String error;
    ASSERT_TRUE(click(behind.right, centreOf(behind.right), Button::left, 1, juce::ModifierKeys(), error)) << error;
    EXPECT_EQ(behind.right.downs, 1);
    EXPECT_EQ(front.right.downs, 0);
}
#endif
