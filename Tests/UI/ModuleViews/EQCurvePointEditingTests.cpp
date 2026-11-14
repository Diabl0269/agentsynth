// EQCurvePointEditingTests.cpp -- the EQ curve follows the shared point-editing rules (docs/control/point-editing.md)
// through real mouse and key events: box select, Shift+arrow extend in frequency order, Cmd+A, Delete selecting the
// next band, a selection moving together, and held Alt+arrows gliding (one undo step, diagonal, fine with Shift,
// repeats ignored), plus the eased tap and Reduce Motion. The per-editor matrix (PointEditorBehaviourMatrixTests) holds
// the same behaviours against the other editors.
#include "Modules/FX/ParametricEQModule.h"
#include "UI/Layout/KeyGlide/KeyHold.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include <cmath>
#include <gtest/gtest.h>
#include <set>

namespace eq_point_editing_tests {

constexpr int kEqAlt = juce::ModifierKeys::altModifier;
constexpr int kEqShift = juce::ModifierKeys::shiftModifier;
constexpr int kEqCmd = juce::ModifierKeys::commandModifier;

juce::KeyPress eqKey(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

// Bands in slots 0, 1, 2 at 200 Hz, 1 kHz and 4 kHz; slot 5 is left off. Counts the undo steps the curve reports.
struct Rig {
    Rig() {
        const float freqs[] = {200.0f, 1000.0f, 4000.0f};
        const float gains[] = {3.0f, -4.0f, 5.0f};
        for (int b = 0; b < 3; ++b) {
            eq.setBandFreq(b, freqs[b]);
            eq.setBandGain(b, gains[b]);
            eq.setBandEnabled(b, true);
        }
        curve = std::make_unique<EQCurveComponent>(eq);
        curve->setBounds(0, 0, 400, 160);
        curve->onGestureStart = [this] { ++starts; };
        curve->onGestureEnd = [this] { ++ends; };
        auto& host = curve->getKeyHoldForTest().host;
        host.nowMs = [this] { return now; };
        host.isArrowDown = [this](int code) { return down.count(code) > 0; };
        host.isShiftDown = [this] { return shift; };
        host.isAltDown = [this] { return alt; };
    }
    ~Rig() { curve.reset(); }

    bool press(int code, int mods = 0) { return curve->keyPressed(eqKey(code, mods)); }
    ParametricEQModule::BandSnapshot band(int b) const { return eq.getBandSnapshots()[(size_t)b]; }
    juce::Point<float> dot(int b) const { return curve->bandPosition(band(b)); }

    juce::MouseEvent event(juce::Point<float> p, int mods, bool dragged, juce::Point<float> from) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(mods), 0.0f,
                                0.0f, 0.0f, 0.0f, 0.0f, curve.get(), curve.get(), juce::Time::getCurrentTime(), from,
                                juce::Time::getCurrentTime(), 1, dragged);
    }
    void drag(juce::Point<float> from, juce::Point<float> to, int mods = 0) {
        const int left = juce::ModifierKeys::leftButtonModifier | mods;
        curve->mouseDown(event(from, left, false, from));
        curve->mouseDrag(event(to, left, true, from));
        curve->mouseUp(event(to, left, true, from));
    }
    void click(juce::Point<float> at, int mods = 0) {
        const int left = juce::ModifierKeys::leftButtonModifier | mods;
        curve->mouseDown(event(at, left, false, at));
        curve->mouseUp(event(at, left, false, at));
    }
    // A box from the top edge left of band `a` to the bottom edge right of band `b`.
    void box(int a, int b, int mods = 0) {
        drag({dot(a).x - 10.0f, 2.0f}, {dot(b).x + 10.0f, (float)curve->getHeight() - 2.0f}, mods);
    }
    std::set<int> selected() const {
        const auto& s = curve->getSelectedBands();
        return {s.begin(), s.end()};
    }

    // The held-key clock: a frame `ms` after the press's tap threshold.
    void frame(double ms) {
        now = 1000.0 + 150.0 + ms;
        curve->getKeyHoldForTest().tick();
    }
    void release() {
        down.clear();
        curve->keyStateChanged(false);
    }

    ParametricEQModule eq;
    std::unique_ptr<EQCurveComponent> curve;
    int starts = 0;
    int ends = 0;
    double now = 1000.0;
    std::set<int> down;
    bool shift = false;
    bool alt = true;
};

struct FullMotion {
    FullMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
        synth::ui::setReducedMotionForTest(false);
    }
    ~FullMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

} // namespace eq_point_editing_tests

using namespace eq_point_editing_tests;

// ---- box select ----

TEST(EQCurvePointEditing, ADragOnEmptySpaceSelectsTheBandsInsideTheBox) {
    Rig rig;
    rig.box(0, 1);
    EXPECT_EQ(rig.selected(), (std::set<int>{0, 1}));
    EXPECT_EQ(rig.curve->getSelectedBand(), 1) << "the cursor is the band highest in frequency";
    EXPECT_EQ(rig.starts, 0) << "selecting is not an undo step";
}

TEST(EQCurvePointEditing, ABoxReplacesTheSelectionAndAShiftBoxAddsToIt) {
    Rig rig;
    rig.click(rig.dot(2));
    ASSERT_EQ(rig.selected(), (std::set<int>{2}));
    rig.box(0, 0);
    EXPECT_EQ(rig.selected(), (std::set<int>{0}));
    rig.box(1, 1, kEqShift);
    EXPECT_EQ(rig.selected(), (std::set<int>{0, 1}));
}

TEST(EQCurvePointEditing, ABareClickOnEmptySpaceClearsTheSelection) {
    Rig rig;
    rig.box(0, 2);
    ASSERT_EQ(rig.selected().size(), 3u);
    rig.click({(rig.dot(0).x + rig.dot(1).x) * 0.5f, 2.0f});
    EXPECT_TRUE(rig.selected().empty());
    EXPECT_EQ(rig.curve->getSelectedBand(), -1);
}

TEST(EQCurvePointEditing, ShiftClickAddsABandToTheSelectionAndAgainTakesItOut) {
    Rig rig;
    rig.click(rig.dot(0));
    rig.click(rig.dot(2), kEqShift);
    EXPECT_EQ(rig.selected(), (std::set<int>{0, 2}));
    EXPECT_EQ(rig.curve->getSelectedBand(), 2);
    rig.click(rig.dot(2), kEqShift);
    EXPECT_EQ(rig.selected(), (std::set<int>{0}));
    EXPECT_EQ(rig.starts, 0) << "a shift-click does not move the band";
}

// ---- Shift+arrows and Cmd+A ----

TEST(EQCurvePointEditing, ShiftArrowsExtendInFrequencyOrderNotSlotOrder) {
    Rig rig;
    // Slot 5 lands between the first two bands in frequency.
    rig.eq.setBandFreq(5, 500.0f);
    rig.eq.setBandGain(5, 1.0f);
    rig.eq.setBandEnabled(5, true);
    rig.curve->timerCallback();
    rig.click(rig.dot(0));
    EXPECT_TRUE(rig.press(juce::KeyPress::rightKey, kEqShift));
    EXPECT_EQ(rig.selected(), (std::set<int>{0, 5})) << "the next band up is the 500 Hz one in slot 5";
    EXPECT_EQ(rig.curve->getSelectedBand(), 5);
    rig.press(juce::KeyPress::rightKey, kEqShift);
    EXPECT_EQ(rig.selected(), (std::set<int>{0, 1, 5}));
    rig.press(juce::KeyPress::leftKey, kEqShift);
    EXPECT_EQ(rig.selected(), (std::set<int>{0, 5})) << "reversing shrinks the range back toward the anchor";
}

TEST(EQCurvePointEditing, ShiftArrowAtTheEndOfTheLineChangesNothing) {
    Rig rig;
    rig.click(rig.dot(2));
    EXPECT_TRUE(rig.press(juce::KeyPress::rightKey, kEqShift));
    EXPECT_EQ(rig.selected(), (std::set<int>{2}));
}

TEST(EQCurvePointEditing, CmdASelectsEveryBandThatIsOn) {
    Rig rig;
    rig.click(rig.dot(1));
    EXPECT_TRUE(rig.press('A', kEqCmd));
    EXPECT_EQ(rig.selected(), (std::set<int>{0, 1, 2}));
    EXPECT_EQ(rig.curve->getSelectedBand(), 1) << "a cursor already in the selection stays";
}

// ---- moving the selection ----

TEST(EQCurvePointEditing, AltArrowsMoveEverySelectedBandAsOneUndoStep) {
    Rig rig;
    rig.box(0, 1);
    rig.starts = rig.ends = 0;
    const float f0 = rig.band(0).freqHz, f1 = rig.band(1).freqHz, f2 = rig.band(2).freqHz;
    EXPECT_TRUE(rig.press(juce::KeyPress::rightKey, kEqAlt));
    EXPECT_NEAR(rig.band(0).freqHz, f0 * std::pow(2.0f, 1.0f / 12.0f), 0.1f);
    EXPECT_NEAR(rig.band(1).freqHz, f1 * std::pow(2.0f, 1.0f / 12.0f), 0.2f);
    EXPECT_FLOAT_EQ(rig.band(2).freqHz, f2) << "an unselected band stays put";
    EXPECT_EQ(rig.starts, 1);
    EXPECT_EQ(rig.ends, 1);
    rig.press(juce::KeyPress::upKey, kEqAlt);
    EXPECT_NEAR(rig.band(0).gainDb, 4.0f, 0.01f);
    EXPECT_NEAR(rig.band(1).gainDb, -3.0f, 0.01f);
}

TEST(EQCurvePointEditing, DraggingOneOfSeveralSelectedBandsMovesThemAllByTheSameAmount) {
    Rig rig;
    rig.box(0, 1);
    rig.starts = rig.ends = 0;
    const auto g0 = rig.band(0).gainDb, g1 = rig.band(1).gainDb;
    const auto from = rig.dot(1);
    rig.drag(from, from + juce::Point<float>(0.0f, -20.0f));
    EXPECT_EQ(rig.selected(), (std::set<int>{0, 1})) << "the drag keeps the group";
    const float up0 = rig.band(0).gainDb - g0, up1 = rig.band(1).gainDb - g1;
    EXPECT_GT(up1, 0.0f);
    EXPECT_NEAR(up0, up1, 0.01f);
    EXPECT_EQ(rig.starts, 1);
    EXPECT_EQ(rig.ends, 1);
}

TEST(EQCurvePointEditing, AClickOnABandOfAGroupWithoutDraggingPicksJustThatBand) {
    Rig rig;
    rig.box(0, 2);
    rig.click(rig.dot(1));
    EXPECT_EQ(rig.selected(), (std::set<int>{1}));
}

// ---- delete ----

TEST(EQCurvePointEditing, DeleteSwitchesTheSelectionOffAsOneStepAndSelectsTheNextBand) {
    Rig rig;
    rig.click(rig.dot(0));
    rig.starts = rig.ends = 0;
    EXPECT_TRUE(rig.press(juce::KeyPress::deleteKey));
    EXPECT_FALSE(rig.eq.isBandEnabled(0));
    EXPECT_EQ(rig.selected(), (std::set<int>{1})) << "the band after it in frequency";
    EXPECT_EQ(rig.starts, 1);
    EXPECT_EQ(rig.ends, 1);

    rig.box(1, 2);
    rig.starts = rig.ends = 0;
    rig.press(juce::KeyPress::deleteKey);
    EXPECT_EQ(rig.eq.getEnabledBandCount(), 0);
    EXPECT_TRUE(rig.selected().empty());
    EXPECT_EQ(rig.starts, 1) << "several bands are one undo step";
}

TEST(EQCurvePointEditing, DeletingTheLastBandSelectsTheLastOneLeft) {
    Rig rig;
    rig.click(rig.dot(2));
    rig.press(juce::KeyPress::deleteKey);
    EXPECT_EQ(rig.selected(), (std::set<int>{1}));
}

TEST(EQCurvePointEditing, BackspaceSwitchesTheSelectionOffAndKeepsItSelected) {
    Rig rig;
    rig.box(0, 1);
    rig.press(juce::KeyPress::backspaceKey);
    EXPECT_FALSE(rig.eq.isBandEnabled(0));
    EXPECT_FALSE(rig.eq.isBandEnabled(1));
    EXPECT_EQ(rig.selected(), (std::set<int>{0, 1}));
}

// ---- held arrows ----

TEST(EQCurvePointEditing, AHeldAltArrowGlidesPastATapAndIsOneUndoStep) {
    Rig rig;
    rig.click(rig.dot(1));
    rig.starts = rig.ends = 0;
    const float f0 = rig.band(1).freqHz;
    rig.down = {juce::KeyPress::rightKey};
    EXPECT_TRUE(rig.press(juce::KeyPress::rightKey, kEqAlt));
    auto& hold = rig.curve->getKeyHoldForTest();
    ASSERT_TRUE(hold.active());
    const float tap = rig.band(1).freqHz;
    EXPECT_NEAR(tap, f0 * std::pow(2.0f, 1.0f / 12.0f), 0.2f) << "the press is one step";
    rig.frame(300.0);
    const float glided = rig.band(1).freqHz;
    EXPECT_GT(glided, tap);
    rig.frame(900.0);
    EXPECT_GT(rig.band(1).freqHz, glided) << "it keeps gliding while the key is down";
    EXPECT_EQ(rig.starts, 1);
    EXPECT_EQ(rig.ends, 0) << "the step stays open until the key goes up";
    rig.release();
    EXPECT_FALSE(hold.active());
    EXPECT_EQ(rig.ends, 1);
    const float rest = rig.band(1).freqHz;
    rig.frame(2000.0);
    EXPECT_FLOAT_EQ(rig.band(1).freqHz, rest) << "release stops it";
}

TEST(EQCurvePointEditing, KeyRepeatDoesNotAddSteps) {
    Rig rig;
    rig.click(rig.dot(1));
    rig.starts = 0;
    rig.down = {juce::KeyPress::rightKey};
    rig.press(juce::KeyPress::rightKey, kEqAlt);
    const float after = rig.band(1).freqHz;
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(rig.press(juce::KeyPress::rightKey, kEqAlt)) << "an OS repeat is taken and ignored";
    EXPECT_FLOAT_EQ(rig.band(1).freqHz, after);
    EXPECT_EQ(rig.starts, 1);
    rig.release();
}

TEST(EQCurvePointEditing, TwoHeldArrowsMoveAGroupDiagonally) {
    Rig rig;
    rig.box(0, 1);
    rig.starts = rig.ends = 0;
    rig.down = {juce::KeyPress::rightKey};
    rig.press(juce::KeyPress::rightKey, kEqAlt);
    rig.frame(300.0);
    const float g0 = rig.band(0).gainDb, f0 = rig.band(0).freqHz;
    rig.down = {juce::KeyPress::rightKey, juce::KeyPress::upKey};
    rig.press(juce::KeyPress::upKey, kEqAlt);
    rig.frame(600.0);
    rig.frame(900.0);
    EXPECT_GT(rig.band(0).gainDb, g0) << "the second arrow moves the gain";
    EXPECT_GT(rig.band(0).freqHz, f0) << "while the first keeps the frequency going";
    EXPECT_GT(rig.band(1).gainDb, -4.0f);
    const float fBoth = rig.band(0).freqHz, gBoth = rig.band(0).gainDb;
    rig.down = {juce::KeyPress::upKey};
    rig.curve->keyStateChanged(false);
    rig.frame(1200.0);
    EXPECT_FLOAT_EQ(rig.band(0).freqHz, fBoth) << "letting one go leaves the other carrying on";
    EXPECT_GT(rig.band(0).gainDb, gBoth);
    rig.release();
    EXPECT_EQ(rig.starts, 1);
    EXPECT_EQ(rig.ends, 1) << "both keys and the whole group are one undo step";
}

TEST(EQCurvePointEditing, ShiftMakesAHeldGlideSlower) {
    float plain = 0.0f, fine = 0.0f;
    for (const bool useShift : {false, true}) {
        Rig rig;
        rig.click(rig.dot(1));
        const float f0 = rig.band(1).freqHz;
        rig.shift = useShift;
        rig.down = {juce::KeyPress::rightKey};
        rig.press(juce::KeyPress::rightKey, kEqAlt | (useShift ? kEqShift : 0));
        rig.frame(300.0);
        rig.frame(900.0);
        (useShift ? fine : plain) = std::log2(rig.band(1).freqHz / f0);
        rig.release();
    }
    EXPECT_GT(plain, 0.0f);
    EXPECT_LT(fine, plain * 0.5f);
    EXPECT_GT(fine, 0.0f);
}

TEST(EQCurvePointEditing, AHeldGlideStopsAtTheEndOfTheRangeAndAHoldAgainstItIsNoUndoStep) {
    Rig rig;
    rig.click(rig.dot(2));
    rig.eq.setBandFreq(2, ParametricEQModule::kMaxFreq);
    rig.curve->timerCallback();
    rig.starts = rig.ends = 0;
    rig.down = {juce::KeyPress::rightKey};
    rig.press(juce::KeyPress::rightKey, kEqAlt);
    rig.frame(900.0);
    EXPECT_FLOAT_EQ(rig.band(2).freqHz, ParametricEQModule::kMaxFreq);
    rig.release();
    EXPECT_EQ(rig.starts, 0);
    EXPECT_EQ(rig.ends, 0);
}

// ---- motion ----

TEST(EQCurvePointEditing, ATapEasesTheDotToItsNewPlaceAndEverythingLandsWhenTheEaseEnds) {
    FullMotion motion;
    Rig rig;
    rig.curve->getBandGlideForTest().forceAnimateForTest(true);
    rig.click(rig.dot(1));
    const auto before = rig.curve->getDrawnBandPosition(1);
    rig.press(juce::KeyPress::rightKey, kEqAlt);
    const auto resting = rig.dot(1);
    ASSERT_GT(resting.x, before.x + 1.0f);
    const auto drawn = rig.curve->getDrawnBandPosition(1);
    EXPECT_LT(drawn.x, resting.x - 0.5f) << "the dot trails the step instead of snapping";
    EXPECT_GE(drawn.x, before.x - 0.5f);
    rig.curve->getBandGlideForTest().stepForTest(1000.0);
    EXPECT_NEAR(rig.curve->getDrawnBandPosition(1).x, resting.x, 0.01f);
}

TEST(EQCurvePointEditing, ATapLandsAtOnceUnderReduceMotionAndAnimationsOff) {
    Rig rig;
    rig.curve->getBandGlideForTest().forceAnimateForTest(true);
    for (const auto mode : {synth::ui::AnimationMode::reduced, synth::ui::AnimationMode::off}) {
        synth::ui::setAnimationMode(mode);
        rig.click(rig.dot(1));
        rig.press(juce::KeyPress::rightKey, kEqAlt);
        EXPECT_NEAR(rig.curve->getDrawnBandPosition(1).x, rig.dot(1).x, 0.01f) << (int)mode;
        EXPECT_FLOAT_EQ(rig.curve->getDrawnBandAlpha(1), 1.0f) << "no fade either";
    }
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    synth::ui::setReducedMotionForTest(std::nullopt);
}
