#pragma once

#include "DrumKitPadVoice.h"
#include "DrumKitPads.h"
#include "DrumVoices.h"
#include "Modules/ModuleBase.h"
#include "Modules/Sampler/SamplerInstrumentHost.h"
#include "Modules/Sampler/SamplerInstrumentSlot.h"
#include "Modules/Sampler/SamplerVoicePool.h"
#include "Modules/VoiceCountSource.h"
#include <array>
#include <atomic>
#include <bitset>
#include <functional>
#include <set>
#include <vector>

/**
 * Synthesized electronic drum kit on one MIDI track, played through 32 pad slots.
 *
 * Each pad has a MIDI note, a sound (a built-in drum, the loaded recorded kit, an audio file, or another
 * instrument) and its own Level, Tune, Decay and Start parameters ("pad1Level" ... "pad32Start"). A new kit's pads
 * 1-20 play the General MIDI drums on their usual notes; 21-32 are free notes with no sound. The pad count (4..32)
 * only decides how many pads the card shows: every pad answers its note.
 *
 * The group parameters (kickLevel, snareTune, closedDecay ... and the *Recorded switches) stay: the AI and older
 * projects write them. Setting one writes its value to every pad of that group (closedDecay to the closed and pedal
 * hats, openDecay to the open hat); a Recorded switch moves its group's pads between the kit and the built-in drum.
 *
 * A drum plays out its own decay whatever the note length, so Note-Off does nothing to it. An instrument pad sends its
 * pitch to another module's note inbox and its Note-Off when the hit ends. A closed or pedal hat pad chokes every open
 * hat pad. Output is a stereo pair (Declared, ships SPLIT like the Sampler).
 */
class DrumKitModule
    : public ModuleBase
    , public synth::VoiceCountSource
    , public synth::sampler::InstrumentHost
    , private juce::AudioProcessorParameter::Listener {
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
    ~DrumKitModule() override;

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
    /** True when a pad on `note` plays a recording now. */
    bool playsRecording(int note) const;
    /** True when every note that `decayParamId` (e.g. "kickDecay") shapes plays a recording, so the Decay is moot. */
    bool decayIsMoot(const juce::String& decayParamId) const;
    /** Any thread. The recorded and audio-file voices sounding at the end of the last block. */
    int displayVoiceCount() const noexcept override { return recordedVoices_.load(std::memory_order_relaxed); }

    juce::var getExtraState() const override;
    void setExtraState(const juce::var& state) override;

    // ---- Pads (message thread unless noted) ----
    enum class PadParam { Level, Tune, Decay, Start };
    /** True for "pad<N>Level/Tune/Decay/Start". */
    static bool isPadParamId(const juce::String& id);
    const synth::drums::PadTable& padTable() const noexcept { return pads_; }
    const synth::drums::Pad& pad(int index) const { return pads_.pads[(size_t)juce::jlimit(0, 31, index)]; }
    int padCount() const noexcept { return pads_.count; }
    /** Clamped by clampPadCount. A never-used free pad the count shows gets its built-in sound. */
    void setPadCount(int count);
    /** Whether pad `index` is on the card, in the clip editors' rows. A hidden pad still plays its hits. */
    bool padShown(int index) const { return pad(index).shown; }
    /** The pads within the count the card draws: shown ones, in pad order. */
    std::vector<int> shownPads() const;
    /** How many pads within the count are switched off. */
    int hiddenPadCount() const;
    /** Sets pad `index`'s Shown switch; false when it already read so. Never touches the sound. */
    bool setPadShown(int index, bool shown);
    /** Switches every pad's Shown on (also beyond the count); how many changed. */
    int showAllPads();
    /** Replaces pad `index`; a File pad's file is decoded now (false, leaving the pad, when it cannot be read). */
    bool setPad(int index, const synth::drums::Pad& pad);
    /** Makes pad `index` play `file` (decoded now); false when it cannot be read. */
    bool setPadFile(int index, const juce::File& file);
    /** Every pad plays the loaded kit (its region for the pad's sound, the built-in drum without one). */
    void fillPadsWithKit();
    juce::AudioParameterFloat* padParameter(int index, PadParam which) const;
    /** True when pad `index` plays a recording now: a kit pad whose sound the loaded kit has a region for. */
    bool padPlaysRecording(int index) const;
    /** Plays pad `index` at the start of the next block. Any thread. */
    void auditionPad(int index) noexcept;
    /** Any thread. The pad the audio thread last struck, or -1. */
    int lastPlayedPad() const noexcept { return lastPlayedPad_.load(std::memory_order_relaxed); }
    /** Pad `index` plays its slot's kit sound again (name and kind too); its note and knobs stay. */
    void resetPad(int index);
    /** True when the loaded kit has a region for `sound`. */
    bool kitHasSound(int sound) const;

    // ---- Instrument pads (DrumKitModuleInstrumentPads.cpp) ----
    /** The modules an instrument pad may play: sounding MIDI instruments other than a Drum Kit or a sequencer. */
    static bool canPlayFromPad(ModuleType type) noexcept;
    /** Points each instrument pad at `inboxFor(uuid)` (null: the module is gone, the pad is silent). */
    void linkInstrumentPads(const std::function<synth::NoteInbox*(const juce::String& uuid)>& inboxFor);
    /** True when instrument pad `index` found its module at the last link. */
    bool padInstrumentLinked(int index) const;
    /** Links every Drum Kit in `graph`; returns the nodes some pad plays. Message thread. */
    static std::set<juce::AudioProcessorGraph::NodeID> linkAllInstrumentPads(juce::AudioProcessorGraph& graph);
    /** How long a pad played from the card holds an instrument's note. */
    static constexpr double kAuditionHoldSeconds = 0.25;

private:
    struct GroupLink {
        juce::AudioParameterFloat* param = nullptr;
        PadParam field = PadParam::Level;
        Group group = Group::None;
        int hats = 0;                       // 1: closed and pedal hats only, 2: open hat only
        float lastSeen = 0.0f;              // message thread
        std::atomic<float> audioSeen{0.0f}; // audio thread poll, also written by the listener
    };
    struct PadParams {
        juce::AudioParameterFloat* level = nullptr;
        juce::AudioParameterFloat* tune = nullptr;
        juce::AudioParameterFloat* decay = nullptr;
        juce::AudioParameterFloat* start = nullptr;
    };

    juce::AudioParameterFloat* addFloat(const char* id, const char* name, float lo, float hi, float def);
    void addGroupLink(juce::AudioParameterFloat* param, PadParam field, Group group, int hats = 0);
    void addRecordedParameters();
    void addPadParameters();
    void stopAll() noexcept;
    float synthSample() noexcept;
    float limited(float sum) noexcept;
    bool loadInstrument(const synth::sampler::InstrumentSource& source);
    void acquireKit() noexcept;
    void releaseKit() noexcept;

    // Pads, message thread (DrumKitModulePads.cpp)
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}
    bool linkReaches(const GroupLink& link, int sound) const noexcept;
    void writeLinkToPads(GroupLink& link, float value, bool notify);
    void applyRecordedSwitch(int group, bool on);
    synth::drums::PadTable defaultTableForSwitches() const;
    void revealPad(int index);
    void loadPadSamples(bool deferIfOpening);
    void publishPads();

    // Pads, audio thread (DrumKitModulePadAudio.cpp)
    void acquirePads() noexcept;
    void pollGroupLinks() noexcept;
    void triggerNote(int note, float velocity) noexcept;
    void triggerPad(int index, float velocity) noexcept;
    void triggerSynth(int index, int sound, float velocity) noexcept;
    void triggerRecordedPad(int index, int kitNote, int sound, float velocity) noexcept;
    void chokeOpenHats() noexcept;
    void renderSamples(float* left, float* right, int count) noexcept;
    bool samplesActive() const noexcept;
    void renderChunk(float* left, float* right, int count) noexcept;

    // Instrument pads, audio thread (DrumKitModuleInstrumentPads.cpp)
    void strikeInstrumentPad(int index, const synth::drums::PadSnapshot::Entry& e, float velocity) noexcept;
    void releaseInstrumentPad(int index, int offset) noexcept;
    void releaseInstrumentNote(int note, int offset) noexcept;
    void releaseAllInstrumentPads(int offset) noexcept;
    void releaseMovedInstrumentPads(const synth::drums::PadSnapshot& next) noexcept;
    void advanceAuditionReleases(int numSamples) noexcept;

    juce::AudioParameterFloat* levelParam = nullptr;
    std::array<juce::AudioParameterBool*, kNumGroups> recorded_{};
    std::array<bool, kNumGroups> lastRecorded_{}; // message thread
    std::array<GroupLink, 22> links_;
    int numLinks_ = 0;
    std::array<PadParams, synth::drums::kMaxPads> padParams_{};

    static constexpr int kRenderChunk = 256;

    double sampleRate_ = 44100.0;
    synth::drums::NoiseSource noise_;
    std::array<synth::drums::PadVoice, synth::drums::kMaxPads> voices_;
    juce::SmoothedValue<float> smoothedLevel_;

    synth::sampler::InstrumentPlayer instrumentPlayer; // before the slot, which refers to it; only hands the kit over
    synth::sampler::InstrumentSlot instrumentSlot{instrumentPlayer, this};
    synth::sampler::VoicePool recordedPool_;
    synth::sampler::Instrument::Ptr heldKit_; // audio thread
    const synth::sampler::Instrument* poolKit_ = nullptr;
    std::bitset<128> covered_; // audio thread: the notes the held kit has a region for
    std::atomic<int> recordedVoices_{0};

    synth::drums::PadTable pads_;                                                    // message thread
    std::array<synth::sampler::SampleData::Ptr, synth::drums::kMaxPads> padSamples_; // decoded File pads
    std::array<int, synth::drums::kMaxPads> padLoadSerial_{};
    juce::ReferenceCountedArray<synth::drums::PadSnapshot> retainedSnapshots_; // freed once the audio thread let go
    juce::ReferenceCountedArray<synth::sampler::SampleData> retainedSamples_;
    mutable juce::SpinLock padLock_;
    synth::drums::PadSnapshot::Ptr publishedPads_; // under padLock_
    synth::drums::PadSnapshot::Ptr heldPads_;      // audio thread
    std::atomic<std::uint32_t> auditionMask_{0};
    std::atomic<int> lastPlayedPad_{-1};
    std::array<synth::NoteInbox::Ptr, synth::drums::kMaxPads> padInboxes_; // message thread
    std::array<juce::String, synth::drums::kMaxPads> padLinkUuid_;         // the uuid padInboxes_ was found for
    std::array<synth::NoteInbox*, synth::drums::kMaxPads> heldInbox_{};    // audio thread: a note is held there
    std::array<int, synth::drums::kMaxPads> heldPitch_{};
    std::array<int, synth::drums::kMaxPads> releaseIn_{}; // audio thread: samples to an auditioned note's end, 0 none
    int eventOffset_ = 0;                                 // audio thread: where in the block the hit lands
    bool auditioning_ = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE(DrumKitModule)
};
