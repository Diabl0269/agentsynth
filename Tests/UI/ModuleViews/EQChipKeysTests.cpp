// EQChipKeysTests.cpp -- the EQ band chips and the curve are two focus stops of one control, so a key does the same
// thing on either. A table over every EQ action id (so a new EQ action is covered the day it is added) presses its
// default key, through the window's own key dispatch, once with the curve focused and once with a band chip focused,
// and compares what each did to the bands, the selection and the solo. Plain Left/Right keep walking the chips; the +
// chip passes on only the add key; a held Alt+arrow from a chip glides as one undo step.
#include "../Layout/NativeWindowTestGuard.h"
#include "../Layout/RealKeyDelivery.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/ParametricEQModule.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/KeyGlide/KeyHold.h"
#include "UI/Layout/PopupHost.h"
#include "UI/ModuleViews/EQBandStrip.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/EQWindow.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include <sstream>
#include <vector>

namespace {

constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kShift = juce::ModifierKeys::shiftModifier;

juce::KeyPress keyOf(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

// Slot and frequency: the chips read 4, 2, 1, 3 left to right, so a chip's slot is never its place in the row.
const std::vector<std::pair<int, float>> kBands = {{0, 1000.0f}, {1, 300.0f}, {2, 3000.0f}, {3, 100.0f}};

struct WindowRig {
    WindowRig() {
        auto* module = new ParametricEQModule();
        eq = module;
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(module));
        for (const auto& [slot, freq] : kBands) {
            eq->setBandFreq(slot, freq);
            eq->setBandGain(slot, 0.0f);
            eq->setBandEnabled(slot, true);
        }
        window = std::make_unique<EQWindow>(*eq);
        window->setUndoManager(&undo);
        window->setGestureCallbacks(
            [this] {
                ++starts;
                undo.captureBeforeState(engine.getGraph());
            },
            [this] {
                ++ends;
                undo.pushSnapshotFromCapture(engine.getGraph());
            });
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
    juce::Component* chip(int band) { return strip().getChip(band); }

    // Presses `key` with `focus` holding keyboard focus; false when the window did not hand it over.
    bool press(juce::Component& focus, const juce::KeyPress& key) {
        if (!synth::test::real_keys::takeFocus(focus))
            return false;
        synth::test::real_keys::deliver(focus, key);
        curve().timerCallback();
        return true;
    }

    // Everything a band action can change: every slot's state, the selection and the solo.
    std::string state() {
        std::ostringstream out;
        for (int b = 0; b < ParametricEQModule::kMaxBands; ++b) {
            const auto s = eq->getBandSnapshots()[(size_t)b];
            if (!s.enabled)
                continue;
            out << b << ":" << s.freqHz << "/" << s.gainDb << "/" << s.q << "/" << (int)s.type << " ";
        }
        out << "sel";
        for (const int b : curve().getSelectedBands())
            out << " " << b;
        out << " cursor " << curve().getSelectedBand() << " solo " << eq->getSoloBand();
        return out.str();
    }

    AudioEngine engine;
    AppUndoManager undo;
    juce::AudioProcessorGraph::Node::Ptr node;
    ParametricEQModule* eq = nullptr; // owned by the graph
    juce::Component host;
    std::unique_ptr<EQWindow> window;
    const char* skip = nullptr;
    int starts = 0;
    int ends = 0;
};

// Opens nothing: captures what launchPopover would have shown (the type menu) and where.
struct PopoverCapture {
    PopoverCapture() {
        synth::ui::test_hooks::popoverLauncherForTest() = [this](std::unique_ptr<juce::Component> content,
                                                                 juce::Rectangle<int> area) -> juce::CallOutBox* {
            ++opened;
            lastArea = area;
            content.reset();
            return nullptr;
        };
    }
    ~PopoverCapture() { synth::ui::test_hooks::popoverLauncherForTest() = nullptr; }
    int opened = 0;
    juce::Rectangle<int> lastArea;
};

struct Outcome {
    std::string state;
    int popovers = 0;
    juce::Rectangle<int> popoverArea;
};

// One key from the curve (chip < 0) or from `chip`'s focus, on a fresh window with `band` the band it is about.
Outcome pressOn(const juce::KeyPress& key, int band, bool fromChip, bool& ran) {
    WindowRig rig;
    ran = false;
    if (rig.skip != nullptr)
        return {};
    PopoverCapture capture;
    // From the chip the selection starts on another band: the chip's own band must be the one the key acts on.
    rig.curve().setSelectedBand(fromChip ? (band + 1) % 4 : band);
    juce::Component* target = fromChip ? rig.chip(band) : static_cast<juce::Component*>(&rig.curve());
    if (target == nullptr || !rig.press(*target, key))
        return {};
    ran = true;
    return {rig.state(), capture.opened, capture.lastArea};
}

} // namespace

// Every EQ action's default key acts on the chip's band exactly as from the curve, from each of the four chips.
TEST(EQChipKeys, EveryEqActionActsOnTheChipsBandExactlyAsFromTheCurve) {
    ShortcutManager manager;
    const auto ids = ShortcutManager::getActionIdsInCategory(ShortcutCategory::EQ);
    ASSERT_GE(ids.size(), 13);
    for (const auto& id : ids) {
        // Plain Left/Right (the walk) and Backspace (deletes the chip's own band) are the chip's own keys, tested
        // below; every other action must reach the band.
        if (id == "eqSelectPrev" || id == "eqSelectNext" || id == "eqRemoveBand")
            continue;
        const auto binding = manager.getBinding(id);
        ASSERT_TRUE(binding.isValid()) << id;
        for (int band = 0; band < 4; ++band) {
            bool ranCurve = false, ranChip = false;
            const auto fromCurve = pressOn(binding, band, false, ranCurve);
            const auto fromChip = pressOn(binding, band, true, ranChip);
            if (!ranCurve || !ranChip)
                GTEST_SKIP() << "needs a display and a native window for real key delivery";
            EXPECT_EQ(fromChip.state, fromCurve.state) << id << " on the chip of band " << band + 1;
            EXPECT_EQ(fromChip.popovers, fromCurve.popovers) << id << " on the chip of band " << band + 1;
            if (fromCurve.popovers > 0)
                EXPECT_EQ(fromChip.popoverArea, fromCurve.popoverArea) << id << ": the menu opens at the band's dot";
            // The key must have done something on the curve, or the comparison proves nothing.
            WindowRig fresh;
            if (fresh.skip == nullptr) {
                fresh.curve().setSelectedBand(band);
                EXPECT_TRUE(fromCurve.state != fresh.state() || fromCurve.popovers > 0)
                    << id << " did nothing from the curve, so the table does not test it";
            }
        }
    }
}

// The fine and group variants of the select keys reach the curve too: Shift+Right extends the selection from a chip.
TEST(EQChipKeys, ShiftRightFromAChipExtendsTheSelectionAndFocusFollowsTheCursor) {
    WindowRig rig;
    if (rig.skip != nullptr)
        GTEST_SKIP() << rig.skip;
    rig.curve().setSelectedBand(3); // 100 Hz, the first chip
    auto* chip = rig.chip(3);
    ASSERT_NE(chip, nullptr);
    ASSERT_TRUE(rig.press(*chip, keyOf(juce::KeyPress::rightKey, kShift)));
    EXPECT_EQ(rig.curve().getSelectedBands().size(), 2u);
    EXPECT_EQ(rig.curve().getSelectedBand(), 1) << "the cursor went on to the 300 Hz band";
    ASSERT_NE(rig.chip(1), nullptr);
    EXPECT_TRUE(rig.chip(1)->hasKeyboardFocus(false)) << "the keys follow it, so the next press has a target";
}

// Plain Left/Right still walk the chips (and select), never the curve's own select action twice.
TEST(EQChipKeys, PlainArrowsOnAChipStillWalkTheChips) {
    WindowRig rig;
    if (rig.skip != nullptr)
        GTEST_SKIP() << rig.skip;
    rig.curve().setSelectedBand(3);
    ASSERT_TRUE(rig.press(*rig.chip(3), keyOf(juce::KeyPress::rightKey)));
    EXPECT_EQ(rig.curve().getSelectedBand(), 1) << "one step right: 300 Hz";
    EXPECT_TRUE(rig.chip(1)->hasKeyboardFocus(false));
    EXPECT_EQ(rig.curve().getSelectedBands().size(), 1u);
}

// Return still selects; Delete and Backspace delete the band and focus goes on to the next chip.
TEST(EQChipKeys, ReturnSelectsAndDeleteAndBackspaceRemoveTheBandAndMoveFocusToTheNextChip) {
    for (const int code : {(int)juce::KeyPress::deleteKey, (int)juce::KeyPress::backspaceKey}) {
        WindowRig rig;
        if (rig.skip != nullptr)
            GTEST_SKIP() << rig.skip;
        rig.curve().setSelectedBand(3);
        ASSERT_TRUE(rig.press(*rig.chip(1), keyOf(juce::KeyPress::returnKey)));
        EXPECT_EQ(rig.curve().getSelectedBand(), 1) << "Return on a chip selects its band";
        ASSERT_TRUE(rig.press(*rig.chip(1), keyOf(code)));
        EXPECT_FALSE(rig.eq->isBandEnabled(1));
        EXPECT_EQ(rig.curve().getSelectedBand(), 0) << "the next chip (1 kHz) takes the selection";
        ASSERT_NE(rig.chip(0), nullptr);
        EXPECT_TRUE(rig.chip(0)->hasKeyboardFocus(false));
    }
}

// The + chip hands on only the add key: Alt+arrows, S and the rest need a band and are left to the window.
TEST(EQChipKeys, ThePlusChipPassesOnOnlyTheAddKey) {
    WindowRig rig;
    if (rig.skip != nullptr)
        GTEST_SKIP() << rig.skip;
    rig.curve().setSelectedBand(1);
    const auto before = rig.state();
    auto* add = rig.strip().getAddChip();
    ASSERT_NE(add, nullptr);
    for (const auto& key : {keyOf(juce::KeyPress::rightKey, kAlt), keyOf(juce::KeyPress::upKey, kAlt), keyOf('s'),
                            keyOf(juce::KeyPress::upKey)}) {
        ASSERT_TRUE(rig.press(*add, key));
        EXPECT_EQ(rig.state(), before) << "a band key on the + chip must not act on the selected band";
    }
    ASSERT_TRUE(rig.press(*add, keyOf('+')));
    EXPECT_EQ(rig.eq->getEnabledBandCount(), 5) << "the add key adds a band";
}

// A held Alt+Right from a chip glides past the tap step and is one undo step, ended by the key-up the chip hears.
TEST(EQChipKeys, AHeldAltArrowFromAChipGlidesAndIsOneUndoStep) {
    WindowRig rig;
    if (rig.skip != nullptr)
        GTEST_SKIP() << rig.skip;
    double now = 1000.0;
    std::set<int> down;
    auto& hold = rig.curve().getKeyHoldForTest();
    hold.host.nowMs = [&now] { return now; };
    hold.host.isArrowDown = [&down](int code) { return down.count(code) > 0; };
    hold.host.isShiftDown = [] { return false; };
    hold.host.isAltDown = [] { return true; };

    auto* chip = rig.chip(1); // 300 Hz
    ASSERT_NE(chip, nullptr);
    rig.curve().setSelectedBand(0);
    const float start = rig.eq->getBandSnapshots()[1].freqHz;
    rig.starts = rig.ends = 0;
    down = {juce::KeyPress::rightKey};
    ASSERT_TRUE(rig.press(*chip, keyOf(juce::KeyPress::rightKey, kAlt)));
    ASSERT_TRUE(hold.active()) << "the press began a hold on the curve";
    const float tap = rig.eq->getBandSnapshots()[1].freqHz;
    EXPECT_GT(tap, start) << "the tap moved the chip's band, not the one that was selected";
    EXPECT_FLOAT_EQ(rig.eq->getBandSnapshots()[0].freqHz, 1000.0f);
    now = 1000.0 + 150.0 + 600.0;
    hold.tick();
    const float glided = rig.eq->getBandSnapshots()[1].freqHz;
    EXPECT_GT(glided, tap) << "holding it carries the dot on";
    down.clear();
    chip->keyStateChanged(false); // the OS reports the key-up to the focused chip
    EXPECT_FALSE(hold.active()) << "the chip's key-up ends the curve's hold";
    EXPECT_EQ(rig.starts, 1) << "the whole hold is one undo step";
    EXPECT_EQ(rig.ends, 1);
}
