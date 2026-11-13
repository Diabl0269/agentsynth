#pragma once

#include "UI/Settings/AudioSettingsTab/AudioSettingsSource.h"

class AudioEngine;

namespace synth::maincomponent {

// MainComponent's answer to the Settings Audio tab (AudioSettingsSource): the open interface as the engine sees it,
// the engine's input layout store and input levels, the Use tick through the engine's input switch-on, and the
// recording latency a take is shifted by. MIDI lists and the rest come from the device manager.
class EngineSettingsSource : public ::DeviceManagerSettingsSource {
public:
    explicit EngineSettingsSource(AudioEngine& engine);

    AudioInterfaceInfo currentInterface() override;
    void noteOpenInterface() override;
    synth::AudioInputLayoutStore& layoutStore() override;
    float readInputPeak(int jack) override;
    void setInputsUsed(int firstJack, int numJacks, bool used) override;

private:
    AudioEngine& engine_;
};

} // namespace synth::maincomponent
