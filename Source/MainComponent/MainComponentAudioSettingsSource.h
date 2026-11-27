#pragma once

#include "UI/Settings/AudioSettingsTab/AudioSettingsSource.h"
#include <juce_data_structures/juce_data_structures.h>

class AudioEngine;

namespace synth::maincomponent {

/** The user setting that holds Settings > Audio's Render cores (0 Auto, n cores). */
inline constexpr const char* kRenderCoresKey = "renderCores";

// MainComponent's answer to the Settings Audio tab (AudioSettingsSource): the open interface as the engine sees it,
// the engine's input layout store and input levels, the Use tick through the engine's input switch-on, and the
// recording latency a take is shifted by. MIDI lists and the rest come from the device manager.
class EngineSettingsSource : public ::DeviceManagerSettingsSource {
public:
    EngineSettingsSource(AudioEngine& engine, juce::ApplicationProperties& props);

    AudioInterfaceInfo currentInterface() override;
    void noteOpenInterface() override;
    synth::AudioInputLayoutStore& layoutStore() override;
    float readInputPeak(int jack) override;
    void setInputsUsed(int firstJack, int numJacks, bool used) override;
    int renderCores() override;
    void setRenderCores(int renderCores) override;
    /** Message thread, before the device first starts: sets the engine's worker count from the saved setting. */
    void applySavedRenderCores();

private:
    AudioEngine& engine_;
    juce::ApplicationProperties& props_;
};

} // namespace synth::maincomponent
