#pragma once

#include "DrumVoices.h"
#include "Modules/ModuleBase.h"
#include <array>

/**
 * Synthesized electronic drum kit on one MIDI track.
 *
 * Each MIDI note plays a drum (General MIDI numbers), so one track carries a whole beat:
 *
 *   | notes      | drum                       | group parameters         |
 *   | :--------- | :------------------------- | :----------------------- |
 *   | 35, 36     | kick                       | Kick level/tune/decay    |
 *   | 38, 40     | snare                      | Snare level/tune/decay   |
 *   | 37         | rim click                  | Snare level/tune         |
 *   | 39         | clap                       | Clap level/tune/decay    |
 *   | 42, 44     | closed / pedal hat         | Hat level/tune, Closed decay |
 *   | 46         | open hat (choked by 42/44) | Hat level/tune, Open decay   |
 *   | 41 43 45 47 48 50 | toms, low to high   | Tom level/tune/decay     |
 *   | 49, 57     | crash                      | Cymbal level/tune/decay  |
 *   | 51, 59     | ride                       | Cymbal level/tune/decay  |
 *   | 56         | cowbell                    | Cowbell level/tune/decay |
 *
 * Any other note is ignored. A drum plays out its own decay whatever the note length, so Note-Off
 * does nothing. This module owns its amp envelopes: there is no ADSR or VCA in front of it.
 * Output is mono-summed onto a stereo pair (Declared, ships SPLIT like the Sampler).
 *
 * Hit settings (level, tune, decay) are read when a drum is struck; only the master Level is live.
 */
class DrumKitModule : public ModuleBase {
public:
    static constexpr int kNumChannels = 2;

    /** The note-to-drum map, exposed for tests and docs. */
    enum class Drum { Kick, Snare, Rim, Clap, ClosedHat, OpenHat, Tom, Crash, Ride, Cowbell, None };
    static Drum drumForNote(int note) noexcept;

    DrumKitModule();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::String getOutputPortLabel(int i) const override {
        return isDualIO() ? (i == 1 ? "Audio R" : "Audio L") : "Audio";
    }
    int getVisibleInputPortCount() const override { return 0; }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Oscillator; }
    ModuleType getModuleType() const override { return ModuleType::DrumKit; }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

private:
    using Hit = synth::drums::Hit;

    juce::AudioParameterFloat* addFloat(const char* id, const char* name, float lo, float hi, float def);
    void trigger(int note, float velocity);
    Hit hitFor(juce::AudioParameterFloat* level, juce::AudioParameterFloat* tune, float decaySeconds,
               float velocity) const;
    void stopAll() noexcept;
    float renderSample() noexcept;

    juce::AudioParameterFloat* kickLevel = nullptr;
    juce::AudioParameterFloat* kickTune = nullptr;
    juce::AudioParameterFloat* kickDecay = nullptr;
    juce::AudioParameterFloat* snareLevel = nullptr;
    juce::AudioParameterFloat* snareTune = nullptr;
    juce::AudioParameterFloat* snareDecay = nullptr;
    juce::AudioParameterFloat* clapLevel = nullptr;
    juce::AudioParameterFloat* clapTune = nullptr;
    juce::AudioParameterFloat* clapDecay = nullptr;
    juce::AudioParameterFloat* hatLevel = nullptr;
    juce::AudioParameterFloat* hatTune = nullptr;
    juce::AudioParameterFloat* closedDecay = nullptr;
    juce::AudioParameterFloat* openDecay = nullptr;
    juce::AudioParameterFloat* tomLevel = nullptr;
    juce::AudioParameterFloat* tomTune = nullptr;
    juce::AudioParameterFloat* tomDecay = nullptr;
    juce::AudioParameterFloat* cymbalLevel = nullptr;
    juce::AudioParameterFloat* cymbalTune = nullptr;
    juce::AudioParameterFloat* cymbalDecay = nullptr;
    juce::AudioParameterFloat* cowbellLevel = nullptr;
    juce::AudioParameterFloat* cowbellTune = nullptr;
    juce::AudioParameterFloat* cowbellDecay = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;

    static constexpr int kNumToms = 6;

    double sampleRate_ = 44100.0;
    synth::drums::NoiseSource noise_;
    synth::drums::KickVoice kick_;
    synth::drums::SnareVoice snare_, rim_;
    synth::drums::ClapVoice clap_;
    synth::drums::CowbellVoice cowbell_;
    synth::drums::MetallicVoice closedHat_, openHat_, crash_, ride_;
    std::array<synth::drums::TomVoice, kNumToms> toms_;
    juce::SmoothedValue<float> smoothedLevel_;
};
