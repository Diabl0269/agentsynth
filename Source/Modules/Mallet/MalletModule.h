#pragma once

#include "MalletVoices.h"
#include "Modules/ModuleBase.h"
#include <array>

/**
 * Struck-bar instrument: modal synthesis with eight voices (marimba, vibraphone, xylophone, glockenspiel,
 * bell). Each note-on strikes a bar at the note's pitch (12-TET, A4 = 440 Hz, plus Tune and Fine); velocity
 * sets the loudness and, by Velocity brightness, how hard the mallet hits. Note-Off damps the bar over the
 * Release time. When all eight bars are busy the oldest is taken over.
 *
 * Everything but Level and Tremolo is read when a note starts (Release when it ends). Tremolo is a
 * free-running 5.5 Hz amplitude wobble on the whole output, like a vibraphone's motor. No ADSR or VCA is
 * needed in front of it. Output is mono-summed onto a stereo pair (Declared, ships SPLIT like the Drum Kit).
 * The partial tables and formulas are in docs/modules/pluck-mallet.md.
 */
class MalletModule : public ModuleBase {
public:
    static constexpr int kNumChannels = 2;
    static constexpr float kTremoloHz = 5.5f;

    MalletModule();

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
    ModuleType getModuleType() const override { return ModuleType::Mallet; }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    /** Bars still sounding (held or ringing out); exposed for tests. */
    int activeVoiceCount() const noexcept;

private:
    juce::AudioParameterFloat* addFloat(const char* id, const char* name, float lo, float hi, float def,
                                        float skew = 1.0f);
    void noteOn(int note, float velocity);
    void noteOff(int note);
    void stopAll() noexcept;
    float renderSample() noexcept;

    juce::AudioParameterChoice* materialParam = nullptr;
    juce::AudioParameterFloat* tuneParam = nullptr;
    juce::AudioParameterFloat* fineParam = nullptr;
    juce::AudioParameterFloat* decayParam = nullptr;
    juce::AudioParameterFloat* dampingParam = nullptr;
    juce::AudioParameterFloat* brightnessParam = nullptr;
    juce::AudioParameterFloat* hardnessParam = nullptr;
    juce::AudioParameterFloat* resonatorParam = nullptr;
    juce::AudioParameterFloat* tremoloParam = nullptr;
    juce::AudioParameterFloat* releaseParam = nullptr;
    juce::AudioParameterFloat* velBrightParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;

    double sampleRate_ = 44100.0;
    double tremoloPhase_ = 0.0;
    std::uint32_t noteCounter_ = 0;
    synth::drums::NoiseSource noise_;
    std::array<synth::mallet::MalletVoice, synth::mallet::kNumVoices> voices_;
    juce::SmoothedValue<float> smoothedLevel_, smoothedTremolo_;
};
