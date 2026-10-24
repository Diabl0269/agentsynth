// Concern: the engine-backed source of the Settings Audio tab.
#include "MainComponent/MainComponentAudioSettingsSource.h"

#include "AudioEngine/AudioEngine.h"

namespace synth::maincomponent {

EngineSettingsSource::EngineSettingsSource(AudioEngine& engine)
    : ::DeviceManagerSettingsSource(engine.getDeviceManager())
    , engine_(engine) {}

AudioInterfaceInfo EngineSettingsSource::currentInterface() {
    const auto input = engine_.getAudioInputInfo();
    AudioInterfaceInfo info;
    info.deviceType = engine_.getDeviceManager().getCurrentAudioDeviceType();
    info.deviceName = input.deviceName;
    info.numInputs = input.deviceInputs;
    info.usedMask = input.activeMask;
    if (auto* device = engine_.getDeviceManager().getCurrentAudioDevice()) {
        info.sampleRate = device->getCurrentSampleRate();
        info.bufferSamples = device->getCurrentBufferSizeSamples();
    }
    info.recordingLatencySamples = engine_.getRecordingLatencySamples();
    return info;
}

// Only a first sighting is recorded here: the engine stamps the interface as used on every device change, and a
// refresh of the tab must not re-save the layout.
void EngineSettingsSource::noteOpenInterface() {
    const auto info = currentInterface();
    if (info.deviceName.isNotEmpty() && !engine_.getInputLayoutStore().has(info.deviceType, info.deviceName))
        engine_.noteOpenInterface();
}

synth::AudioInputLayoutStore& EngineSettingsSource::layoutStore() { return engine_.getInputLayoutStore(); }

float EngineSettingsSource::readInputPeak(int jack) { return engine_.readInputPeak(jack); }

void EngineSettingsSource::setInputsUsed(int firstJack, int numJacks, bool used) {
    engine_.setDeviceInputsUsed(firstJack, numJacks, used);
}

} // namespace synth::maincomponent
