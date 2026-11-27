// EQCurveBandsTests.cpp -- the Parametric EQ curve and its band strip with all 24 bands: real mouse gestures past the
// fourth slot, the type menu and its single undo step, kind colours, spoken names and the strip's chips.
#include "../../TestWait.h"
#include "../Theme/ThemeSweep/ThemeSweepSupport.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/ParametricEQModule.h"
#include "UI/Layout/CentredLabel.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/PopupHost.h"
#include "UI/ModuleViews/EQBandKinds.h"
#include "UI/ModuleViews/EQBandStrip.h"
#include "UI/ModuleViews/EQBandTypeMenu.h"
#include "UI/ModuleViews/EQBandTypePill.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/EQWindow.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ReadableText.h"
#include "UI/Theme/Theme.h"
#include <cmath>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::eq::BandType;
using synth::ui::EqBandKind;

namespace {

constexpr int kW = 480;
constexpr int kH = 200;

juce::MouseEvent mouse(juce::Component& comp, juce::Point<float> pos,
                       int modFlags = juce::ModifierKeys::leftButtonModifier, bool dragged = false,
                       juce::Point<float> downPos = {}) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(modFlags), 0.0f,
                            0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), dragged ? downPos : pos,
                            juce::Time::getCurrentTime(), 1, dragged);
}

// Where (freq, db) is on `curve`'s plot, in its component coordinates.
juce::Point<float> at(const EQCurveComponent& curve, float freq, float db) {
    return {curve.xForFreq(freq), curve.yForDb(db)};
}

// A curve over a module with `bands` slots on, spread across the spectrum, gesture counts wired.
struct Rig {
    explicit Rig(int bands = 0) {
        for (int b = 0; b < bands; ++b) {
            eq.setBandFreq(b, 40.0f * std::pow(1.3f, (float)b));
            eq.setBandGain(b, 3.0f);
            eq.setBandEnabled(b, true);
        }
        curve = std::make_unique<EQCurveComponent>(eq);
        curve->setBounds(0, 0, kW, kH);
        curve->onGestureStart = [this] { ++starts; };
        curve->onGestureEnd = [this] { ++ends; };
    }
    ParametricEQModule eq;
    std::unique_ptr<EQCurveComponent> curve;
    int starts = 0;
    int ends = 0;
};

// Captures what launchPopover would have opened.
struct PopoverCapture {
    PopoverCapture() {
        synth::ui::test_hooks::popoverLauncherForTest() = [this](std::unique_ptr<juce::Component> content,
                                                                 juce::Rectangle<int> area) -> juce::CallOutBox* {
            menu = std::move(content);
            lastArea = area;
            return nullptr;
        };
    }
    ~PopoverCapture() { synth::ui::test_hooks::popoverLauncherForTest() = nullptr; }
    synth::ui::EQBandTypeMenu* typeMenu() const { return dynamic_cast<synth::ui::EQBandTypeMenu*>(menu.get()); }
    std::unique_ptr<juce::Component> menu;
    juce::Rectangle<int> lastArea;
};

} // namespace

TEST(EQCurveBands, DoubleClickOnEmptySpaceAddsABandPastFourAsABellParkedAtTheClick) {
    Rig rig(4);
    const auto p = at(*rig.curve, 2000.0f, -6.0f);
    rig.curve->mouseDoubleClick(mouse(*rig.curve, p));
    ASSERT_TRUE(rig.eq.isBandEnabled(4)) << "slot 5 is the lowest free one";
    EXPECT_EQ(rig.eq.getBandType(4), BandType::Peak);
    EXPECT_NEAR(rig.eq.getBandSnapshots()[4].freqHz, 2000.0f, 30.0f);
    EXPECT_NEAR(rig.eq.getBandSnapshots()[4].gainDb, -6.0f, 0.3f);
    EXPECT_EQ(rig.curve->getSelectedBand(), 4);
    EXPECT_EQ(rig.starts, 1) << "one undo step";
    EXPECT_EQ(rig.ends, 1);
}

TEST(EQCurveBands, TheFirstFourSlotsStillPickTheirHomeFrequency) {
    Rig rig;
    rig.curve->mouseDoubleClick(mouse(*rig.curve, at(*rig.curve, 60.0f, 0.0f)));
    EXPECT_TRUE(rig.eq.isBandEnabled(0));
    EXPECT_FALSE(rig.eq.isBandEnabled(4));
}

TEST(EQCurveBands, ABandPastFourCanBeHitDraggedWheeledAndRemovedWithTheMouse) {
    Rig rig;
    rig.eq.setBandFreq(16, 1500.0f);
    rig.eq.setBandGain(16, 6.0f);
    rig.eq.setBandEnabled(16, true);
    rig.curve->timerCallback();

    const auto dot = at(*rig.curve, 1500.0f, 6.0f);
    EXPECT_EQ(rig.curve->hitTestBand(dot), 16) << "band 17";

    const auto target = at(*rig.curve, 3000.0f, -4.0f);
    rig.curve->mouseDown(mouse(*rig.curve, dot));
    EXPECT_EQ(rig.curve->getSelectedBand(), 16);
    rig.curve->mouseDrag(mouse(*rig.curve, target, juce::ModifierKeys::leftButtonModifier, true, dot));
    rig.curve->mouseUp(mouse(*rig.curve, target));
    EXPECT_NEAR(rig.eq.getBandSnapshots()[16].freqHz, 3000.0f, 40.0f);
    EXPECT_NEAR(rig.eq.getBandSnapshots()[16].gainDb, -4.0f, 0.3f);
    EXPECT_EQ(rig.starts, 1) << "a drag is one undo step";

    const float q = rig.eq.getBandSnapshots()[16].q;
    juce::MouseWheelDetails wheel;
    wheel.deltaY = 0.5f;
    rig.curve->mouseWheelMove(mouse(*rig.curve, target), wheel);
    EXPECT_GT(rig.eq.getBandSnapshots()[16].q, q);

    rig.curve->mouseDoubleClick(mouse(*rig.curve, target));
    EXPECT_FALSE(rig.eq.isBandEnabled(16));
}

TEST(EQCurveBands, RightClickingADotOpensItsTypeMenuAndAChoiceIsOneUndoStep) {
    Rig rig;
    rig.eq.setBandFreq(8, 800.0f);
    rig.eq.setBandEnabled(8, true);
    rig.curve->timerCallback();
    PopoverCapture capture;

    const auto dot = at(*rig.curve, 800.0f, 0.0f);
    rig.curve->mouseDown(mouse(*rig.curve, dot, juce::ModifierKeys::rightButtonModifier));
    ASSERT_NE(capture.typeMenu(), nullptr);
    EXPECT_EQ(rig.curve->getSelectedBand(), 8);
    EXPECT_FALSE(capture.typeMenu()->slopesEnabledForTest()) << "a bell has no slope";

    capture.typeMenu()->chooseTypeForTest(BandType::Notch);
    EXPECT_EQ(rig.eq.getBandType(8), BandType::Notch);
    EXPECT_EQ(rig.starts, 1);
    EXPECT_EQ(rig.ends, 1);
}

TEST(EQCurveBands, ACutOffersItsSlopesAndASlopeChoiceSetsIt) {
    Rig rig;
    rig.eq.setBandEnabled(2, true);
    rig.curve->timerCallback();
    rig.curve->setSelectedBand(2);
    PopoverCapture capture;
    rig.curve->showTypeMenu({0, 0, 10, 10});
    ASSERT_NE(capture.typeMenu(), nullptr);

    capture.typeMenu()->chooseTypeForTest(BandType::HighCut);
    EXPECT_TRUE(capture.typeMenu()->slopesEnabledForTest());
    capture.typeMenu()->chooseSlopeForTest(48);
    EXPECT_EQ(rig.eq.getBandType(2), BandType::HighCut);
    EXPECT_EQ(rig.eq.getBandSlope(2), 48);
    EXPECT_NE(capture.typeMenu()->slopeRowForTest(96), nullptr);
    EXPECT_NE(capture.typeMenu()->typeRowForTest(BandType::Tilt), nullptr);
}

TEST(EQCurveBands, ChoosingTheTypeTheBandAlreadyHasRecordsNoUndoStep) {
    Rig rig;
    rig.eq.setBandEnabled(5, true);
    rig.curve->timerCallback();
    rig.curve->setBandTypeAndSlope(5, BandType::Peak);
    EXPECT_EQ(rig.starts, 0);
}

TEST(EQCurveBands, EveryTypeHasAKindAndEveryKindItsFixedColour) {
    const synth::theme::Colors colors;
    using synth::eq::BandType;
    const struct {
        BandType type;
        EqBandKind kind;
        juce::uint32 colour;
    } expected[] = {
        {BandType::Peak, EqBandKind::Bell, 0xff00D1FF},       {BandType::LowShelf, EqBandKind::Shelf, 0xffF5B94A},
        {BandType::HighShelf, EqBandKind::Shelf, 0xffF5B94A}, {BandType::LowCut, EqBandKind::Cut, 0xffA78BFA},
        {BandType::HighCut, EqBandKind::Cut, 0xffA78BFA},     {BandType::Notch, EqBandKind::Notch, 0xffFF6B9A},
        {BandType::Tilt, EqBandKind::Tilt, 0xff3DDC97},       {BandType::BandPass, EqBandKind::BandPass, 0xff7BE495}};
    for (const auto& e : expected) {
        EXPECT_EQ(synth::ui::eqBandKindOf(e.type), e.kind);
        EXPECT_EQ(synth::ui::eqBandKindColour(colors, e.kind), juce::Colour(e.colour));
    }
}

TEST(EQCurveBands, ADotRingTakesItsBandsKindColour) {
    Rig rig(2);
    const synth::theme::Colors colors;
    EXPECT_EQ(rig.curve->getDotColourForTest(0), colors.eqKindShelf) << "slot 1 starts as a low shelf";
    EXPECT_EQ(rig.curve->getDotColourForTest(1), colors.eqKindBell);
}

TEST(EQCurveBands, TheSelectedBandIsSpokenWithItsNumberTypeFrequencyGainAndQ) {
    Rig rig;
    rig.eq.setBandFreq(6, 2400.0f);
    rig.eq.setBandGain(6, 3.0f);
    rig.eq.setBandQ(6, 0.7f);
    rig.eq.setBandEnabled(6, true);
    rig.curve->timerCallback();
    rig.curve->setSelectedBand(6);
    EXPECT_EQ(rig.curve->getAccessibilityValueText(), "Band 7, Bell, 2.4 kHz, +3.0 dB, Q 0.7");
}

TEST(EQCurveBands, KeysSelectAcross24BandsAndDigitsStayOnTheFirstFour) {
    Rig rig(24);
    const auto key = [&rig](int code) { return rig.curve->keyPressed(juce::KeyPress(code)); };
    for (int i = 0; i < 40; ++i)
        key(juce::KeyPress::rightKey);
    EXPECT_EQ(rig.curve->getSelectedBand(), 23);
    key('4');
    EXPECT_EQ(rig.curve->getSelectedBand(), 3);
    EXPECT_FALSE(key('5')) << "digits do not reach past band 4";
}

TEST(EQCurveBands, ThePlusChipsAddsABellInTheWidestGap) {
    Rig rig;
    rig.eq.setBandFreq(0, 100.0f);
    rig.eq.setBandEnabled(0, true);
    rig.eq.setBandFreq(1, 400.0f);
    rig.eq.setBandEnabled(1, true);
    rig.curve->timerCallback();
    const int band = rig.curve->addBandInLargestGap();
    ASSERT_GE(band, 0);
    EXPECT_GT(rig.eq.getBandSnapshots()[(size_t)band].freqHz, 1000.0f) << "between 400 Hz and 20 kHz";
    EXPECT_EQ(rig.curve->getSelectedBand(), band);
}

// ---------------------------------------------------------------------------
// The band strip
// ---------------------------------------------------------------------------

namespace {
struct StripRig : Rig {
    StripRig()
        : Rig(0) {
        eq.setBandFreq(6, 3000.0f);
        eq.setBandEnabled(6, true);
        eq.setBandFreq(0, 100.0f);
        eq.setBandEnabled(0, true);
        eq.setBandFreq(10, 500.0f);
        eq.setBandEnabled(10, true);
        curve->timerCallback();
        strip = std::make_unique<synth::ui::EQBandStrip>(*curve);
        strip->setBounds(0, 0, 400, 24);
    }
    std::unique_ptr<synth::ui::EQBandStrip> strip;
};
} // namespace

TEST(EQBandStrip, ChipsRunInFrequencyOrderAndKeepTheirBandIndex) {
    StripRig rig;
    EXPECT_EQ(rig.strip->getChipBands(), (std::vector<int>{0, 10, 6}));
    auto* chip = rig.strip->getChip(6);
    ASSERT_NE(chip, nullptr);
    EXPECT_EQ(chip->getTitle(), "Band 7, Bell, 3 kHz");
    EXPECT_NE(chip->getTooltip(), juce::String());

    rig.eq.setBandFreq(6, 250.0f); // passes band 11 on the way down
    rig.curve->timerCallback();
    EXPECT_EQ(rig.strip->getChipBands(), (std::vector<int>{0, 6, 10}));
    EXPECT_EQ(rig.strip->getChip(6), chip) << "the chip is the same one: slot identity is the index";
    EXPECT_EQ(chip->getTitle(), "Band 7, Bell, 250 Hz");
}

TEST(EQBandStrip, AChipThatPassesAnotherGlidesToItsNewPlace) {
    StripRig rig;
    rig.strip->forceAnimateForTest(true);
    const auto before = rig.strip->getChip(6)->getBounds();
    rig.eq.setBandFreq(6, 250.0f);
    rig.curve->timerCallback();
    EXPECT_TRUE(rig.strip->isGlideAnimatingForTest());
    EXPECT_EQ(rig.strip->getChip(6)->getBounds().getX(), before.getX()) << "starts where it was";
    rig.strip->stepGlideForTest(1000.0);
    EXPECT_LT(rig.strip->getChip(6)->getX(), before.getX());
}

// Regression test for FRO1694: a new band's chip first showed at the band's old frequency (the band was selected while
// its cached settings were still the off ones) and slid across its neighbours to the click.
TEST(EQBandStrip, ANewBandsChipGrowsInPlaceInsteadOfSlidingInFromAStaleFrequency) {
    StripRig rig;
    rig.strip->forceAnimateForTest(true);
    const int band = rig.curve->addPointAt(at(*rig.curve, 9000.0f, 2.0f));
    ASSERT_GE(band, 0);
    auto* chip = rig.strip->getChip(band);
    ASSERT_NE(chip, nullptr);
    EXPECT_LT(chip->getWidth(), 4) << "it starts as a sliver, not as a full chip somewhere else";
    rig.strip->stepGlideForTest(1000.0);
    EXPECT_GT(chip->getWidth(), 20);
}

// Regression test for FRO1694: a removed band's chip vanished in one frame while its dot shrank.
TEST(EQBandStrip, ARemovedBandsChipShrinksIntoItsMiddleBeforeItGoes) {
    StripRig rig;
    rig.strip->forceAnimateForTest(true);
    const int childrenBefore = rig.strip->getNumChildComponents();
    rig.curve->removeBand(10);
    EXPECT_EQ(rig.strip->getNumChildComponents(), childrenBefore) << "the chip stays while it shrinks";
    rig.strip->stepGlideForTest(1000.0);
    EXPECT_EQ(rig.strip->getNumChildComponents(), childrenBefore - 1) << "and goes once it has landed";
}

namespace {
// Six bands in a row, the strip wide enough for one row of chips.
struct SixBandStripRig : Rig {
    SixBandStripRig()
        : Rig(6) {
        curve->timerCallback();
        strip = std::make_unique<synth::ui::EQBandStrip>(*curve);
        strip->setBounds(0, 0, 900, 24);
        strip->forceAnimateForTest(true);
    }
    void moveBand(int band, float freq) {
        eq.setBandFreq(band, freq);
        curve->timerCallback();
    }
    juce::Rectangle<float> offset(int band) const {
        const auto* tween = strip->getGlideForTest(band);
        return tween != nullptr ? tween->offsetFor(band) : juce::Rectangle<float>();
    }
    std::unique_ptr<synth::ui::EQBandStrip> strip;
};
} // namespace

// Regression test for FRO1781: every chip shared one tween clock, so retargeting a chip elsewhere in the strip (the
// next step of a dot drag) restarted the glide of the chips still moving: they slowed down mid-way and took longer.
TEST(EQBandStrip, AGlidingChipIsNotSlowedByAnotherChipBeingRetargeted) {
    SixBandStripRig moved;
    SixBandStripRig reference;
    for (auto* rig : {&moved, &reference})
        rig->moveBand(0, 60.0f); // band 1 passes band 2: both glide
    moved.strip->stepGlideForTest(60.0);
    reference.strip->stepGlideForTest(60.0);
    ASSERT_NE(moved.offset(0), juce::Rectangle<float>()) << "still gliding";

    moved.moveBand(4, 160.0f); // band 5 passes band 6, in the same strip
    for (int frame = 0; frame < 6; ++frame) {
        moved.strip->stepGlideForTest(16.0);
        reference.strip->stepGlideForTest(16.0);
        for (const int band : {0, 1})
            EXPECT_EQ(moved.offset(band), reference.offset(band)) << "band " << band + 1 << " frame " << frame;
    }
}

TEST(EQBandStrip, ADotDraggedAcrossANeighbourAndBackKeepsItsChipContinuousAndLandsItAtRest) {
    SixBandStripRig rig;
    const auto restOf = [&](int band) { return rig.strip->getChip(band)->getBounds(); };
    const auto before = restOf(0);
    rig.moveBand(0, 60.0f);
    rig.strip->stepGlideForTest(48.0);
    const auto midway = restOf(0);
    EXPECT_NE(midway.getX(), before.getX());

    rig.moveBand(0, 40.0f); // back again, mid-glide
    EXPECT_NEAR(restOf(0).getX(), midway.getX(), 1) << "the retarget itself does not jump";
    int lastX = restOf(0).getX();
    for (int frame = 0; frame < 6; ++frame) {
        rig.strip->stepGlideForTest(16.0);
        EXPECT_LE(std::abs(restOf(0).getX() - lastX), 70) << "frame " << frame;
        lastX = restOf(0).getX();
    }
    rig.strip->stepGlideForTest(1000.0);
    EXPECT_EQ(restOf(0), before) << "lands on its rest slot";
    EXPECT_EQ(rig.strip->getChipBands(), (std::vector<int>{0, 1, 2, 3, 4, 5}));
    EXPECT_FALSE(rig.strip->isGlideAnimatingForTest());
}

TEST(EQBandStrip, TheTooltipFollowsTheBandWithoutBeingRebuiltOnEveryStep) {
    SixBandStripRig rig;
    const auto before = rig.strip->getChip(2)->getTooltip();
    rig.moveBand(2, 77.0f);
    EXPECT_NE(rig.strip->getChip(2)->getTooltip(), before) << "built at hover time from the curve's current band";
}

TEST(EQBandStrip, PressingAChipSelectsItsBandAndTheCurveSelectionMovesTheChip) {
    StripRig rig;
    rig.strip->getChip(10)->onClick();
    EXPECT_EQ(rig.curve->getSelectedBand(), 10);
    EXPECT_TRUE(rig.strip->getChip(10)->getToggleState());
    rig.curve->setSelectedBand(6);
    EXPECT_FALSE(rig.strip->getChip(10)->getToggleState());
    EXPECT_TRUE(rig.strip->getChip(6)->getToggleState());
}

TEST(EQBandStrip, ReturnOnAFocusedChipSelectsItAndTheOffBandShowsOnlyWhileSelected) {
    StripRig rig;
    EXPECT_EQ(rig.strip->getChip(3), nullptr) << "an off band has no chip";
    rig.curve->setSelectedBand(3);
    ASSERT_NE(rig.strip->getChip(3), nullptr) << "unless it is selected (drawn at 35%)";
    rig.curve->setSelectedBand(-1);
    EXPECT_EQ(rig.strip->getChip(3), nullptr);

    juce::Component* chip = rig.strip->getChip(0);
    EXPECT_TRUE(chip->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(rig.curve->getSelectedBand(), 0);
}

TEST(EQBandStrip, ThePlusChipAddsABandAndSelectsIt) {
    StripRig rig;
    ASSERT_NE(rig.strip->getAddChip(), nullptr);
    EXPECT_EQ(rig.strip->getAddChip()->getTitle(), "Add band");
    rig.strip->getAddChip()->onClick();
    EXPECT_EQ(rig.eq.getEnabledBandCount(), 4);
    EXPECT_GE(rig.curve->getSelectedBand(), 0);
    EXPECT_EQ(rig.strip->getChipBands().size(), (size_t)4);
}

// Regression test for FRO1784: Left/Right stepped by slot number and clamped at 24, spinning on a hidden slot.
TEST(EQBandStrip, LeftAndRightOnTheCurveWalkTheBandsInFrequencyOrderThenLeadToThePlusChip) {
    StripRig rig; // slots 0 (100 Hz), 10 (500 Hz) and 6 (3 kHz): not in numeric order
    int pastEnd = 0;
    rig.curve->onSelectPastLast = [&pastEnd] { ++pastEnd; };
    const int expected[] = {0, 10, 6};
    for (const int band : expected) {
        rig.curve->selectBandRelative(+1);
        EXPECT_EQ(rig.curve->getSelectedBand(), band);
    }
    EXPECT_EQ(pastEnd, 0);
    rig.curve->selectBandRelative(+1);
    EXPECT_EQ(pastEnd, 1) << "past the last band the + chip is next";
    EXPECT_EQ(rig.curve->getSelectedBand(), 6) << "the selection stays on the last band";
    rig.curve->selectBandRelative(-1);
    EXPECT_EQ(rig.curve->getSelectedBand(), 10);
    rig.curve->selectBandRelative(-1);
    rig.curve->selectBandRelative(-1);
    EXPECT_EQ(rig.curve->getSelectedBand(), 0) << "no wrap at the first band";
    EXPECT_EQ(rig.curve->shownBandsInOrder(), rig.strip->getChipBands()) << "keys and chips share one order";
}

TEST(EQBandStrip, AnOffBandIsSkippedByTheArrowsUnlessItIsTheSelectedOne) {
    StripRig rig;
    rig.eq.setBandFreq(3, 300.0f); // off, between 100 and 500
    rig.curve->timerCallback();
    rig.curve->setSelectedBand(0);
    rig.curve->selectBandRelative(+1);
    EXPECT_EQ(rig.curve->getSelectedBand(), 10) << "slot 3 is off and not shown";
}

TEST(EQBandStrip, ArrowsOnTheChipsMoveFocusInTheShownOrderEndingOnThePlusChip) {
    StripRig rig;
    juce::Component parent;
    parent.setBounds(0, 0, 400, 24);
    parent.addAndMakeVisible(*rig.strip);
    parent.addToDesktop(0);
    parent.setVisible(true);
    auto* first = rig.strip->getChip(0);
    first->grabKeyboardFocus();
    if (!first->hasKeyboardFocus(false)) {
        parent.removeFromDesktop();
        GTEST_SKIP() << "the window did not give the chip keyboard focus";
    }
    const auto press = [](juce::Component* chip, const juce::KeyPress& key) { return chip->keyPressed(key); };
    const juce::KeyPress right(juce::KeyPress::rightKey), left(juce::KeyPress::leftKey);
    EXPECT_TRUE(press(first, right));
    EXPECT_TRUE(rig.strip->getChip(10)->hasKeyboardFocus(false)) << "chip 11 (500 Hz) follows chip 1";
    EXPECT_TRUE(press(rig.strip->getChip(10), right));
    EXPECT_TRUE(rig.strip->getChip(6)->hasKeyboardFocus(false));
    EXPECT_TRUE(press(rig.strip->getChip(6), right));
    EXPECT_TRUE(rig.strip->getAddChip()->hasKeyboardFocus(false));
    EXPECT_TRUE(press(rig.strip->getAddChip(), right));
    EXPECT_TRUE(rig.strip->getAddChip()->hasKeyboardFocus(false)) << "the + chip is the end stop";
    EXPECT_TRUE(press(rig.strip->getAddChip(), left));
    EXPECT_TRUE(rig.strip->getChip(6)->hasKeyboardFocus(false));
    EXPECT_TRUE(press(rig.strip->getChip(6), left));
    EXPECT_TRUE(rig.strip->getChip(10)->hasKeyboardFocus(false));
    rig.strip->getAddChip()->grabKeyboardFocus();
    press(rig.strip->getAddChip(), juce::KeyPress(juce::KeyPress::returnKey));
    EXPECT_EQ(rig.eq.getEnabledBandCount(), 4) << "Return on + adds a band";
    parent.removeFromDesktop();
}

// Keyboard reach: the arrows between chips move the selection, not only keyboard focus.
TEST(EQBandStrip, ArrowsOnTheChipsAlsoSelectThatBandAndLandingOnThePlusLeavesTheSelection) {
    StripRig rig;
    rig.curve->setSelectedBand(0);
    const juce::KeyPress right(juce::KeyPress::rightKey), left(juce::KeyPress::leftKey);
    EXPECT_TRUE(static_cast<juce::Component*>(rig.strip->getChip(0))->keyPressed(right));
    EXPECT_EQ(rig.curve->getSelectedBand(), 10) << "chip 11 (500 Hz) follows chip 1";
    EXPECT_TRUE(rig.strip->getChip(10)->getToggleState());
    EXPECT_TRUE(static_cast<juce::Component*>(rig.strip->getChip(10))->keyPressed(right));
    EXPECT_EQ(rig.curve->getSelectedBand(), 6);
    EXPECT_TRUE(static_cast<juce::Component*>(rig.strip->getChip(6))->keyPressed(right)); // onto +
    EXPECT_EQ(rig.curve->getSelectedBand(), 6) << "landing on + changes nothing";
    EXPECT_TRUE(static_cast<juce::Component*>(rig.strip->getAddChip())->keyPressed(left));
    EXPECT_EQ(rig.curve->getSelectedBand(), 6);
    EXPECT_TRUE(static_cast<juce::Component*>(rig.strip->getChip(6))->keyPressed(left));
    EXPECT_EQ(rig.curve->getSelectedBand(), 10);
}

// A mouse delete path on the chips; each is one undo step, like the dot's double-click.
TEST(EQBandStrip, DoubleClickingAChipDeletesItsBandAsOneUndoStep) {
    StripRig rig;
    const auto e = mouse(*rig.strip->getChip(10), {5.0f, 5.0f});
    rig.strip->getChip(10)->mouseDoubleClick(e);
    synth::test::waitUntil([&] { return !rig.eq.isBandEnabled(10); });
    EXPECT_FALSE(rig.eq.isBandEnabled(10));
    EXPECT_TRUE(rig.eq.isBandEnabled(0));
    EXPECT_TRUE(rig.eq.isBandEnabled(6));
    EXPECT_EQ(rig.starts, 1) << "one undo step";
    EXPECT_EQ(rig.ends, 1);
}

TEST(EQBandStrip, RightClickingAChipOpensItsMenuAndThePlusChipHasNone) {
    StripRig rig;
    std::vector<int> asked;
    rig.strip->chipMenuForTest = [&asked](int band) { asked.push_back(band); };
    const auto right = [](juce::Component& c) {
        return mouse(c, {5.0f, 5.0f}, juce::ModifierKeys::rightButtonModifier);
    };
    static_cast<juce::Component*>(rig.strip->getChip(6))->mouseDown(right(*rig.strip->getChip(6)));
    EXPECT_EQ(asked, (std::vector<int>{6}));
    static_cast<juce::Component*>(rig.strip->getAddChip())->mouseDown(right(*rig.strip->getAddChip()));
    EXPECT_EQ(asked.size(), (size_t)1) << "+ has no delete menu";
}

// + stays at a fixed place however many bands there are.
TEST(EQBandStrip, ThePlusChipStaysAtTheRightEdgeAsBandsAreAdded) {
    Rig rig(2);
    synth::ui::EQBandStrip strip(*rig.curve);
    strip.setBounds(0, 0, 600, 24);
    const auto home = strip.getAddChip()->getBounds();
    EXPECT_EQ(home.getRight(), 600);
    for (int b = 2; b < 8; ++b) {
        rig.eq.setBandFreq(b, 40.0f * std::pow(1.3f, (float)b));
        rig.eq.setBandEnabled(b, true);
        rig.curve->timerCallback();
        strip.completeGlideForTest();
        EXPECT_EQ(strip.getAddChip()->getBounds(), home) << "after adding band " << b + 1;
    }
    EXPECT_EQ(strip.getChipBands().size(), (size_t)8);
    for (const int b : strip.getChipBands())
        EXPECT_LE(strip.getChip(b)->getRight(), home.getX()) << "chips flow left of +";
}

TEST(EQBandStrip, ChipsWrapToMoreRowsWhenTheWidthAsksForIt) {
    Rig rig(24);
    synth::ui::EQBandStrip strip(*rig.curve);
    EXPECT_GT(strip.heightForWidth(300), strip.heightForWidth(1200));
    EXPECT_GE(strip.heightForWidth(300), 2 * 22);
    Rig few(3);
    synth::ui::EQBandStrip small(*few.curve);
    EXPECT_EQ(small.heightForWidth(400), 22) << "a few chips fit one row";
}

TEST(EQBandStrip, TheTypePillShowsTheSelectedBandsTypeAndOpensTheMenu) {
    Rig rig(2);
    synth::ui::EQBandTypePill pill(*rig.curve);
    EXPECT_EQ(pill.getPillText(), "");
    rig.curve->setSelectedBand(1);
    EXPECT_EQ(pill.getPillText(), "Bell");
    EXPECT_EQ(pill.getTitle(), "Band 2 type, Bell");
    PopoverCapture capture;
    pill.onClick();
    EXPECT_NE(capture.typeMenu(), nullptr);
}

TEST(EQBandStrip, TheWindowHostsTheStripAndKeepsARoomForCompareAndSuggest) {
    ParametricEQModule eq;
    EQWindow window(eq);
    EXPECT_EQ(window.getReservedToolbarSlot().getWidth(), EQWindow::kReservedToolbarSlotWidth);
    EXPECT_GE(window.getReservedToolbarSlot().getX(), window.getSpectrumToggle().getRight());
    EXPECT_LE(window.getStrip().getBottom(), window.getHeight());
    EXPECT_GE(window.getStrip().getY(), window.getCurve().getBottom());
}

// The EQ band strip's chips and type pill in every built-in theme: the label of a resting or hovered chip reaches 4.5:1
// against its kind tint over the surface it sits on, and the label of a selected chip or the pill against the solid
// kind colour. The text sweep cannot read these (they are painted, not Labels), so the maths here follows
// EQBandStrip::Chip::paintButton and EQBandTypePill::paintButton.
TEST(EQBandStrip, TheChipAndPillLabelsReadInEveryThemeForEveryKind) {
    constexpr EqBandKind kinds[] = {EqBandKind::Bell,  EqBandKind::Shelf, EqBandKind::Cut,
                                    EqBandKind::Notch, EqBandKind::Tilt,  EqBandKind::BandPass};
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto& colors = theme.colors;
        for (const auto kind : kinds) {
            const auto tint = synth::ui::eqBandKindColour(colors, kind);
            const auto ink = synth::ui::eqBandKindInk(colors);
            for (const auto& ground : {colors.surface, colors.bg0}) {
                for (const float alpha : {0.22f, 0.34f}) {
                    const auto fill = ground.withAlpha(1.0f).overlaidWith(tint.withAlpha(alpha));
                    EXPECT_GE(synth::test::theme_sweep::contrastRatio(colors.textPrimary, fill),
                              synth::test::theme_sweep::kMinTextContrast)
                        << theme.id << ": chip label on a kind tint at " << alpha << " (kind " << (int)kind << ")";
                }
            }
            EXPECT_GE(synth::test::theme_sweep::contrastRatio(ink, tint), synth::test::theme_sweep::kMinTextContrast)
                << theme.id << ": selected chip (kind " << (int)kind << ")";
            EXPECT_GE(synth::test::theme_sweep::contrastRatio(ink, tint.brighter(0.15f)),
                      synth::test::theme_sweep::kMinTextContrast)
                << theme.id << ": hovered pill (kind " << (int)kind << ")";
        }
    }
}

// The EQ dot sweep (below): the numbered dots of the Parametric EQ curve, measured from rendered pixels in every
// built-in theme. Each theme renders a dot for every number 1-24, unselected and selected, and asserts:
//   * size: the painted ring is at least as big as EQCurveComponent::kHandleRadius (the one constant paint, hit
//     test, readout and type menu share);
//   * an unselected ring reads at 3:1 against the curve's surface and against the spectrum's fill, and is painted in
//     exactly that colour (full alpha);
//   * a selected dot's halo reads at 1.5:1 against the surface, and a selected ring of the accent stands outside the
//   dot;
//   * the number's ink centroid sits within kCentreTolerance px of the ring's centre, horizontally and vertically.
// It replaces the hand-picked bands of the old EQDotNumberCentringTests (checked 1, 4, 10 and 24 at one theme).
// AGENTSYNTH_EQ_DOT_VERBOSE=1 prints every measurement. Images use SoftwareImageType(): the default native image type
// reads back zeros on a Windows runner.
namespace eq_dot_sweep {

namespace {

constexpr int kW = 520;
constexpr int kH = 200;
constexpr float kCentreTolerance = 0.5f;

bool verbose() {
    const char* v = std::getenv("AGENTSYNTH_EQ_DOT_VERBOSE");
    return v != nullptr && v[0] == '1';
}

float distance(juce::Colour a, juce::Colour b) {
    return std::max({std::abs(a.getFloatRed() - b.getFloatRed()), std::abs(a.getFloatGreen() - b.getFloatGreen()),
                     std::abs(a.getFloatBlue() - b.getFloatBlue())});
}

struct Measured {
    juce::Point<float> ring;     // where the dot is drawn
    juce::Point<float> centroid; // the number's ink centroid
    juce::Point<float> boxCentre;
    bool inkFound = false;
    float outerRadius = 0.0f;      // from the ring centre up to the outer edge of the ring's stroke
    juce::Colour haloSample;       // the pixel in the middle of the halo band straight above the dot
    juce::Colour paintedRing;      // the colour the dot's ring is meant to be painted in
    juce::Colour ringSample;       // the pixel in the middle of the ring's stroke straight above the dot
    juce::Colour accentRingSample; // the pixel in the middle of the selected dot's accent ring
};

// One dot (band `number` - 1 only) at 2.2 kHz and +6 dB, so the plot above it holds only the surface.
Measured measure(juce::LookAndFeel& laf, int number, bool selected) {
    ParametricEQModule eq;
    const int band = number - 1;
    eq.setBandFreq(band, 2200.0f + 0.37f * (float)band);
    eq.setBandGain(band, 6.0f);
    eq.setBandEnabled(band, true);
    EQCurveComponent curve(eq);
    curve.setLookAndFeel(&laf);
    curve.setBounds(0, 0, kW, kH);
    curve.setShowSpectrum(false);
    curve.timerCallback();
    if (selected)
        curve.setSelectedBand(band);

    juce::Image img(juce::Image::ARGB, kW, kH, true, juce::SoftwareImageType());
    {
        juce::Graphics g(img);
        curve.paintEntireComponent(g, false);
    }
    const auto& colors = synth::theme::themeOf(curve).colors;
    Measured m;
    m.ring = curve.getDrawnBandPosition(band);
    const float radius = EQCurveComponent::kHandleRadius + (selected ? EQCurveComponent::kDotActiveGrow : 0.0f);
    const int cx = (int)std::floor(m.ring.x), cy = (int)std::floor(m.ring.y);

    // Straight up from the centre: the ring's stroke is the run of pixels nearest the ring's colour.
    const auto at = [&](float dy) { return img.getPixelAt(cx, (int)std::floor(m.ring.y - dy)); };
    // The pixel of the ring's stroke nearest the colour it is painted in (the stroke is anti-aliased across two rows).
    m.paintedRing = curve.getDotRingColourForTest(band);
    m.ringSample = at(radius);
    for (const float dy : {radius - 0.5f, radius, radius + 0.5f})
        if (distance(at(dy), m.paintedRing) < distance(m.ringSample, m.paintedRing))
            m.ringSample = at(dy);
    m.haloSample = at(radius + 1.0f + EQCurveComponent::kDotAccentRingPx +
                      (EQCurveComponent::kDotHaloWidth - 1.0f - EQCurveComponent::kDotAccentRingPx) * 0.5f);
    m.accentRingSample = at(radius + 1.0f + EQCurveComponent::kDotAccentRingPx * 0.5f);
    for (float dy = radius + 6.0f; dy >= radius - 3.0f; dy -= 0.25f)
        if (distance(at(dy), colors.bg1) > 0.08f && m.outerRadius == 0.0f && !selected) {
            m.outerRadius = dy;
            break;
        }

    // The number: ink inside the ring's inner edge, weighted by how far each pixel is from the dot's fill.
    double sum = 0, sx = 0, sy = 0;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    const float inner = EQCurveComponent::kHandleRadius - 3.0f;
    for (int y = cy - (int)inner - 1; y <= cy + (int)inner + 1; ++y)
        for (int x = cx - (int)inner - 1; x <= cx + (int)inner + 1; ++x) {
            const float dx = (float)x + 0.5f - m.ring.x, dy = (float)y + 0.5f - m.ring.y;
            if (dx * dx + dy * dy > inner * inner)
                continue;
            const float w = distance(img.getPixelAt(x, y), colors.bg1);
            if (w < 0.02f)
                continue;
            sum += w;
            sx += w * ((double)x + 0.5);
            sy += w * ((double)y + 0.5);
            if (w > 0.25f) {
                x0 = std::min(x0, (float)x);
                y0 = std::min(y0, (float)y);
                x1 = std::max(x1, (float)x + 1.0f);
                y1 = std::max(y1, (float)y + 1.0f);
            }
        }
    m.inkFound = sum > 0.0;
    if (m.inkFound) {
        m.centroid = {(float)(sx / sum), (float)(sy / sum)};
        m.boxCentre = {(x0 + x1) * 0.5f, (y0 + y1) * 0.5f};
    }
    curve.setLookAndFeel(nullptr);
    return m;
}

} // namespace

TEST(EQDotSweep, EveryNumberAndDotReadsAndSitsInTheMiddleInEveryTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        synth::theme::AppLookAndFeel laf;
        laf.applyTheme(theme);
        const auto& c = theme.colors;
        const auto spectrumFill = c.bg1.interpolatedWith(c.accent2, 0.22f);
        for (int number = 1; number <= 24; ++number) {
            for (const bool selected : {false, true}) {
                const auto m = measure(laf, number, selected);
                const auto label = theme.id.toStdString() + " dot " + std::to_string(number) +
                                   (selected ? " selected" : " unselected");
                ASSERT_TRUE(m.inkFound) << label;
                if (verbose())
                    std::cout << "[ eq dot ] " << label << " centroid off (" << m.centroid.x - m.ring.x << ", "
                              << m.centroid.y - m.ring.y << ") box off (" << m.boxCentre.x - m.ring.x << ", "
                              << m.boxCentre.y - m.ring.y << ") ring contrast "
                              << synth::theme::contrastBetween(m.ringSample, c.bg1) << " halo contrast "
                              << synth::theme::contrastBetween(m.haloSample, c.bg1) << "\n";
                EXPECT_NEAR(m.centroid.x, m.ring.x, kCentreTolerance) << label << ": number off-centre sideways";
                EXPECT_NEAR(m.centroid.y, m.ring.y, kCentreTolerance) << label << ": number off-centre vertically";
                if (!selected) {
                    EXPECT_GE(m.outerRadius, EQCurveComponent::kHandleRadius)
                        << label << ": dot smaller than the constant";
                    EXPECT_GE(synth::theme::contrastBetween(m.paintedRing, c.bg1), 3.0) << label << ": ring on surface";
                    EXPECT_GE(synth::theme::contrastBetween(m.paintedRing, spectrumFill), 3.0)
                        << label << ": ring on spectrum fill";
                    EXPECT_LT(distance(m.ringSample, m.paintedRing), 0.12f)
                        << label << ": ring not painted in full colour";
                } else {
                    EXPECT_GE(synth::theme::contrastBetween(m.haloSample, c.bg1), 1.5) << label << ": halo on surface";
                    EXPECT_LT(distance(m.accentRingSample, c.accent), 0.12f)
                        << label << ": no accent ring on the selected dot";
                }
            }
        }
    }
}

} // namespace eq_dot_sweep
