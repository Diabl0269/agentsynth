// MixerMeterReadoutTests.cpp -- FRO146: the per-column clip readout (Cubase's "Meter Peak Level"
// field, Source/UI/Mixer/MixerMeterReadout.h). Click-to-reset is driven through the REAL mouse
// path (a synthesized juce::MouseEvent into mouseUp()), not by calling reset() directly, so the
// modifier-key branch (plain click vs Option/Alt-click) is actually exercised.
#include "UI/Mixer/MixerMeterReadout.h"
#include <gtest/gtest.h>

using synth::ui::MixerMeterReadout;

namespace {

/** Same recipe MixerColumnComponentTests.cpp's own synthesizeMouseUp uses -- a real mouseUp()
 *  call with a synthesized event, optionally carrying the Alt/Option modifier. */
void synthesizeMouseUp(juce::Component& component, bool altDown) {
    const juce::Point<int> centre(component.getWidth() / 2, component.getHeight() / 2);
    const auto mods =
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | (altDown ? juce::ModifierKeys::altModifier : 0));
    component.mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre.toFloat(), mods, 0.0f,
                                       0.0f, 0.0f, 0.0f, 0.0f, &component, &component, juce::Time::getCurrentTime(),
                                       centre.toFloat(), juce::Time::getCurrentTime(), 1, false));
}

} // namespace

TEST(MixerMeterReadoutTest, StartsAtNegativeInfinityNotClipped) {
    MixerMeterReadout readout;
    EXPECT_EQ(readout.getDisplayTextForTest(), "-inf");
    EXPECT_FALSE(readout.isClippedForTest());
}

TEST(MixerMeterReadoutTest, TracksTheRunningMaxNotTheLatestPeak) {
    MixerMeterReadout readout;
    readout.updatePeak(-3.2f);
    readout.updatePeak(-10.0f); // quieter -- the max must not drop
    EXPECT_EQ(readout.getDisplayTextForTest(), "-3.2");
}

TEST(MixerMeterReadoutTest, TurnsClippedAboveZeroDbfsAndStaysClippedAcrossLaterQuietTicks) {
    MixerMeterReadout readout;
    readout.updatePeak(4.1f);
    EXPECT_TRUE(readout.isClippedForTest());
    EXPECT_EQ(readout.getDisplayTextForTest(), "+4.1");

    readout.updatePeak(-40.0f); // silence, several ticks later
    EXPECT_TRUE(readout.isClippedForTest()) << "clip is sticky -- only reset() clears it";
    EXPECT_EQ(readout.getDisplayTextForTest(), "+4.1") << "the max is unaffected by a later quiet tick";
}

TEST(MixerMeterReadoutTest, ResetGoesBackToNegativeInfinityNotClipped) {
    MixerMeterReadout readout;
    readout.updatePeak(6.0f);
    ASSERT_TRUE(readout.isClippedForTest());
    readout.reset();
    EXPECT_FALSE(readout.isClippedForTest());
    EXPECT_EQ(readout.getDisplayTextForTest(), "-inf");
}

TEST(MixerMeterReadoutTest, APlainClickResetsThisReadoutThroughTheRealMousePath) {
    MixerMeterReadout readout;
    readout.setSize(30, 12);
    readout.updatePeak(2.0f);
    ASSERT_TRUE(readout.isClippedForTest());

    bool resetAllFired = false;
    readout.onResetAllRequested = [&] { resetAllFired = true; };

    synthesizeMouseUp(readout, /*altDown=*/false);

    EXPECT_FALSE(readout.isClippedForTest()) << "a plain click resets THIS readout";
    EXPECT_EQ(readout.getDisplayTextForTest(), "-inf");
    EXPECT_FALSE(resetAllFired) << "a plain click must not fire the reset-all fan-out";
}

TEST(MixerMeterReadoutTest, AnAltClickFiresResetAllInsteadOfResettingItself) {
    MixerMeterReadout readout;
    readout.setSize(30, 12);
    readout.updatePeak(2.0f);
    ASSERT_TRUE(readout.isClippedForTest());

    bool resetAllFired = false;
    readout.onResetAllRequested = [&] { resetAllFired = true; };

    synthesizeMouseUp(readout, /*altDown=*/true);

    EXPECT_TRUE(resetAllFired) << "an Option/Alt-click fans out to reset every column";
    EXPECT_TRUE(readout.isClippedForTest()) << "and leaves THIS readout untouched -- the fan-out's "
                                               "caller is responsible for resetting it too";
}
