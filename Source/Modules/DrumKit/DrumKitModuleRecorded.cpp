// DrumKitModuleRecorded.cpp -- the Drum Kit's recorded kit: loading and saving it, which notes play it, and the
// recorded voices' trigger, choke and mix. The synth drums and the module's parameters are in DrumKitModule.cpp.
#include "DrumKitModule.h"
#include "Modules/ModuleFileStateKeys.h"
#include "Modules/Sampler/SamplerInstrumentPlayer.h"

namespace {

using synth::sampler::VoiceParams;

constexpr std::array<const char*, DrumKitModule::kNumGroups> kRecordedIds = {
    "kickRecorded", "snareRecorded", "clapRecorded", "hatRecorded", "tomRecorded", "cymbalRecorded", "cowbellRecorded"};
constexpr std::array<const char*, DrumKitModule::kNumGroups> kRecordedNames = {
    "Kick recorded", "Snare recorded",  "Clap recorded",   "Hat recorded",
    "Tom recorded",  "Cymbal recorded", "Cowbell recorded"};
constexpr std::array<const char*, DrumKitModule::kNumGroups> kLevelIds = {
    "kickLevel", "snareLevel", "clapLevel", "hatLevel", "tomLevel", "cymbalLevel", "cowbellLevel"};
constexpr std::array<const char*, DrumKitModule::kNumGroups> kTuneIds = {
    "kickTune", "snareTune", "clapTune", "hatTune", "tomTune", "cymbalTune", "cowbellTune"};

constexpr float kChokeSeconds = 0.012f;
constexpr float kPedalHatGain = 0.8f;
// How much velocity shapes a recording's level on top of the layer it picks (the Sampler's Feel, fixed).
constexpr float kVelocityFeel = 0.5f;
constexpr int kOpenHatNote = 46;
constexpr int kMaxNote = 127;

// Fresh each call: a drum is one-shot, so only the envelope's attack matters and nothing is ever released.
VoiceParams recordedVoiceParams() {
    VoiceParams p;
    p.maxVoices = synth::sampler::VoicePool::kMaxVoices;
    return p;
}

// The notes a kit has a (key press) region for.
template <class Regions>
std::bitset<128> coverageOf(const Regions& regions) {
    std::bitset<128> notes;
    for (const auto& region : regions) {
        if (region.releaseTrigger)
            continue;
        for (int key = juce::jmax(0, region.loKey); key <= juce::jmin(kMaxNote, region.hiKey); ++key)
            notes.set((size_t)key);
    }
    return notes;
}

// The notes each Decay knob shapes (the rim shares the Snare's, a pedal hat the Closed decay).
std::vector<int> notesOfDecay(const juce::String& decayParamId) {
    if (decayParamId == "kickDecay")
        return {35, 36};
    if (decayParamId == "snareDecay")
        return {37, 38, 40};
    if (decayParamId == "clapDecay")
        return {39};
    if (decayParamId == "closedDecay")
        return {42, 44};
    if (decayParamId == "openDecay")
        return {46};
    if (decayParamId == "tomDecay")
        return {41, 43, 45, 47, 48, 50};
    if (decayParamId == "cymbalDecay")
        return {49, 51, 57, 59};
    if (decayParamId == "cowbellDecay")
        return {56};
    return {};
}

} // namespace

DrumKitModule::Group DrumKitModule::groupForNote(int note) noexcept {
    switch (drumForNote(note)) {
    case Drum::Kick:
        return Group::Kick;
    case Drum::Snare:
    case Drum::Rim:
        return Group::Snare;
    case Drum::Clap:
        return Group::Clap;
    case Drum::ClosedHat:
    case Drum::OpenHat:
        return Group::Hat;
    case Drum::Tom:
        return Group::Tom;
    case Drum::Crash:
    case Drum::Ride:
        return Group::Cymbal;
    case Drum::Cowbell:
        return Group::Cowbell;
    case Drum::None:
        break;
    }
    return Group::None;
}

// Appended after Level so saved projects and routings keep their order; a switch has no CV jack.
void DrumKitModule::addRecordedParameters() {
    for (size_t g = 0; g < (size_t)kNumGroups; ++g) {
        recorded_[g] = new juce::AudioParameterBool(kRecordedIds[g], kRecordedNames[g], true);
        addParameter(recorded_[g]);
        groupLevel_[g] = static_cast<juce::AudioParameterFloat*>(findParameterByID(this, kLevelIds[g]));
        groupTune_[g] = static_cast<juce::AudioParameterFloat*>(findParameterByID(this, kTuneIds[g]));
    }
}

// ---- Loading and saving ----

bool DrumKitModule::loadInstrument(const synth::sampler::InstrumentSource& source) {
    return instrumentSlot.load(source);
}

bool DrumKitModule::loadInstrumentFromSfz(const juce::File& sfz) {
    return loadInstrument(synth::sampler::InstrumentSource::fromSfz(sfz));
}

bool DrumKitModule::loadInstrumentFromPack(const juce::String& packId, const juce::String& instrumentId) {
    return loadInstrument(synth::sampler::InstrumentSource::fromPack(packId, instrumentId));
}

bool DrumKitModule::loadInstrumentFromFiles(const juce::StringArray& absolutePaths) {
    return !absolutePaths.isEmpty() && loadInstrument(synth::sampler::InstrumentSource::fromFiles(absolutePaths));
}

// A source still decoding is what the module holds, so a save or an undo snapshot taken before it lands keeps it.
juce::var DrumKitModule::getExtraState() const {
    const auto& source = instrumentSlot.sourceToSave();
    if (source.kind == synth::sampler::InstrumentSource::Kind::None)
        return {};
    juce::DynamicObject::Ptr state = new juce::DynamicObject();
    synth::sampler::writeInstrumentState(source, *state);
    return juce::var(state.get());
}

void DrumKitModule::setExtraState(const juce::var& state) {
    const auto* obj = state.getDynamicObject();
    const auto source = obj != nullptr ? synth::sampler::readInstrumentState(*obj) : synth::sampler::InstrumentSource{};
    if (source.kind == synth::sampler::InstrumentSource::Kind::None)
        instrumentSlot.clear(); // a state saved with no kit: undo of the first load
    else
        instrumentSlot.restore(source);
}

// ---- Which notes play recordings ----

bool DrumKitModule::playsRecording(int note) const {
    const auto group = groupForNote(note);
    if (group == Group::None || !recorded_[(size_t)group]->get())
        return false;
    return coverageOf(instrumentSlot.regions()).test((size_t)juce::jlimit(0, kMaxNote, note));
}

bool DrumKitModule::decayIsMoot(const juce::String& decayParamId) const {
    const auto notes = notesOfDecay(decayParamId);
    if (notes.empty())
        return false;
    for (int note : notes)
        if (!playsRecording(note))
            return false;
    return true;
}

// ---- Audio thread ----

// Once per block: takes up a newly published kit (the voice pool stops when its instrument changes).
void DrumKitModule::acquireKit() noexcept {
    if (!instrumentPlayer.tryAcquire(heldKit_) || heldKit_.get() == poolKit_)
        return;
    poolKit_ = heldKit_.get();
    recordedPool_.setInstrument(poolKit_);
    covered_ = poolKit_ != nullptr ? coverageOf(poolKit_->regions) : std::bitset<128>();
}

// Lets go of the kit so a replaced one can be freed (bypass and mute are silent anyway).
void DrumKitModule::releaseKit() noexcept {
    if (heldKit_ == nullptr)
        return;
    recordedPool_.setInstrument(nullptr);
    heldKit_ = nullptr;
    poolKit_ = nullptr;
    covered_.reset();
    recordedVoices_.store(0, std::memory_order_relaxed);
}

bool DrumKitModule::recordedNow(int note) const noexcept {
    const auto group = groupForNote(note);
    return poolKit_ != nullptr && group != Group::None && recorded_[(size_t)group]->get() &&
           covered_.test((size_t)juce::jlimit(0, kMaxNote, note));
}

// A closed or pedal hat shuts the open hat, whichever kind of voice the open hat is.
void DrumKitModule::chokeOpenHat() noexcept {
    openHat_.choke(sampleRate_);
    recordedPool_.chokeNote(kOpenHatNote, kChokeSeconds);
}

void DrumKitModule::triggerRecorded(int note, float velocity) {
    const auto group = (size_t)groupForNote(note);
    if (drumForNote(note) == Drum::ClosedHat)
        chokeOpenHat();
    const float vel = juce::jlimit(0.0f, 1.0f, velocity);
    float gain = groupLevel_[group]->get() * ((1.0f - kVelocityFeel) + kVelocityFeel * std::pow(vel, 1.6f));
    if (note == 44)
        gain *= kPedalHatGain;
    recordedPool_.noteOn(note, vel, recordedVoiceParams(), gain, (double)groupTune_[group]->get());
}

// Mixes `count` samples: the synth drums (mono, onto both sides) plus the recorded voices (stereo), then the module's
// Level and the soft limit. Recordings get no extra headroom: unlike the Sampler's chords only a few hits overlap, and
// a -9 dB trim made a recorded kit play about 14 dB below the synth kit at the same settings.
void DrumKitModule::renderChunk(float* left, float* right, int count) noexcept {
    std::array<float, kRenderChunk> recL{}, recR{};
    const bool kit = poolKit_ != nullptr;
    if (kit)
        recordedPool_.render(recL.data(), recR.data(), 0, count, recordedVoiceParams());
    for (int i = 0; i < count; ++i) {
        const float synthSum = synthSample();
        const float level = smoothedLevel_.getNextValue();
        if (!kit) {
            left[i] = limited(synthSum * level);
            if (right != nullptr)
                right[i] = left[i];
            continue;
        }
        left[i] = limited((synthSum + recL[(size_t)i]) * level);
        if (right != nullptr)
            right[i] = limited((synthSum + recR[(size_t)i]) * level);
    }
    recordedVoices_.store(recordedPool_.activeVoiceCount(), std::memory_order_relaxed);
}
