// PointerStateTests.cpp -- bug kind harness-mode-divergence: every pointer-position and live-modifier read in
// Source/ goes through UI/Layout/PointerState.h, and test-mode input feeds that seam on every synthetic event, so a
// test-mode click or drag behaves like a real hand's for popups, menus and drags that read the cursor or held keys.

#include "../UI/SourceScan.h"
#include "TestMode/TestModeInput.h"
#include "TestModeRig.h"
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/PointerState.h"
#include "UI/Layout/PopoverPointer.h"

#include <regex>

using namespace synth::testmode;
using testmode_rig::Rig;

namespace {

// Reads the seam from inside the mouse and key handlers, which is where the app's own code reads it.
struct SeamProbe : juce::Component {
    juce::Point<int> pointerAtDown;
    juce::ModifierKeys modsAtDown, modsAtKey;
    bool buttonAtDown = false, draggingAtDrag = false;
    void mouseDown(const juce::MouseEvent&) override {
        pointerAtDown = synth::ui::pointerPosition();
        modsAtDown = synth::ui::currentModifiers();
        buttonAtDown = synth::ui::isAnyMouseButtonDown();
    }
    void mouseDrag(const juce::MouseEvent&) override { draggingAtDrag = synth::ui::isPointerDragging(); }
    bool keyPressed(const juce::KeyPress&) override {
        modsAtKey = synth::ui::currentModifiers();
        return true;
    }
};

} // namespace

TEST(PointerState, ReturnsTheRealValuesWhenNothingIsSet) {
    synth::ui::clearSyntheticPointer();
    EXPECT_FALSE(synth::ui::syntheticPointerPosition().has_value());
    EXPECT_EQ(synth::ui::pointerPosition(), juce::Desktop::getMousePosition());
    EXPECT_EQ(synth::ui::currentModifiers().getRawFlags(),
              juce::ModifierKeys::getCurrentModifiersRealtime().getRawFlags());
}

TEST(PointerState, TheSyntheticPointerWinsUntilItIsCleared) {
    synth::ui::setSyntheticPointer({123, 456}, juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    EXPECT_EQ(synth::ui::pointerPosition(), juce::Point<int>(123, 456));
    EXPECT_TRUE(synth::ui::currentModifiers().isShiftDown());
    EXPECT_FALSE(synth::ui::isAnyMouseButtonDown());
    EXPECT_FALSE(synth::ui::isPointerDragging());
    synth::ui::setSyntheticPointer({1, 2}, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier));
    EXPECT_TRUE(synth::ui::isAnyMouseButtonDown());
    EXPECT_TRUE(synth::ui::isPointerDragging());
    synth::ui::clearSyntheticPointer();
    EXPECT_EQ(synth::ui::pointerPosition(), juce::Desktop::getMousePosition());
}

TEST(PointerState, PopoverAndContextMenuPlacementReadTheSyntheticPointer) {
    synth::ui::setSyntheticPointer({321, 222});
    EXPECT_EQ(synth::ui::popoverPointerPosition(), juce::Point<int>(321, 222));
    EXPECT_EQ(synth::ui::contextMenuOptionsAtPointer().getTargetScreenArea().getPosition(), juce::Point<int>(321, 222));
    EXPECT_EQ(synth::ui::contextMenuOptions(std::nullopt).getTargetScreenArea().getPosition(),
              juce::Point<int>(321, 222));
    synth::ui::clearSyntheticPointer();
}

TEST(PointerState, ScopedModifiersOverrideOnlyTheModifiersAndRestoreThem) {
    synth::ui::setSyntheticPointer({5, 6}, juce::ModifierKeys(juce::ModifierKeys::altModifier));
    {
        const synth::ui::ScopedSyntheticModifiers held{juce::ModifierKeys(juce::ModifierKeys::commandModifier)};
        EXPECT_TRUE(synth::ui::currentModifiers().isCommandDown());
        EXPECT_FALSE(synth::ui::currentModifiers().isAltDown());
        EXPECT_EQ(synth::ui::pointerPosition(), juce::Point<int>(5, 6));
    }
    EXPECT_TRUE(synth::ui::currentModifiers().isAltDown());
    synth::ui::clearSyntheticPointer();
}

TEST(PointerState, ATestModeClickPutsThePointerAndModifiersWhereTheAppReadsThem) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    SeamProbe probe;
    rig.addAndMakeVisible(probe);
    probe.setBounds(10, 10, 100, 100);
    const auto realBefore = juce::Desktop::getMousePosition();
    juce::String error;
    ASSERT_TRUE(click(probe, {30, 40}, Button::left, 1, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), error))
        << error;
    EXPECT_EQ(probe.pointerAtDown, probe.localPointToGlobal(juce::Point<int>(30, 40)));
    EXPECT_TRUE(probe.modsAtDown.isShiftDown());
    EXPECT_TRUE(probe.buttonAtDown) << "a held synthetic button reads as held";
    EXPECT_EQ(synth::ui::pointerPosition(), probe.localPointToGlobal(juce::Point<int>(30, 40)));
    EXPECT_FALSE(synth::ui::isAnyMouseButtonDown()) << "released after the click";
    EXPECT_EQ(juce::Desktop::getMousePosition(), realBefore) << "the OS cursor is untouched";
    // A drag reads as dragging, a click as not.
    MouseStep step{&probe, {30, 40}, juce::ModifierKeys(), true, Button::left};
    ASSERT_TRUE(sendMouse(step, error));
    step.local = {60, 40};
    ASSERT_TRUE(sendMouse(step, error));
    EXPECT_TRUE(probe.draggingAtDrag);
    step.buttonDown = false;
    ASSERT_TRUE(sendMouse(step, error));
    EXPECT_FALSE(synth::ui::isPointerDragging());
}

TEST(PointerState, ASyntheticKeyCarriesItsModifiersToTheHandlerAndThenLetsGo) {
    // Goes through the seam directly, not a native window: which component a peer hands a key to depends on the OS
    // window focus, which differs on Linux and Windows runners.
    SeamProbe probe;
    synth::ui::setSyntheticPointer({7, 8});
    juce::String error;
    const auto key = keyPressFromText("cmd+z", error);
    {
        const synth::ui::ScopedSyntheticModifiers held(key.getModifiers());
        EXPECT_TRUE(probe.keyPressed(key));
    }
    EXPECT_TRUE(probe.modsAtKey.isCommandDown());
    EXPECT_FALSE(synth::ui::currentModifiers().isCommandDown()) << "the key is not held after it was handled";
    synth::ui::clearSyntheticPointer();
}

// Guard (empty baseline, never an allow-list): the raw OS reads live in PointerState.cpp and nowhere else in Source/.
TEST(PointerStateGuard, NoSourceFileReadsTheOsPointerOrLiveModifiersDirectly) {
    static const std::regex raw(R"(getMousePosition|getCurrentModifiersRealtime|)"
                                R"(getMainMouseSource\s*\(\s*\)\s*\.\s*(?:isDragging|getScreenPosition)|)"
                                R"(isKeyCurrentlyDown)");
    for (const auto& [file, text] : synth::test::source_scan::sourceTexts()) {
        if (file == "Source/UI/Layout/PointerState.cpp")
            continue;
        EXPECT_FALSE(std::regex_search(text, raw))
            << file << " reads the OS pointer or live modifiers directly: use synth::ui::pointerPosition, "
            << "currentModifiers, isAnyMouseButtonDown, isPointerDragging or isKeyDown (UI/Layout/PointerState.h), "
            << "which test mode feeds, so a synthetic click behaves like a real hand's";
    }
}
