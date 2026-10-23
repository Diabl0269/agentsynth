// Concern: the engine-backed TrackAudioInputSurface the routing pane's audio Input row reads.
#include "MainComponent/MainComponentAudioInputSurface.h"

#include "AudioEngine/AudioEngine.h"

namespace synth::maincomponent {

AudioInputSurface::AudioInputSurface(AudioEngine& engine, std::function<void()> openAudioSettings)
    : engine_(engine)
    , openAudioSettings_(std::move(openAudioSettings)) {}

synth::AudioInputStatus AudioInputSurface::getAudioInputStatus() { return engine_.getAudioInputInfo(); }

float AudioInputSurface::readInputPeak(int channel) { return engine_.readInputPeak(channel); }

void AudioInputSurface::enableDeviceInputs() { engine_.enableDeviceInputs(); }

// A plugin has no Audio tab (its host owns the device), so nothing opens there.
void AudioInputSurface::openAudioSettings() {
    if (!engine_.isHosted() && openAudioSettings_)
        openAudioSettings_();
}

} // namespace synth::maincomponent
