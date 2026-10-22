#pragma once

#include "SoundBrowser/SoundEntry.h"
#include "UI/Library/SoundBrowser/SoundWaveformView.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/** The lower part of the Sounds view's footer: the selected sound's waveform with its playhead, a Stop button, the
 *  preview's own volume and the Auto-play switch. It owns no audio: the browser tells it what plays. */
class SoundPreviewFooter : public juce::Component {
public:
    static constexpr int kHeight = 108;

    SoundPreviewFooter();

    /** Shows the waveform of `entry` (none clears it). */
    void showSound(const std::optional<synth::soundbrowser::SoundEntry>& entry);
    void setPlayer(const synth::soundbrowser::SoundPreviewPlayer* player) { wave_.setPlayer(player); }
    void followRequest(std::uint64_t requestId) { wave_.followRequest(requestId); }

    bool isAutoPlay() const { return autoPlay_.getToggleState(); }
    void setAutoPlay(bool on) { autoPlay_.setToggleState(on, juce::dontSendNotification); }
    float getGainDb() const { return (float)volume_.getValue(); }
    void setGainDb(float db) { volume_.setValue(db, juce::dontSendNotification); }

    /** The user flipped Auto-play, moved the volume or pressed Stop. */
    std::function<void(bool)> onAutoPlayChanged;
    std::function<void(float)> onGainChanged;
    std::function<void()> onStop;

    SoundWaveformView& getWaveform() { return wave_; }
    juce::Slider& getVolume() { return volume_; }
    juce::ToggleButton& getAutoPlay() { return autoPlay_; }
    juce::TextButton& getStopButton() { return stop_; }

    void resized() override;

private:
    SoundWaveformView wave_;
    juce::TextButton stop_{"Stop"};
    juce::ToggleButton autoPlay_{"Auto-play"};
    juce::Slider volume_{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundPreviewFooter)
};

} // namespace synth::ui
