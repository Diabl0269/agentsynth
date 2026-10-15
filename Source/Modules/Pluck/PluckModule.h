#pragma once

#include "Modules/ModuleBase.h"
#include "PluckVoices.h"
#include <array>

/**
 * Plucked-string instrument: an extended Karplus-Strong string with eight voices (guitar, harp, koto).
 * Each note-on starts a string at the note's pitch (12-TET, A4 = 440 Hz, plus Tune and Fine); velocity sets
 * the loudness and, by Velocity brightness, how hard and bright the pluck is. Note-Off damps the string over
 * the Release time. When all eight strings are busy the oldest is taken over.
 *
 * Decay, Decay tracking, Brightness, Pick position, Pick hardness, Stiffness, Tune and Fine are read when a note
 * starts; Release when it ends. Level and Body are live. No ADSR or VCA is needed in front of it. Output is mono-summed
 * onto a stereo pair (Declared, ships SPLIT like the Drum Kit). The algorithm and the formulas are in
 * docs/modules/pluck-mallet.md.
 */
class PluckModule : public ModuleBase {
public:
    static constexpr int kNumChannels = 2;
    static constexpr double kDecayReferenceHz = 130.8128; // C3: where Decay is exactly the T60

    PluckModule();

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
    ModuleType getModuleType() const override { return ModuleType::Pluck; }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    /** Strings still sounding (held or ringing out); exposed for tests. */
    int activeVoiceCount() const noexcept;

private:
    juce::AudioParameterFloat* addFloat(const char* id, const char* name, float lo, float hi, float def,
                                        float skew = 1.0f);
    void noteOn(int note, float velocity);
    void noteOff(int note);
    void stopAll() noexcept;
    float renderSample() noexcept;

    /** One resonant bandpass of the body (peak gain 1 at its centre). */
    struct BodyBand {
        double b0 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set(double hz, double q, double sampleRate) noexcept;
        double process(double x) noexcept;
        void reset() noexcept { z1 = z2 = 0.0; }
    };

    juce::AudioParameterFloat* tuneParam = nullptr;
    juce::AudioParameterFloat* fineParam = nullptr;
    juce::AudioParameterFloat* decayParam = nullptr;
    juce::AudioParameterFloat* decayTrackParam = nullptr;
    juce::AudioParameterFloat* brightnessParam = nullptr;
    juce::AudioParameterFloat* pickPositionParam = nullptr;
    juce::AudioParameterFloat* pickHardnessParam = nullptr;
    juce::AudioParameterFloat* bodyParam = nullptr;
    juce::AudioParameterFloat* stiffnessParam = nullptr;
    juce::AudioParameterFloat* releaseParam = nullptr;
    juce::AudioParameterFloat* velBrightParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;

    static constexpr int kNumBodyBands = 3;

    double sampleRate_ = 44100.0;
    std::uint32_t noteCounter_ = 0;
    int bodyTail_ = 0; // samples left before the body resonators are considered silent
    synth::drums::NoiseSource noise_;
    std::array<synth::pluck::PluckVoice, synth::pluck::kNumVoices> voices_;
    std::array<BodyBand, kNumBodyBands> body_;
    juce::SmoothedValue<float> smoothedLevel_, smoothedBody_;
};
