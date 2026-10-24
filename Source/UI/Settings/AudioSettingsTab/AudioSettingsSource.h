#pragma once

#include "AudioEngine/AudioInputLayoutStore.h"
#include <cstdint>
#include <juce_audio_devices/juce_audio_devices.h>
#include <vector>

// What the Settings Audio tab asks of the machine and the app, behind one seam so a test feeds it a fake interface
// and never depends on the audio hardware it runs on. MainComponent answers it from its AudioEngine
// (MainComponentAudioSettingsSource.h); the tab falls back to DeviceManagerSettingsSource when it is given none.
// Message thread only.
struct AudioInterfaceInfo {
    juce::String deviceType;         // "CoreAudio"
    juce::String deviceName;         // the interface the inputs belong to; empty when none is open
    int numInputs = 0;               // jacks the interface has (at most synth::AudioInputLayoutStore::kMaxJacks shown)
    std::uint32_t usedMask = 0;      // bit c set = jack c is turned on
    double sampleRate = 0.0;         // Hz, 0 when nothing is open
    int bufferSamples = 0;           // 0 when nothing is open
    int recordingLatencySamples = 0; // input + graph + output, as a take is shifted by it
};

struct AudioMidiDevice {
    juce::String identifier;
    juce::String name;
    bool enabled = false; // inputs: ticked (feeds the patch); outputs: the chosen one
};

class AudioSettingsSource {
public:
    virtual ~AudioSettingsSource() = default;

    virtual AudioInterfaceInfo currentInterface() = 0;
    /** Records the open interface as used (a first sighting gets an empty entry). */
    virtual void noteOpenInterface() = 0;
    virtual synth::AudioInputLayoutStore& layoutStore() = 0;
    /** The loudest sample on 0-based jack since the last read (reading resets it). */
    virtual float readInputPeak(int jack) = 0;
    /** The "Use" tick: turns `numJacks` jacks from `firstJack` on or off on the device, leaving the rest as they are.
     */
    virtual void setInputsUsed(int firstJack, int numJacks, bool used) = 0;

    virtual std::vector<AudioMidiDevice> midiInputs() = 0;
    virtual void setMidiInputEnabled(const juce::String& identifier, bool enabled) = 0;
    /** The outputs on offer; the one `enabled` is the chosen one (none = no MIDI output). */
    virtual std::vector<AudioMidiDevice> midiOutputs() = 0;
    virtual void setMidiOutput(const juce::String& identifier) = 0;
};

// The source for a tab given none: reads the juce::AudioDeviceManager directly, keeps its own layout store, has no
// engine to read levels or latency from.
class DeviceManagerSettingsSource : public AudioSettingsSource {
public:
    explicit DeviceManagerSettingsSource(juce::AudioDeviceManager& deviceManager)
        : deviceManager_(deviceManager) {}

    AudioInterfaceInfo currentInterface() override;
    void noteOpenInterface() override;
    synth::AudioInputLayoutStore& layoutStore() override { return store_; }
    float readInputPeak(int) override { return 0.0f; }
    void setInputsUsed(int firstJack, int numJacks, bool used) override;
    std::vector<AudioMidiDevice> midiInputs() override;
    void setMidiInputEnabled(const juce::String& identifier, bool enabled) override;
    std::vector<AudioMidiDevice> midiOutputs() override;
    void setMidiOutput(const juce::String& identifier) override;

private:
    juce::AudioDeviceManager& deviceManager_;
    synth::AudioInputLayoutStore store_;
};
