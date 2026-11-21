// EQCurveReadoutVisibilityTests.cpp -- when the EQ curve's floating value readout shows: while the pointer is over the
// dot or over its chip, while it is dragged, and for a second after the last single-band change (a key nudge, a Q
// step, a drag). A selection alone never shows it, and a group has no single value to show.
#include "Modules/FX/ParametricEQModule.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/EQBandStrip.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/EQWindow.h"
#include <gtest/gtest.h>

namespace {

struct Rig {
    Rig() {
        const float freqs[] = {200.0f, 1000.0f, 4000.0f};
        for (int b = 0; b < 3; ++b) {
            eq.setBandFreq(b, freqs[b]);
            eq.setBandGain(b, 0.0f);
            eq.setBandEnabled(b, true);
        }
        window = std::make_unique<EQWindow>(eq);
        window->setBounds(0, 0, 820, 470);
        curve().setReadoutClockForTest([this] { return nowMs; });
    }
    EQCurveComponent& curve() { return window->getCurve(); }
    juce::Point<float> dot(int b) { return curve().bandPosition(eq.getBandSnapshots()[(size_t)b]); }
    juce::MouseEvent event(juce::Component& on, juce::Point<float> p, bool dragged, int mods = 0) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(mods), 0.0f,
                                0.0f, 0.0f, 0.0f, 0.0f, &on, &on, juce::Time::getCurrentTime(), p,
                                juce::Time::getCurrentTime(), 1, dragged);
    }
    void clickAndLeave(int band) {
        const int left = juce::ModifierKeys::leftButtonModifier;
        curve().mouseDown(event(curve(), dot(band), false, left));
        curve().mouseUp(event(curve(), dot(band), false, left));
        curve().mouseExit(event(curve(), dot(band), false));
    }
    bool press(int code, int mods = 0) { return curve().keyPressed(juce::KeyPress(code, juce::ModifierKeys(mods), 0)); }
    bool showing(int band) const {
        const auto bands = const_cast<Rig*>(this)->curve().getReadoutBands();
        return std::find(bands.begin(), bands.end(), band) != bands.end();
    }

    ParametricEQModule eq;
    std::unique_ptr<EQWindow> window;
    double nowMs = 10000.0;
};

} // namespace

TEST(EQReadoutVisibility, SelectingABandShowsNoReadout) {
    Rig rig;
    rig.clickAndLeave(1);
    ASSERT_EQ(rig.curve().getSelectedBand(), 1);
    EXPECT_TRUE(rig.curve().getReadoutBands().empty()) << "selection alone is not a reason to show a value";
    rig.curve().setSelectedBand(2);
    EXPECT_TRUE(rig.curve().getReadoutBands().empty());
}

TEST(EQReadoutVisibility, HoveringTheDotShowsItAndLeavingHidesIt) {
    Rig rig;
    rig.curve().mouseMove(rig.event(rig.curve(), rig.dot(1), false));
    EXPECT_TRUE(rig.showing(1));
    rig.curve().mouseExit(rig.event(rig.curve(), rig.dot(1), false));
    EXPECT_TRUE(rig.curve().getReadoutBands().empty());
}

TEST(EQReadoutVisibility, HoveringTheChipShowsTheBandsReadoutOnTheCurve) {
    Rig rig;
    juce::Component* chip = rig.window->getStrip().getChip(1);
    ASSERT_NE(chip, nullptr);
    chip->mouseEnter(rig.event(*chip, {5.0f, 5.0f}, false));
    EXPECT_TRUE(rig.showing(1)) << "the chip hover is forwarded to its dot";
    chip->mouseExit(rig.event(*chip, {5.0f, 5.0f}, false));
    EXPECT_TRUE(rig.curve().getReadoutBands().empty());
}

TEST(EQReadoutVisibility, ARetiredChipTakesItsForwardedHoverWithIt) {
    Rig rig;
    juce::Component* chip = rig.window->getStrip().getChip(1);
    ASSERT_NE(chip, nullptr);
    chip->mouseEnter(rig.event(*chip, {5.0f, 5.0f}, false));
    rig.curve().removeBand(1);
    EXPECT_EQ(rig.curve().getChipHoveredBand(), -1);
}

TEST(EQReadoutVisibility, ADragShowsItAndItLingersForASecondAfterTheRelease) {
    Rig rig;
    const int left = juce::ModifierKeys::leftButtonModifier;
    const auto from = rig.dot(1);
    rig.curve().mouseDown(rig.event(rig.curve(), from, false, left));
    rig.curve().mouseDrag(rig.event(rig.curve(), from + juce::Point<float>(12.0f, -8.0f), true, left));
    EXPECT_TRUE(rig.showing(1)) << "while it is dragged";
    rig.curve().mouseUp(rig.event(rig.curve(), from + juce::Point<float>(12.0f, -8.0f), true, left));
    rig.curve().mouseExit(rig.event(rig.curve(), from, false));
    rig.nowMs += 900.0;
    rig.curve().timerCallback();
    EXPECT_TRUE(rig.showing(1)) << "still within the second after the last change";
    rig.nowMs += 200.0;
    rig.curve().timerCallback();
    EXPECT_FALSE(rig.showing(1)) << "about a second after it, it goes";
}

TEST(EQReadoutVisibility, AKeyNudgeOfOneBandShowsItForASecondFromTheLastStep) {
    Rig rig;
    rig.curve().setSelectedBand(1);
    ASSERT_TRUE(rig.curve().getReadoutBands().empty());
    ASSERT_TRUE(rig.press(juce::KeyPress::upKey, juce::ModifierKeys::altModifier));
    EXPECT_TRUE(rig.showing(1));
    rig.nowMs += 800.0;
    ASSERT_TRUE(rig.press(juce::KeyPress::upKey, juce::ModifierKeys::altModifier));
    rig.nowMs += 800.0;
    EXPECT_TRUE(rig.showing(1)) << "each step starts the second again";
    rig.nowMs += 300.0;
    EXPECT_FALSE(rig.showing(1));
}

TEST(EQReadoutVisibility, AQStepShowsTheReadoutToo) {
    Rig rig;
    rig.curve().setSelectedBand(1);
    ASSERT_TRUE(rig.press(juce::KeyPress::pageUpKey));
    EXPECT_TRUE(rig.showing(1));
}

TEST(EQReadoutVisibility, AGroupMoveShowsNoReadout) {
    Rig rig;
    rig.curve().setSelection({0, 1}, 1, 1);
    ASSERT_TRUE(rig.press(juce::KeyPress::upKey, juce::ModifierKeys::altModifier));
    EXPECT_TRUE(rig.curve().getReadoutBands().empty()) << "two dots have no single value to show";
}

TEST(EQReadoutVisibility, WithAnimationsOffTheReadoutLandsAtOnce) {
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    Rig rig;
    rig.curve().mouseMove(rig.event(rig.curve(), rig.dot(1), false));
    EXPECT_FLOAT_EQ(rig.curve().getReadoutLook(1), 1.0f);
    rig.curve().mouseExit(rig.event(rig.curve(), rig.dot(1), false));
    EXPECT_FLOAT_EQ(rig.curve().getReadoutLook(1), 0.0f);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
}
