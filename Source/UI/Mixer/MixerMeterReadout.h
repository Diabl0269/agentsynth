#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>

// MixerMeterReadout.h -- FRO146 (Cubase's "Meter Peak Level" field): one column's numeric max-peak
// readout, sitting near its meter/fader. Shows the highest peak (dBFS) seen since the last reset --
// "-3.2", "+4.1", or "-inf" before anything has rendered -- and turns the clip colour once any
// peak exceeds 0 dBFS, STAYING that colour across every later quiet tick until reset.
//
// Click resets THIS column's readout; Option/Alt-click fires onResetAllRequested instead, which
// MixerColumnComponent/MixerMasterColumn forward up to MixerPanelComponent to reset every column --
// the same fan-out the mixer panel header's own "Reset Meters" button performs directly.
namespace synth::ui {

class MixerMeterReadout : public juce::Component {
public:
    MixerMeterReadout();

    /** One tick: `peakDb` is the loudest of the meter's bars this tick (already dBFS, see
     *  MixerMeterScale.h's meterLinearToDb). Raises the readout's own running max and, once it
     *  crosses 0 dBFS, latches the clip colour -- a later quiet tick never clears it, only
     *  reset() does. A peak at or below the floor (nothing rendered yet) leaves the readout
     *  unchanged. */
    void updatePeak(float peakDb);

    /** Back to "-inf", not clipped. */
    void reset();

    bool isClippedForTest() const noexcept { return clipped_; }
    juce::String getDisplayTextForTest() const { return displayText_; }

    std::function<void()> onResetAllRequested;

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& event) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void refreshDisplayText();

    float maxPeakDb_ = -std::numeric_limits<float>::infinity();
    bool clipped_ = false;
    juce::String displayText_{"-inf"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerMeterReadout)
};

} // namespace synth::ui
