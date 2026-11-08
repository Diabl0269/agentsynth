// Synthetic input travels the ComponentPeer path: a click reaches the right child's real mouseDown/mouseUp, a drag
// delivers mouseDrag, a key reaches a keyPressed handler -- and the OS cursor never moves.

#include "../UI/Layout/PopupTestCleanup.h"
#include "TestMode/TestModeInput.h"
#include "TestModeRig.h"
#include "UI/Layout/PopupHost.h"

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
    // The space bar types a space too (a scenario's "type: {text: test loop}" sends it); with Cmd it types nothing.
    EXPECT_EQ(keyPressFromText("space", error).getTextCharacter(), L' ');
    EXPECT_EQ(keyPressFromText("cmd+space", error).getTextCharacter(), 0);
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
    synth::test::waitUntil([&] { return front.isShowing() && behind.isShowing(); }, 1000);
    juce::String error;
    ASSERT_TRUE(click(behind.right, centreOf(behind.right), Button::left, 1, juce::ModifierKeys(), error)) << error;
    EXPECT_EQ(behind.right.downs, 1);
    EXPECT_EQ(front.right.downs, 0);
}
#endif

namespace {
struct TestModeInputPopover : ::testing::Test {
    void TearDown() override { synth::test::finishPopupTest(); }
};

juce::CallOutBox* firstCallOut() {
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* box = dynamic_cast<juce::CallOutBox*>(desktop.getComponent(i)))
            return box;
    return nullptr;
}

bool liveCallOutOpen() {
    auto* box = firstCallOut();
    return box != nullptr && box->isVisible() && !synth::ui::PopupMotion::isDismissing(*box);
}
} // namespace

// Regression test for FRO1526: a test-mode click never moves the OS cursor, so a popover read the cursor, took the
// press on its own button for a click away, closed, and the button then re-opened it.
TEST_F(TestModeInputPopover, AClickOnAPopoversOwnButtonClosesIt) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    synth::ui::PopoverSlot slot;
    const auto toggle = [&] {
        slot.open(
            [] {
                auto c = std::make_unique<juce::Component>();
                c->setSize(120, 60);
                return c;
            },
            rig.button.getScreenBounds(), &rig.button, {.trigger = &rig.button});
    };
    rig.button.onClick = toggle;
    toggle();
    synth::test::waitUntil([] { return firstCallOut() != nullptr && firstCallOut()->isCurrentlyModal(); }, 2000);
    auto* box = firstCallOut();
    ASSERT_NE(box, nullptr);
    ASSERT_TRUE(box->isCurrentlyModal());

    // Nothing sets the pointer, and the real cursor is not moved: only the synthetic press says where it is.
    synth::ui::test_hooks::popoverMouseForTest() = std::nullopt;
    juce::String error;
    ASSERT_TRUE(click(rig.button, centreOf(rig.button), Button::left, 1, juce::ModifierKeys(), error)) << error;

    synth::test::waitUntil([&] { return !liveCallOutOpen(); }, 2000);
    EXPECT_FALSE(liveCallOutOpen()) << "the popover is still open or opened again";
    // Give a re-open the time it would need: a popover that opens again within it fails.
    EXPECT_FALSE(synth::test::waitUntil([] { return liveCallOutOpen(); }, 300)) << "the button re-opened the popover";
}
