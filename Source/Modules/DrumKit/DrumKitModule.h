#pragma once

#include "DrumVoices.h"
#include "Modules/ModuleBase.h"
#include "Modules/Sampler/SamplerInstrumentHost.h"
#include "Modules/Sampler/SamplerInstrumentSlot.h"
#include "Modules/Sampler/SamplerVoicePool.h"
#include "Modules/VoiceCountSource.h"
#include <array>
#include <atomic>
#include <bitset>

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
 * A recorded kit (a sound pack drum instrument, an SFZ or the user's files, loaded like the Sampler's Instrument)
 * replaces the synth voice of every note it has a region for, group by group (each group's Recorded switch). Group
 * Level and Tune apply to both kinds of hit; Decay only to the synth voices.
 *
 * Hit settings (level, tune, decay) are read when a drum is struck; only the master Level is live.
 */
class DrumKitModule
    : public ModuleBase
    , public synth::VoiceCountSource
    , public synth::sampler::InstrumentHost {
public:
    static constexpr int kNumChannels = 2;

    /** The note-to-drum map, exposed for tests and docs. */
    enum class Drum { Kick, Snare, Rim, Clap, ClosedHat, OpenHat, Tom, Crash, Ride, Cowbell, None };
    static Drum drumForNote(int note) noexcept;

    /** The parameter group a note belongs to (the rim is in Snare). */
    enum class Group { Kick, Snare, Clap, Hat, Tom, Cymbal, Cowbell, None };
    static constexpr int kNumGroups = 7;
    static Group groupForNote(int note) noexcept;

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

    // ---- Recorded kit (message thread unless noted) ----
    bool loadInstrumentFromSfz(const juce::File& sfz) override;
    bool loadInstrumentFromPack(const juce::String& packId, const juce::String& instrumentId) override;
    bool loadInstrumentFromFiles(const juce::StringArray& absolutePaths) override;
    void clearInstrument() override { instrumentSlot.clear(); }
    juce::String getInstrumentName() const override { return instrumentPlayer.name(); }
    int getInstrumentGeneration() const noexcept override { return instrumentPlayer.generation(); }
    int getInstrumentLoadSerial() const noexcept override { return instrumentSlot.loadSerial(); }
    juce::StringArray getInstrumentMessages() const override { return instrumentSlot.messages(); }
    bool isInstrumentLoadPending() const noexcept override { return instrumentSlot.isPending(); }
    synth::sampler::Instrument::Ptr getInstrument() const { return instrumentPlayer.current(); }
    synth::sampler::InstrumentSource::Kind getInstrumentKind() const { return instrumentSlot.loadedKind(); }
    bool hasRecordedKit() const { return getInstrumentKind() != synth::sampler::InstrumentSource::Kind::None; }
    /** True when `note` plays a recording now: a kit with a region for it and its group's Recorded switch on. */
    bool playsRecording(int note) const;
    /** True when every note that `decayParamId` (e.g. "kickDecay") shapes plays a recording, so the Decay is moot. */
    bool decayIsMoot(const juce::String& decayParamId) const;
    /** Any thread. The recorded voices sounding at the end of the last block. */
    int displayVoiceCount() const noexcept override { return recordedVoices_.load(std::memory_order_relaxed); }

    juce::var getExtraState() const override;
    void setExtraState(const juce::var& state) override;

private:
    using Hit = synth::drums::Hit;

    juce::AudioParameterFloat* addFloat(const char* id, const char* name, float lo, float hi, float def);
    void trigger(int note, float velocity);
    Hit hitFor(juce::AudioParameterFloat* level, juce::AudioParameterFloat* tune, float decaySeconds,
               float velocity) const;
    void addRecordedParameters();
    void stopAll() noexcept;
    float synthSample() noexcept;
    float limited(float sum) noexcept;
    bool loadInstrument(const synth::sampler::InstrumentSource& source);
    void acquireKit() noexcept;
    void releaseKit() noexcept;
    bool recordedNow(int note) const noexcept;
    void chokeOpenHat() noexcept;
    void triggerRecorded(int note, float velocity);
    void renderChunk(float* left, float* right, int count) noexcept;

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
    std::array<juce::AudioParameterBool*, kNumGroups> recorded_{};
    std::array<juce::AudioParameterFloat*, kNumGroups> groupLevel_{};
    std::array<juce::AudioParameterFloat*, kNumGroups> groupTune_{};

    static constexpr int kNumToms = 6;
    static constexpr int kRenderChunk = 256;

    double sampleRate_ = 44100.0;
    synth::drums::NoiseSource noise_;
    synth::drums::KickVoice kick_;
    synth::drums::SnareVoice snare_, rim_;
    synth::drums::ClapVoice clap_;
    synth::drums::CowbellVoice cowbell_;
    synth::drums::MetallicVoice closedHat_, openHat_, crash_, ride_;
    std::array<synth::drums::TomVoice, kNumToms> toms_;
    juce::SmoothedValue<float> smoothedLevel_;

    synth::sampler::InstrumentPlayer instrumentPlayer; // before the slot, which refers to it; only hands the kit over
    synth::sampler::InstrumentSlot instrumentSlot{instrumentPlayer, this};
    synth::sampler::VoicePool recordedPool_;
    synth::sampler::Instrument::Ptr heldKit_; // audio thread
    const synth::sampler::Instrument* poolKit_ = nullptr;
    std::bitset<128> covered_; // audio thread: the notes the held kit has a region for
    std::atomic<int> recordedVoices_{0};
};
