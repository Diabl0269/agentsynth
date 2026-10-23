#pragma once

#include "UI/Timeline/TrackAudioInputSurface.h"
#include <functional>

class AudioEngine;

namespace synth::maincomponent {

// MainComponent's answer to the routing pane's audio Input row (synth::ui::TrackAudioInputSurface): the engine's
// inputs, levels and input switch-on, plus opening Settings on its Audio tab.
class AudioInputSurface : public synth::ui::TrackAudioInputSurface {
public:
    AudioInputSurface(AudioEngine& engine, std::function<void()> openAudioSettings);

    synth::AudioInputStatus getAudioInputStatus() override;
    float readInputPeak(int channel) override;
    void enableDeviceInputs() override;
    void openAudioSettings() override;

private:
    AudioEngine& engine_;
    std::function<void()> openAudioSettings_;
};

} // namespace synth::maincomponent
