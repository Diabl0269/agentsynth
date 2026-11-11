// EQCurveShortcutTests.cpp -- the Parametric EQ curve's keys as rebindable ShortcutManager actions (category EQ): every
// default key through real KeyPress objects, a rebind moving a key, Option-click and S solo with their look and their
// clean-up, Cmd+D duplicate, the keys reaching the curve before the app's own commands on a card, and the tooltips
// naming the live key.
#include "../../FX/ParametricEQ/ParametricEQTestHelpers.h"
#include "../../TestWait.h"
#include "../Layout/NativeWindowTestGuard.h"
#include "../Layout/RealKeyDelivery.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/ParametricEQModule.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/PopupHost.h"
#include "UI/ModuleViews/EQBandStrip.h"
#include "UI/ModuleViews/EQBandTypePill.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/EQSoloButton.h"
#include "UI/ModuleViews/EQWindow.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::eq::BandType;

namespace {

constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kShift = juce::ModifierKeys::shiftModifier;
constexpr int kCmd = juce::ModifierKeys::commandModifier;

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

juce::MouseEvent mouse(juce::Component& comp, juce::Point<float> pos, int modFlags) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(modFlags), 0.0f,
                            0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos,
                            juce::Time::getCurrentTime(), 1, false);
}

// A curve over an EQ with bands 1 (1 kHz, 0 dB) and 2 (2 kHz, +3 dB) on, wired to a manager and counting gestures.
struct Rig {
    Rig() {
        eq.setBandEnabled(0, true);
        eq.setBandFreq(0, 1000.0f);
        eq.setBandGain(0, 0.0f);
        eq.setBandEnabled(1, true);
        eq.setBandFreq(1, 2000.0f);
        eq.setBandGain(1, 3.0f);
        curve = std::make_unique<EQCurveComponent>(eq);
        curve->setBounds(0, 0, 480, 200);
        curve->setShortcutManager(&shortcuts);
        curve->onGestureStart = [this] { ++starts; };
        curve->onGestureEnd = [this] { ++ends; };
    }
    ~Rig() { curve.reset(); }
    bool press(int code, int mods = 0) { return curve->keyPressed(key(code, mods)); }
    ParametricEQModule::BandSnapshot band(int b) const { return eq.getBandSnapshots()[(size_t)b]; }
    void select(int b) {
        curve->selectBand(b);
        starts = ends = 0;
    }

    ShortcutManager shortcuts;
    ParametricEQModule eq;
    std::unique_ptr<EQCurveComponent> curve;
    int starts = 0;
    int ends = 0;
};

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
    std::unique_ptr<juce::Component> menu;
    juce::Rectangle<int> lastArea;
};

} // namespace

// ---- the action table ----

TEST(EQShortcuts, EveryEqActionIsInItsOwnCategoryWithItsDefaultKeyAndAName) {
    ShortcutManager manager;
    struct Row {
        const char* id;
        int code;
        int mods;
    };
    const Row rows[] = {{"eqSelectPrev", juce::KeyPress::leftKey, 0},
                        {"eqSelectNext", juce::KeyPress::rightKey, 0},
                        {"eqMoveLeft", juce::KeyPress::leftKey, kAlt},
                        {"eqMoveRight", juce::KeyPress::rightKey, kAlt},
                        {"eqMoveUp", juce::KeyPress::upKey, kAlt},
                        {"eqMoveDown", juce::KeyPress::downKey, kAlt},
                        {"eqWidthUp", juce::KeyPress::upKey, 0},
                        {"eqWidthDown", juce::KeyPress::downKey, 0},
                        {"eqSolo", 's', 0},
                        {"eqTypeMenu", 't', 0},
                        {"eqAddBand", '+', 0},
                        {"eqRemoveBand", juce::KeyPress::backspaceKey, 0},
                        {"eqDuplicateBand", 'd', kCmd}};
    for (const auto& row : rows) {
        EXPECT_EQ(ShortcutManager::getCategory(row.id), ShortcutCategory::EQ) << row.id;
        EXPECT_TRUE(manager.getBinding(row.id) == key(row.code, row.mods)) << row.id;
        EXPECT_TRUE(ShortcutManager::getActionDescription(row.id).isNotEmpty()) << row.id;
        EXPECT_TRUE(manager.getActionIds().contains(row.id)) << row.id;
    }
    EXPECT_EQ(ShortcutManager::getCategoryName(ShortcutCategory::EQ), "EQ");
    EXPECT_EQ(ShortcutManager::getActionIdsInCategory(ShortcutCategory::EQ).size(), (int)std::size(rows));
}

// ---- the default keys ----

TEST(EQShortcuts, LeftAndRightSelectAndAltArrowsMoveFrequencyAndGainWithShiftFine) {
    Rig rig;
    EXPECT_TRUE(rig.press(juce::KeyPress::rightKey));
    EXPECT_EQ(rig.curve->getSelectedBand(), 0);
    EXPECT_TRUE(rig.press(juce::KeyPress::rightKey, kShift)) << "Shift on a select key still selects";
    EXPECT_EQ(rig.curve->getSelectedBand(), 1);
    EXPECT_TRUE(rig.press(juce::KeyPress::leftKey));
    EXPECT_EQ(rig.curve->getSelectedBand(), 0);
    rig.starts = rig.ends = 0;

    EXPECT_TRUE(rig.press(juce::KeyPress::rightKey, kAlt));
    EXPECT_NEAR(rig.band(0).freqHz, 1000.0f * std::pow(2.0f, 1.0f / 12.0f), 0.5f);
    EXPECT_TRUE(rig.press(juce::KeyPress::leftKey, kAlt | kShift));
    EXPECT_NEAR(rig.band(0).freqHz, 1000.0f * std::pow(2.0f, 0.75f / 12.0f), 0.5f) << "Shift is a quarter step";
    EXPECT_TRUE(rig.press(juce::KeyPress::upKey, kAlt));
    EXPECT_NEAR(rig.band(0).gainDb, 1.0f, 0.01f);
    EXPECT_TRUE(rig.press(juce::KeyPress::downKey, kAlt | kShift));
    EXPECT_NEAR(rig.band(0).gainDb, 0.75f, 0.01f);
    EXPECT_EQ(rig.starts, 4);
    EXPECT_EQ(rig.ends, 4);
}

// Regression test for FRO1695: plain Up/Down used to change gain; they widen and narrow the band now.
TEST(EQShortcuts, UpWidensAndDownNarrowsWhileGainStaysPutAndPageKeysKeepTheirOldDirection) {
    Rig rig;
    rig.select(1);
    const float q = rig.band(1).q, gain = rig.band(1).gainDb;
    EXPECT_TRUE(rig.press(juce::KeyPress::upKey));
    EXPECT_NEAR(rig.band(1).q, q * std::pow(2.0f, -0.25f), 0.001f) << "Up lowers Q: a wider band";
    EXPECT_EQ(rig.band(1).gainDb, gain);
    EXPECT_TRUE(rig.press(juce::KeyPress::downKey));
    EXPECT_NEAR(rig.band(1).q, q, 0.001f);
    EXPECT_TRUE(rig.press(juce::KeyPress::downKey, kShift));
    EXPECT_NEAR(rig.band(1).q, q * std::pow(2.0f, 0.0625f), 0.001f) << "Shift is a quarter step";
    EXPECT_EQ(rig.starts, 3);
    EXPECT_EQ(rig.ends, 3);

    rig.select(0);
    const float before = rig.band(0).q;
    rig.press(juce::KeyPress::pageUpKey);
    EXPECT_NEAR(rig.band(0).q, before * std::pow(2.0f, 0.25f), 0.001f) << "Page Up narrows, as before";
    rig.press(juce::KeyPress::pageDownKey);
    EXPECT_NEAR(rig.band(0).q, before, 0.001f);
}

TEST(EQShortcuts, ThePlusKeyAddsABandHoweverTheKeyboardSendsIt) {
    Rig rig;
    int on = rig.eq.getEnabledBandCount();
    for (const auto& press : {key('+'), key('+', kShift), key('=', kShift), key(juce::KeyPress::numberPadAdd)}) {
        rig.starts = rig.ends = 0;
        EXPECT_TRUE(rig.curve->keyPressed(press)) << press.getTextDescription();
        EXPECT_EQ(rig.eq.getEnabledBandCount(), ++on) << press.getTextDescription();
        EXPECT_EQ(rig.starts, 1) << "an added band is one undo step";
    }
    EXPECT_FALSE(rig.curve->keyPressed(key('='))) << "a bare = is not the plus key";
    EXPECT_EQ(rig.eq.getEnabledBandCount(), on);
}

TEST(EQShortcuts, BackspaceAndDeleteRemoveTheBandAndReturnBringsItBack) {
    Rig rig;
    rig.select(1);
    EXPECT_TRUE(rig.press(juce::KeyPress::backspaceKey));
    EXPECT_FALSE(rig.eq.isBandEnabled(1));
    EXPECT_EQ(rig.curve->getSelectedBand(), 1) << "it stays selected so Return can undo the removal";
    EXPECT_EQ(rig.starts, 1);
    EXPECT_TRUE(rig.press(juce::KeyPress::backspaceKey)) << "nothing to remove still consumes the key";
    EXPECT_EQ(rig.starts, 1);
    EXPECT_TRUE(rig.press(juce::KeyPress::returnKey));
    EXPECT_TRUE(rig.eq.isBandEnabled(1));
    EXPECT_TRUE(rig.press(juce::KeyPress::deleteKey));
    EXPECT_FALSE(rig.eq.isBandEnabled(1)) << "Delete is a fixed alias";
    EXPECT_TRUE(rig.press(juce::KeyPress::returnKey));
    EXPECT_TRUE(rig.eq.isBandEnabled(1));
}

TEST(EQShortcuts, TOpensTheSelectedBandsTypeMenuAtItsDotAndDoesNothingWithoutASelection) {
    Rig rig;
    PopoverCapture capture;
    EXPECT_TRUE(rig.press('t'));
    EXPECT_EQ(capture.menu, nullptr);
    rig.select(1);
    EXPECT_TRUE(rig.press('t'));
    ASSERT_NE(capture.menu, nullptr);
    const auto dot = rig.curve->localAreaToGlobal(
        juce::Rectangle<int>(14, 14).withCentre(rig.curve->getDrawnBandPosition(1).roundToInt()));
    EXPECT_EQ(capture.lastArea, dot);
}

// ---- duplicate ----

TEST(EQShortcuts, CmdDCopiesTheSelectedBandIntoTheLowestFreeSlotTwoSemitonesUpAsOneUndoStep) {
    Rig rig;
    rig.eq.setBandType(1, BandType::HighShelf);
    rig.eq.setBandQ(1, 2.5f);
    rig.eq.setBandEnabled(2, false);
    rig.select(1);
    EXPECT_TRUE(rig.press('d', kCmd));
    EXPECT_EQ(rig.eq.getEnabledBandCount(), 3);
    ASSERT_TRUE(rig.eq.isBandEnabled(2)) << "slot 3 is the lowest free one";
    const auto copy = rig.band(2);
    EXPECT_EQ(copy.type, BandType::HighShelf);
    EXPECT_NEAR(copy.freqHz, 2000.0f * std::pow(2.0f, 2.0f / 12.0f), 0.5f);
    EXPECT_NEAR(copy.gainDb, 3.0f, 0.01f);
    EXPECT_NEAR(copy.q, 2.5f, 0.01f);
    EXPECT_EQ(rig.band(1).freqHz, 2000.0f) << "the original stays put";
    EXPECT_EQ(rig.curve->getSelectedBand(), 2) << "the copy is selected";
    EXPECT_EQ(rig.starts, 1);
    EXPECT_EQ(rig.ends, 1);
}

TEST(EQShortcuts, TheCopyGrowsInLikeAnAddedBandAndClampsAtTheTopOfTheRange) {
    Rig rig;
    rig.eq.setBandFreq(1, ParametricEQModule::kMaxFreq);
    rig.select(1);
    auto& glide = rig.curve->getBandGlideForTest();
    glide.forceAnimateForTest(true);
    ASSERT_TRUE(rig.curve->duplicateSelectedBand());
    EXPECT_NEAR(rig.band(2).freqHz, ParametricEQModule::kMaxFreq, 1.0f);
    EXPECT_LT(rig.curve->getDrawnBandAlpha(2), 1.0f) << "it fades in rather than popping";
    glide.stepForTest(1000.0);
    EXPECT_FLOAT_EQ(rig.curve->getDrawnBandAlpha(2), 1.0f);
}

TEST(EQShortcuts, DuplicateDoesNothingWithNoSelectionOrAllTwentyFourBandsOnButStillTakesTheKey) {
    Rig rig;
    EXPECT_TRUE(rig.press('d', kCmd));
    EXPECT_EQ(rig.eq.getEnabledBandCount(), 2);
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b)
        rig.eq.setBandEnabled(b, true);
    rig.select(1);
    EXPECT_FALSE(rig.curve->duplicateSelectedBand());
    EXPECT_TRUE(rig.press('d', kCmd));
    EXPECT_EQ(rig.starts, 0);
    EXPECT_EQ(rig.eq.getEnabledBandCount(), ParametricEQModule::kMaxBands);
}

TEST(EQShortcuts, OtherCommandChordsStillFallThroughToTheApp) {
    Rig rig;
    EXPECT_FALSE(rig.press('z', kCmd));
    EXPECT_FALSE(rig.press('z', kCmd | kShift));
    EXPECT_FALSE(rig.press('c', kCmd));
    EXPECT_FALSE(rig.press(juce::KeyPress::upKey, kCmd));
}

// ---- rebinding ----

TEST(EQShortcuts, ARebindMakesTheNewKeyWorkAndTheOldOneStop) {
    Rig rig;
    rig.select(1);
    rig.shortcuts.setBinding("eqSolo", key('o'));
    EXPECT_TRUE(rig.press('o'));
    EXPECT_EQ(rig.curve->getSoloBand(), 1);
    rig.press('o');
    rig.press('s');
    EXPECT_EQ(rig.curve->getSoloBand(), -1) << "S no longer solos";

    rig.shortcuts.setBinding("eqWidthUp", key(juce::KeyPress::upKey, kCmd | kShift));
    const float q = rig.band(1).q;
    rig.press(juce::KeyPress::upKey);
    EXPECT_EQ(rig.band(1).q, q) << "plain Up no longer widens";
    EXPECT_TRUE(rig.press(juce::KeyPress::upKey, kCmd | kShift));
    EXPECT_LT(rig.band(1).q, q);

    rig.shortcuts.setBinding("eqDuplicateBand", key('j', kCmd));
    EXPECT_TRUE(rig.press('j', kCmd));
    EXPECT_EQ(rig.eq.getEnabledBandCount(), 3);
    EXPECT_FALSE(rig.press('d', kCmd)) << "the old chord goes back to the app";
    EXPECT_EQ(rig.eq.getEnabledBandCount(), 3);

    rig.shortcuts.setBinding("eqAddBand", key('='));
    const int on = rig.eq.getEnabledBandCount();
    EXPECT_TRUE(rig.press('='));
    EXPECT_EQ(rig.eq.getEnabledBandCount(), on + 1);
    EXPECT_FALSE(rig.press(juce::KeyPress::numberPadAdd)) << "the number pad is the plus key's alias only";
}

TEST(EQShortcuts, AClearedBindingMeansNoKeyAndAManagerlessCurveUsesTheDefaults) {
    Rig rig;
    rig.select(1);
    rig.shortcuts.setBinding("eqSolo", juce::KeyPress());
    EXPECT_FALSE(rig.press('s'));
    EXPECT_EQ(rig.curve->getSoloBand(), -1);

    rig.curve->setShortcutManager(nullptr);
    EXPECT_TRUE(rig.press('s'));
    EXPECT_EQ(rig.curve->getSoloBand(), 1);
    EXPECT_EQ(rig.curve->getShortcutManager(), nullptr);
}

// ---- solo ----

TEST(EQShortcuts, SSolosTheSelectedBandAndTheModuleAndTheAudioSeeIt) {
    Rig rig;
    rig.eq.setBandFreq(0, 300.0f);
    rig.eq.setBandGain(0, 6.0f);
    rig.eq.setBandQ(0, 2.0f);
    rig.eq.setBandFreq(1, 5000.0f);
    rig.eq.setBandGain(1, -6.0f);
    rig.eq.setBandQ(1, 2.0f);
    rig.eq.setPlayConfigDetails(17, 17, eqtest::kSampleRate, eqtest::kBlockSize);
    rig.eq.prepareToPlay(eqtest::kSampleRate, eqtest::kBlockSize);
    rig.select(0);
    EXPECT_EQ(rig.curve->getSoloBand(), -1);
    EXPECT_TRUE(rig.press('s'));
    EXPECT_EQ(rig.eq.getSoloBand(), 0);
    EXPECT_TRUE(rig.curve->ownsSolo());
    EXPECT_NEAR(eqtest::measureGainDb(rig.eq, 300.0f, 17), 0.0f, 0.5f) << "the soloed band's centre plays dry";
    EXPECT_LT(eqtest::measureGainDb(rig.eq, 5000.0f, 17), -12.0f) << "the other band's region is dropped";
    EXPECT_EQ(rig.starts, 0) << "a solo is not an edit: no undo step";
    EXPECT_TRUE(rig.press('s'));
    EXPECT_EQ(rig.eq.getSoloBand(), -1);
    EXPECT_FALSE(rig.curve->ownsSolo());
    EXPECT_NEAR(eqtest::measureGainDb(rig.eq, 5000.0f, 17), -6.0f, 1.0f) << "the whole EQ plays again";
}

TEST(EQShortcuts, TheSoloFollowsTheSelectionAndEndsWhenThereIsNothingToHear) {
    Rig rig;
    rig.select(0);
    rig.press('s');
    rig.curve->setSelectedBand(1);
    EXPECT_EQ(rig.eq.getSoloBand(), 1);
    rig.curve->setSelectedBand(5); // off
    EXPECT_EQ(rig.eq.getSoloBand(), -1);
    EXPECT_FALSE(rig.curve->ownsSolo());
    rig.curve->setSelectedBand(0);
    EXPECT_EQ(rig.eq.getSoloBand(), -1) << "a solo that ended stays ended";
}

TEST(EQShortcuts, RemovingTheSoloedBandEndsTheSolo) {
    Rig rig;
    rig.select(1);
    rig.press('s');
    rig.press(juce::KeyPress::backspaceKey);
    EXPECT_EQ(rig.eq.getSoloBand(), -1);
    EXPECT_FALSE(rig.curve->ownsSolo());
    rig.select(0);
    rig.press('s');
    rig.curve->removeBand(0);
    EXPECT_EQ(rig.eq.getSoloBand(), -1);
}

TEST(EQShortcuts, EscapeEndsASoloThisCurveStartedFirstAndOtherwiseFallsThrough) {
    Rig rig;
    rig.select(1);
    EXPECT_FALSE(rig.press(juce::KeyPress::escapeKey)) << "no solo: the window or menu closes";
    rig.press('s');
    EXPECT_TRUE(rig.press(juce::KeyPress::escapeKey));
    EXPECT_EQ(rig.eq.getSoloBand(), -1);
    EXPECT_FALSE(rig.press(juce::KeyPress::escapeKey));

    rig.eq.setSoloBand(0); // a solo some other surface started is not this curve's to end
    EXPECT_FALSE(rig.press(juce::KeyPress::escapeKey));
    EXPECT_EQ(rig.eq.getSoloBand(), 0);
}

TEST(EQShortcuts, TheCurveThatStartedASoloEndsItWhenItGoesAwayButAnotherCurvesSoloStays) {
    ParametricEQModule eq;
    eq.setBandEnabled(0, true);
    eq.setBandEnabled(1, true);
    {
        EQCurveComponent curve(eq);
        curve.setSelectedBand(1);
        ASSERT_TRUE(curve.toggleSoloSelectedBand());
        EXPECT_EQ(eq.getSoloBand(), 1);
    }
    EXPECT_EQ(eq.getSoloBand(), -1) << "audio never stays soloed with nothing on screen";

    eq.setSoloBand(0);
    {
        EQCurveComponent curve(eq);
        curve.setSelectedBand(1);
    }
    EXPECT_EQ(eq.getSoloBand(), 0);

    EQCurveComponent first(eq);
    first.setSelectedBand(1);
    first.toggleSoloSelectedBand();
    eq.setSoloBand(0); // another surface took over: the first curve lets go of its claim
    first.timerCallback();
    EXPECT_FALSE(first.ownsSolo());
}

TEST(EQShortcuts, OptionClickSolosADotAndOptionClickAgainEndsItWithoutStartingADrag) {
    Rig rig;
    const auto dot = rig.curve->getDrawnBandPosition(1);
    rig.curve->mouseDown(mouse(*rig.curve, dot, juce::ModifierKeys::leftButtonModifier | kAlt));
    rig.curve->mouseDrag(
        mouse(*rig.curve, dot + juce::Point<float>(40.0f, 20.0f), juce::ModifierKeys::leftButtonModifier | kAlt));
    rig.curve->mouseUp(mouse(*rig.curve, dot, juce::ModifierKeys::leftButtonModifier | kAlt));
    EXPECT_EQ(rig.eq.getSoloBand(), 1);
    EXPECT_EQ(rig.curve->getSelectedBand(), 1);
    EXPECT_EQ(rig.starts, 0) << "no drag, no undo step";
    EXPECT_EQ(rig.band(1).freqHz, 2000.0f);

    rig.curve->mouseDown(mouse(*rig.curve, dot, juce::ModifierKeys::leftButtonModifier | kAlt));
    rig.curve->mouseUp(mouse(*rig.curve, dot, juce::ModifierKeys::leftButtonModifier | kAlt));
    EXPECT_EQ(rig.eq.getSoloBand(), -1);

    const auto other = rig.curve->getDrawnBandPosition(0);
    rig.curve->mouseDown(mouse(*rig.curve, dot, juce::ModifierKeys::leftButtonModifier | kAlt));
    rig.curve->mouseDown(mouse(*rig.curve, other, juce::ModifierKeys::leftButtonModifier | kAlt));
    EXPECT_EQ(rig.eq.getSoloBand(), 0) << "Option-click on another dot moves the solo there";
}

TEST(EQShortcuts, TheSoloedDotGetsABrightRingAndTheOthersDimThroughAnEaseNotAPop) {
    Rig rig;
    rig.curve->getSoloEaseForTest().forceAnimateForTest(true);
    EXPECT_EQ(rig.curve->getSoloLook(0), juce::Point<float>(0.0f, 0.0f));
    rig.select(1);
    rig.press('s');
    const auto start = rig.curve->getSoloLook(1);
    EXPECT_LT(start.x, 1.0f) << "the ring is still arriving";
    EXPECT_TRUE(rig.curve->getSoloEaseForTest().isAnimating());
    rig.curve->getSoloEaseForTest().stepForTest(40.0);
    const auto mid = rig.curve->getSoloLook(1);
    EXPECT_GT(mid.x, start.x);
    EXPECT_LT(mid.x, 1.0f);
    EXPECT_GT(rig.curve->getSoloLook(0).y, 0.0f) << "the other band starts to dim";
    rig.curve->getSoloEaseForTest().stepForTest(1000.0);
    EXPECT_EQ(rig.curve->getSoloLook(1), juce::Point<float>(1.0f, 0.0f));
    EXPECT_EQ(rig.curve->getSoloLook(0), juce::Point<float>(0.0f, 1.0f));

    juce::Image image(juce::Image::ARGB, 480, 200, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    EXPECT_NO_THROW(rig.curve->paint(g));

    rig.press('s');
    rig.curve->getSoloEaseForTest().stepForTest(1000.0);
    EXPECT_EQ(rig.curve->getSoloLook(1), juce::Point<float>(0.0f, 0.0f));
    EXPECT_EQ(rig.curve->getSoloLook(0), juce::Point<float>(0.0f, 0.0f));
}

// ---- the window ----

TEST(EQShortcuts, TheWindowsSoloButtonFollowsTheSelectionAndTheModulesSoloAndHasANameAndAFocusStop) {
    ParametricEQModule eq;
    eq.setBandEnabled(0, true);
    eq.setBandEnabled(1, true);
    ShortcutManager shortcuts;
    auto window = std::make_unique<EQWindow>(eq);
    window->setShortcutManager(&shortcuts);
    auto& button = window->getSoloButton();
    EXPECT_EQ(button.getTitle(), "Solo band");
    EXPECT_FALSE(button.isEnabled()) << "no band is selected";
    EXPECT_EQ(window->getCurve().getShortcutManager(), &shortcuts);

    window->getCurve().setSelectedBand(1);
    EXPECT_TRUE(button.isEnabled());
    EXPECT_TRUE(button.getWantsKeyboardFocus()) << "a Tab stop once there is a band to solo";
    EXPECT_FALSE(button.getToggleState());
    button.onClick();
    EXPECT_EQ(eq.getSoloBand(), 1);
    EXPECT_TRUE(button.getToggleState());
    eq.setSoloBand(0); // another surface soloed a different band
    window->getCurve().timerCallback();
    EXPECT_FALSE(button.getToggleState()) << "lit only for the selected band";
    button.onClick();
    EXPECT_EQ(eq.getSoloBand(), 1) << "the button solos the selected band";
    button.onClick();
    EXPECT_EQ(eq.getSoloBand(), -1);

    button.onClick();
    window->getCurve().setSelectedBand(0); // the solo this curve started follows the selection
    EXPECT_EQ(eq.getSoloBand(), 0);
    window.reset(); // closing the window
    EXPECT_EQ(eq.getSoloBand(), -1) << "closing the window ends a solo it started";
}

TEST(EQShortcuts, EveryTooltipThatNamesAKeyReadsTheLiveBindingAfterARebind) {
    ParametricEQModule eq;
    eq.setBandEnabled(0, true);
    ShortcutManager shortcuts;
    EQWindow window(eq);
    window.setShortcutManager(&shortcuts);
    window.getCurve().setSelectedBand(0);

    EXPECT_EQ(window.getSoloButton().getTooltip(), "Solo the selected band (s)");
    EXPECT_TRUE(window.getTypePill().getTooltip().contains("(t)"));
    EXPECT_TRUE(window.getStrip().getAddChip()->getTooltip().contains("(+)"));
    EXPECT_TRUE(window.getCurve().getTooltip().contains("s solos it"));
    EXPECT_TRUE(window.getHintForTest().getTooltip().contains("s solos"));

    shortcuts.setBinding("eqSolo", key('h', kAlt));
    shortcuts.setBinding("eqTypeMenu", key('y'));
    shortcuts.setBinding("eqAddBand", key('n'));
    shortcuts.setBinding("eqDuplicateBand", key('b', kCmd));
    const auto altH = ShortcutManager::keyPressToDisplayString(key('h', kAlt));
    EXPECT_EQ(window.getSoloButton().getTooltip(), "Solo the selected band (" + altH + ")");
    EXPECT_TRUE(window.getTypePill().getTooltip().contains("(y)"));
    EXPECT_TRUE(window.getStrip().getAddChip()->getTooltip().contains("(n)"));
    EXPECT_TRUE(window.getCurve().getTooltip().contains(altH + " solos it"));
    EXPECT_TRUE(window.getHintForTest().getTooltip().contains(altH + " solos"));
    EXPECT_TRUE(window.getHintForTest().getTooltip().contains(ShortcutManager::keyPressToDisplayString(key('b', kCmd)) +
                                                              " duplicates"));

    shortcuts.setBinding("eqSolo", juce::KeyPress());
    EXPECT_EQ(window.getSoloButton().getTooltip(), "Solo the selected band") << "an unbound key is not named";
}

// ---- reaching the curve on a card, before the canvas and the app ----

namespace {

struct RecordingParent : juce::Component {
    std::vector<juce::KeyPress> received;
    bool keyPressed(const juce::KeyPress& k) override {
        received.push_back(k);
        return false;
    }
};

EQCurveComponent* findCurve(juce::Component& card) {
    for (auto* child : card.getChildren())
        if (auto* curve = dynamic_cast<EQCurveComponent*>(child))
            return curve;
    return nullptr;
}

} // namespace

// Real key delivery (the window's peer: listeners, then keyPressed from the focused curve up its parents) on a card in
// a canvas. Cmd+D must add a band to the EQ and duplicate no module; Option+arrows must edit the band and not move the
// card, which the canvas's own Option+arrow keys would do.
TEST(EQShortcuts, OnACardCmdDAndOptionArrowsReachTheCurveBeforeTheCanvasAndTheApp) {
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        GTEST_SKIP() << "needs a display";
    if (nativeWindowsAbortHere())
        GTEST_SKIP() << "needs a native window";
    AudioEngine engine;
    AppUndoManager undo;
    RecordingParent parent;
    GraphEditor editor(engine, &undo);
    ShortcutManager shortcuts;
    editor.getCardKeyboard().setShortcutManager(&shortcuts);
    parent.setSize(900, 600);
    parent.addAndMakeVisible(editor);
    editor.setBounds(parent.getLocalBounds());
    auto node = engine.getGraph().addNode(synth::AIStateMapper::createModule("Parametric EQ"));
    editor.updateComponents();
    auto* eq = dynamic_cast<ParametricEQModule*>(node->getProcessor());
    ASSERT_NE(eq, nullptr);
    eq->setBandEnabled(0, true);
    eq->setBandFreq(0, 1000.0f);
    auto* card = findModuleComp(editor, node->getProcessor());
    ASSERT_NE(card, nullptr);
    auto* curve = findCurve(*card);
    ASSERT_NE(curve, nullptr);
    EXPECT_EQ(curve->getShortcutManager(), &shortcuts) << "the card hands its curve the app's manager";

    parent.addToDesktop(0);
    parent.setVisible(true);
    if (!synth::test::real_keys::takeFocus(*curve))
        GTEST_SKIP() << "the window did not give the curve keyboard focus";
    curve->setSelectedBand(0);
    const int modules = (int)engine.getGraph().getNodes().size();
    const auto position = card->getPosition();

    auto delivery = synth::test::real_keys::deliver(*curve, key('d', kCmd));
    EXPECT_TRUE(delivery.handled);
    EXPECT_EQ(delivery.handledBy.getComponent(), curve);
    EXPECT_EQ(eq->getEnabledBandCount(), 2) << "Cmd+D copied the band";
    EXPECT_EQ((int)engine.getGraph().getNodes().size(), modules) << "no module was duplicated";
    EXPECT_TRUE(parent.received.empty()) << "the key never reached the canvas or the app";

    const float before = eq->getBandSnapshots()[0].freqHz;
    curve->setSelectedBand(0);
    delivery = synth::test::real_keys::deliver(*curve, key(juce::KeyPress::rightKey, kAlt));
    EXPECT_TRUE(delivery.handled);
    EXPECT_EQ(delivery.handledBy.getComponent(), curve);
    EXPECT_GT(eq->getBandSnapshots()[0].freqHz, before) << "Option+Right moved the band's frequency";
    EXPECT_EQ(card->getPosition(), position) << "the canvas did not move the card";
    EXPECT_TRUE(parent.received.empty());

    parent.removeFromDesktop();
}
