#pragma once

#include <optional>

#include "MalletVoices.h"
#include "Modules/ModTargetList.h"
#include "Modules/ModuleBase.h"
#include "Modules/VoiceCountSource.h"
#include <array>
#include <atomic>

/**
 * Struck-bar instrument: modal synthesis with eight voices (marimba, vibraphone, xylophone, glockenspiel,
 * bell). Each note-on strikes a bar at the note's pitch (12-TET, A4 = 440 Hz, plus Tune and Fine); velocity
 * sets the loudness and, by Velocity brightness, how hard the mallet hits. Note-Off damps the bar over the
 * Release time. When all eight bars are busy the oldest is taken over.
 *
 * Everything but Level and Tremolo is read when a note starts (Release when it ends). Every knob takes a cable
 * (kModTargets), read at the same moment as the knob. Tremolo is a
 * free-running 5.5 Hz amplitude wobble on the whole output, like a vibraphone's motor. No ADSR or VCA is
 * needed in front of it. Output is mono-summed onto a stereo pair (Declared, ships SPLIT like the Drum Kit).
 * The partial tables and formulas are in docs/modules/pluck-mallet.md.
 */
class MalletModule
    : public ModuleBase
    , public synth::VoiceCountSource {
public:
    static constexpr int kNumChannels = 2;
    /** Every knob's cable, channel = index. Append only: saved cables keep their channel. */
    static constexpr std::array<synth::ModTargetSpec, 11> kModTargets{{
        {"level", synth::ModRead::Live},
        {"tremolo", synth::ModRead::Live},
        {"tune", synth::ModRead::NoteOn},
        {"fine", synth::ModRead::NoteOn},
        {"decay", synth::ModRead::NoteOn},
        {"damping", synth::ModRead::NoteOn},
        {"brightness", synth::ModRead::NoteOn},
        {"hardness", synth::ModRead::NoteOn},
        {"resonator", synth::ModRead::NoteOn},
        {"release", synth::ModRead::NoteOff},
        {"velBright", synth::ModRead::NoteOn},
    }};
    static constexpr int kLevelCvChannel = 0;   // the Level knob's cable; no gutter jack
    static constexpr int kTremoloCvChannel = 1; // the Tremolo knob's cable; no gutter jack
    static constexpr float kTremoloHz = 5.5f;

    MalletModule();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::String getOutputPortLabel(int i) const override {
        return isDualIO() ? (i == 1 ? "Audio R" : "Audio L") : "Audio";
    }
    int getVisibleInputPortCount() const override { return 0; }
    juce::String getInputPortLabel(int i) const override { return mods_.label(i); }
    LogicalPort mapInputChannel(int raw) const override { return mods_.portOf(raw); }
    std::vector<ModulationTarget> getModulationTargets() const override { return mods_.targets(); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Oscillator; }
    ModuleType getModuleType() const override { return ModuleType::Mallet; }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    /** Bars still sounding (held or ringing out); exposed for tests. */
    int activeVoiceCount() const noexcept;

    /** Strings/bars whose key is still down: what the status bar shows (a tail fading out is not counted). */
    int heldVoiceCount() const noexcept;

    int displayVoiceCount() const noexcept override { return publishedVoices_.load(std::memory_order_relaxed); }

private:
    std::atomic<int> publishedVoices_{0}; // heldVoiceCount() as of the end of the last block
    juce::AudioParameterFloat* addFloat(const char* id, const char* name, float lo, float hi, float def,
                                        float skew = 1.0f,
                                        std::optional<juce::AudioParameterFloatAttributes> attributes = std::nullopt);
    void noteOn(int note, float velocity, const juce::AudioBuffer<float>& cv, int sample);
    void noteOff(int note, const juce::AudioBuffer<float>& cv, int sample);
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
    synth::ModTargetList mods_{kModTargets};

    double sampleRate_ = 44100.0;
    double tremoloPhase_ = 0.0;
    std::uint32_t noteCounter_ = 0;
    synth::drums::NoiseSource noise_;
    std::array<synth::mallet::MalletVoice, synth::mallet::kNumVoices> voices_;
    juce::SmoothedValue<float> smoothedLevel_, smoothedTremolo_;
};
