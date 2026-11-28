// Concern: the engine-backed source of the Settings Audio tab.
#include "MainComponent/MainComponentAudioSettingsSource.h"

#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/RenderCores.h"

namespace synth::maincomponent {

EngineSettingsSource::EngineSettingsSource(AudioEngine& engine, juce::ApplicationProperties& props)
    : ::DeviceManagerSettingsSource(engine.getDeviceManager())
    , engine_(engine)
    , props_(props) {}

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

// Only a live device block ever renders on the workers, so a hosted engine (the plugin) has nothing to offer here.
int EngineSettingsSource::renderCores() {
    if (engine_.isHosted())
        return -1;
    auto* settings = props_.getUserSettings();
    return settings != nullptr ? settings->getIntValue(kRenderCoresKey, synth::kDefaultRenderCores)
                               : synth::kDefaultRenderCores;
}

// setTrackWorkerCount swaps the new pool and plan in under the running device, so the change needs no restart.
void EngineSettingsSource::setRenderCores(int renderCores) {
    if (engine_.isHosted())
        return;
    if (auto* settings = props_.getUserSettings())
        settings->setValue(kRenderCoresKey, std::max(0, renderCores));
    engine_.setTrackWorkerCount(synth::trackWorkersFor(renderCores, machineCores()));
}

void EngineSettingsSource::applySavedRenderCores() {
    if (const int saved = renderCores(); saved >= 0)
        engine_.setTrackWorkerCount(synth::trackWorkersFor(saved, machineCores()));
}

} // namespace synth::maincomponent
