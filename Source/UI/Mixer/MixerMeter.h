#pragma once

#include "MixerMeterBallistics.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// MixerMeter.h -- FRO146 (docs/mixer/mixer.md meters section): a column's stereo peak meter, Cubase-
// MixConsole style -- two bars (L/R) on a -60..+3 dB scale (dB-linear position, see
// MixerMeterScale.h), tick marks, a peak-hold line per bar, and a colour that steps through four
// zones by level (MeterColourStops.h). Reads ChannelStripModule/MasterModule::takeMeterPeak via
// `peakProvider` (the caller supplies its own MeterReader -- see PeakMeterLatch.h -- so this class
// never needs to know which reader it is).
//
// Driven by MixerPanelComponent's refresh(), itself polled from MainComponent's existing 10 Hz
// timer while the mixer tab is showing (docs/layout/rendering.md's precedent for the
// Timeline panel's own tick-riding transport poll) -- NOT a new AnimationDriver/timer of its own.
// refresh(elapsedSeconds) advances each bar's ballistics by the caller-measured elapsed time (so
// they stay rate-independent of the poll's actual, tab-visibility-gated cadence) and repaints only
// once the drawn state has moved past a coarse threshold -- the same gated-repaint shape
// ChannelChipComponent::setMeterLevel() already uses for the track header's channel chip.

namespace synth::ui {

class MixerMeter : public juce::Component {
public:
    MixerMeter() = default;

    /** Drawn-dB delta below which a tick's ballistics update is not worth a repaint -- the bars
     *  are a handful of px tall, so anything finer is invisible. */
    static constexpr float kRepaintThresholdDb = 0.2f;

    /** `leg`: 0 = Left, 1 = Right -- the caller has already resolved its own MeterReader (see
     *  PeakMeterLatch.h) before this is called; MixerMeter itself never reads a module directly. */
    std::function<float(int leg)> peakProvider;

    /** One tick: reads both legs, advances each bar's ballistics by `elapsedSeconds`, and repaints
     *  only past kRepaintThresholdDb. */
    void refresh(float elapsedSeconds);

    float getDisplayedDbForTest(int leg) const noexcept { return ballistics_[legIndex(leg)].displayedDb; }
    float getPeakHoldDbForTest(int leg) const noexcept { return ballistics_[legIndex(leg)].peakHoldDb; }

    void paint(juce::Graphics& g) override;

    /** FRO146: a read-only staticText handler reporting the current displayed level in dB (the
     *  louder of the two bars), not a percentage -- percent meant nothing on a dB scale. */
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    static int legIndex(int leg) noexcept { return leg == 1 ? 1 : 0; }

    std::array<MeterBallisticsState, 2> ballistics_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerMeter)
};

} // namespace synth::ui
