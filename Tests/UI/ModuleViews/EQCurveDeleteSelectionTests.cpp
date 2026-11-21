// EQCurveDeleteSelectionTests.cpp -- where the selection goes when a band is deleted, from every way of deleting it:
// the Delete key with the curve focused and with a chip focused (real key delivery through the window's own peer), a
// double-click on the dot, and the chip's double-click and Delete band menu item. The band that takes the selection is
// the next one in the order the chips show (frequency, never the slot number), else the previous, and Cmd+Z brings back
// the band and its selection.
#include "../Layout/NativeWindowTestGuard.h"
#include "../Layout/RealKeyDelivery.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/ParametricEQModule.h"
#include "UI/ModuleViews/EQBandStrip.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/EQWindow.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace {

juce::KeyPress key(int code) { return juce::KeyPress(code, juce::ModifierKeys(), 0); }

using Bands = std::vector<std::pair<int, float>>;
// Slot and frequency, so the slot order is not the frequency order: the chips read 4, 2, 1, 3 left to right.
const Bands kScrambled = {{0, 1000.0f}, {1, 300.0f}, {2, 3000.0f}, {3, 100.0f}};

// An EQ window over a module whose bands are on in the given slots and frequencies, on the desktop so its keys arrive
// the way the OS delivers them. `skip` says why a test that needs keys cannot run here.
struct WindowRig {
    explicit WindowRig(const Bands& bands) {
        auto* module = new ParametricEQModule();
        eq = module;
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(module));
        for (const auto& [slot, freq] : bands) {
            eq->setBandFreq(slot, freq);
            eq->setBandGain(slot, 0.0f);
            eq->setBandEnabled(slot, true);
        }
        window = std::make_unique<EQWindow>(*eq);
        window->setUndoManager(&undo);
        window->setGestureCallbacks([this] { undo.captureBeforeState(engine.getGraph()); },
                                    [this] { undo.pushSnapshotFromCapture(engine.getGraph()); });
        host.setSize(820, 470);
        host.addAndMakeVisible(*window);
        window->setBounds(host.getLocalBounds());
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            skip = "needs a display";
        else if (nativeWindowsAbortHere())
            skip = "needs a native window";
        else {
            host.addToDesktop(0);
            host.setVisible(true);
        }
    }
    ~WindowRig() {
        window.reset();
        if (host.isOnDesktop())
            host.removeFromDesktop();
    }
    EQCurveComponent& curve() { return window->getCurve(); }
    synth::ui::EQBandStrip& strip() { return window->getStrip(); }
    // Presses Delete with `focus` holding keyboard focus. False when the window did not give it focus.
    bool deleteFrom(juce::Component& focus) {
        if (!synth::test::real_keys::takeFocus(focus))
            return false;
        synth::test::real_keys::deliver(focus, key(juce::KeyPress::deleteKey));
        curve().timerCallback();
        return true;
    }
    void restore(bool forward) {
        ASSERT_TRUE(forward ? undo.redo() : undo.undo());
        curve().timerCallback();
    }

    AudioEngine engine;
    AppUndoManager undo;
    juce::AudioProcessorGraph::Node::Ptr node;
    ParametricEQModule* eq = nullptr; // owned by the graph
    juce::Component host;
    std::unique_ptr<EQWindow> window;
    const char* skip = nullptr;
};

} // namespace

TEST(EQDeleteSelection, TheChipsReadInFrequencyOrderNotSlotOrder) {
    WindowRig rig(kScrambled);
    EXPECT_EQ(rig.curve().shownBandsInOrder(), (std::vector<int>{3, 1, 0, 2}));
}

// Delete on a middle band: the next chip to the right takes the selection, from the curve and from a chip.
TEST(EQDeleteSelection, DeleteOnAMiddleBandSelectsTheNextChipFromTheCurveFocus) {
    WindowRig rig(kScrambled);
    if (rig.skip != nullptr)
        GTEST_SKIP() << rig.skip;
    rig.curve().setSelectedBand(1); // chip 2 (300 Hz): its next chip is band 1 (slot 0, 1 kHz)
    ASSERT_TRUE(rig.deleteFrom(rig.curve()));
    EXPECT_FALSE(rig.eq->isBandEnabled(1));
    EXPECT_EQ(rig.curve().getSelectedBand(), 0);
    EXPECT_EQ(rig.curve().getSelectedBands(), (std::vector<int>{0}));
}

TEST(EQDeleteSelection, DeleteOnAMiddleBandSelectsTheNextChipFromAChipFocus) {
    WindowRig rig(kScrambled);
    if (rig.skip != nullptr)
        GTEST_SKIP() << rig.skip;
    rig.curve().setSelectedBand(1);
    auto* chip = rig.strip().getChip(1);
    ASSERT_NE(chip, nullptr);
    ASSERT_TRUE(rig.deleteFrom(*chip));
    EXPECT_FALSE(rig.eq->isBandEnabled(1)) << "Delete on a focused chip removes its band";
    EXPECT_EQ(rig.curve().getSelectedBand(), 0) << "and the next chip takes the selection";
    auto* next = rig.strip().getChip(0);
    ASSERT_NE(next, nullptr);
    EXPECT_TRUE(next->hasKeyboardFocus(false)) << "with the keys on it, so the next press has a target";
}

TEST(EQDeleteSelection, DeleteOnTheLastChipSelectsThePreviousOne) {
    WindowRig rig(kScrambled);
    if (rig.skip != nullptr)
        GTEST_SKIP() << rig.skip;
    rig.curve().setSelectedBand(2); // 3 kHz, the last along the frequency axis
    ASSERT_TRUE(rig.deleteFrom(rig.curve()));
    EXPECT_EQ(rig.curve().getSelectedBand(), 0) << "the previous chip (1 kHz)";
}

TEST(EQDeleteSelection, DeleteOnTheLastBandLeftSelectsNothing) {
    WindowRig rig({{5, 1000.0f}});
    if (rig.skip != nullptr)
        GTEST_SKIP() << rig.skip;
    rig.curve().setSelectedBand(5);
    ASSERT_TRUE(rig.deleteFrom(rig.curve()));
    EXPECT_FALSE(rig.eq->isBandEnabled(5));
    EXPECT_EQ(rig.curve().getSelectedBand(), -1);
}

// The mouse ways of deleting a band hand the selection on exactly like the key (the dot's double-click, and the chip's
// double-click and Delete band item, which both call removeBand).
TEST(EQDeleteSelection, ADoubleClickOnADotOrAChipSelectsTheNextChipToo) {
    WindowRig rig(kScrambled);
    rig.curve().setSelectedBand(1);
    rig.curve().removeBand(1);
    EXPECT_EQ(rig.curve().getSelectedBand(), 0);
    EXPECT_EQ(rig.curve().getSelectedBands(), (std::vector<int>{0}));
}

TEST(EQDeleteSelection, DeletingABandThatIsNotTheSelectionKeepsTheSelectionWhereItIs) {
    WindowRig rig(kScrambled);
    rig.curve().setSelectedBand(2);
    rig.curve().removeBand(3);
    EXPECT_EQ(rig.curve().getSelectedBand(), 2);
}

TEST(EQDeleteSelection, CmdZBringsBackTheBandAndItsSelectionAndRedoDeletesItAgain) {
    WindowRig rig(kScrambled);
    rig.curve().setSelectedBand(1);
    rig.curve().removeSelectedBands(true);
    ASSERT_EQ(rig.curve().getSelectedBand(), 0);
    rig.restore(false);
    EXPECT_TRUE(rig.eq->isBandEnabled(1)) << "the band is back";
    EXPECT_EQ(rig.curve().getSelectedBand(), 1) << "and selected again, so the keys have it";
    EXPECT_EQ(rig.curve().getSelectedBands(), (std::vector<int>{1}));
    rig.restore(true);
    EXPECT_FALSE(rig.eq->isBandEnabled(1));
    EXPECT_EQ(rig.curve().getSelectedBand(), 0) << "redo deletes it and hands the selection on, as the key did";
}
