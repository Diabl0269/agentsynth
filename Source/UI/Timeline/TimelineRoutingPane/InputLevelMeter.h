#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The thin level meter under an audio track's Input combo: one bar per chosen input (two for a stereo pair), on the
// mixer meters' dB scale and colour zones. Decoration: no focus and no screen-reader node (the combo names the
// input). It has no timer; the pane feeds it from its shared 15 Hz tick and it repaints only when a bar moved.
class InputLevelMeter : public juce::Component {
public:
    InputLevelMeter();

    /** How many bars (1 or 2); bars beyond it are not drawn. */
    void setChannelCount(int count);
    int getChannelCount() const noexcept { return channels_; }
    /** One tick with LINEAR peaks; true only when a bar moved far enough to repaint (and one was issued). */
    bool setLevels(float left, float right);
    /** The drawn level of bar `index` as a 0..1 fraction of the meter scale. */
    float getLevelFraction(int index) const noexcept { return fractions_[(std::size_t)(index == 1 ? 1 : 0)]; }

    void paint(juce::Graphics& g) override;

private:
    int channels_ = 1;
    std::array<float, 2> fractions_{};
    std::array<float, 2> dbs_{-60.0f, -60.0f};
};

} // namespace synth::ui
