// EQCurveBandsTests.cpp -- the Parametric EQ curve and its band strip with all 24 bands: real mouse gestures past the
// fourth slot, the type menu and its single undo step, kind colours, spoken names and the strip's chips.
#include "../Theme/ThemeSweep/ThemeSweepSupport.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/ParametricEQModule.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/PopupHost.h"
#include "UI/ModuleViews/EQBandKinds.h"
#include "UI/ModuleViews/EQBandStrip.h"
#include "UI/ModuleViews/EQBandTypeMenu.h"
#include "UI/ModuleViews/EQBandTypePill.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/EQWindow.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/Theme.h"
#include <gtest/gtest.h>
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

juce::Point<float> at(float freq, float db) {
    return {EQCurveComponent::freqToXStatic(freq, (float)kW), EQCurveComponent::dbToYStatic(db, (float)kH)};
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
    const auto p = at(2000.0f, -6.0f);
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
    rig.curve->mouseDoubleClick(mouse(*rig.curve, at(60.0f, 0.0f)));
    EXPECT_TRUE(rig.eq.isBandEnabled(0));
    EXPECT_FALSE(rig.eq.isBandEnabled(4));
}

TEST(EQCurveBands, ABandPastFourCanBeHitDraggedWheeledAndRemovedWithTheMouse) {
    Rig rig;
    rig.eq.setBandFreq(16, 1500.0f);
    rig.eq.setBandGain(16, 6.0f);
    rig.eq.setBandEnabled(16, true);
    rig.curve->timerCallback();

    const auto dot = at(1500.0f, 6.0f);
    EXPECT_EQ(rig.curve->hitTestBand(dot), 16) << "band 17";

    const auto target = at(3000.0f, -4.0f);
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

    const auto dot = at(800.0f, 0.0f);
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
    Rig rig;
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
    const int band = rig.curve->addPointAt(at(9000.0f, 2.0f));
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
