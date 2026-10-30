// DrumKitModuleRecorded.cpp -- the Drum Kit's recorded kit: loading it, its Recorded switches and which pads play it.
// Saving it is in DrumKitModulePads.cpp, with the pads; its voices are triggered in DrumKitModulePadAudio.cpp.
#include "DrumKitModule.h"
#include "Modules/ModuleFileStateKeys.h"
#include "Modules/Sampler/SamplerInstrumentPlayer.h"

namespace {

constexpr std::array<const char*, DrumKitModule::kNumGroups> kRecordedIds = {
    "kickRecorded", "snareRecorded", "clapRecorded", "hatRecorded", "tomRecorded", "cymbalRecorded", "cowbellRecorded"};
constexpr std::array<const char*, DrumKitModule::kNumGroups> kRecordedNames = {
    "Kick recorded", "Snare recorded",  "Clap recorded",   "Hat recorded",
    "Tom recorded",  "Cymbal recorded", "Cowbell recorded"};

constexpr int kMaxNote = 127;

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
        lastRecorded_[g] = true;
        recorded_[g]->addListener(this);
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

// ---- Which notes play recordings ----

bool DrumKitModule::playsRecording(int note) const {
    for (int i = 0; i < synth::drums::kMaxPads; ++i)
        if (pads_.pads[(size_t)i].note == note && padPlaysRecording(i))
            return true;
    return false;
}

// A kit pad plays the kit's region for its sound (for its own note when it has no built-in sound).
bool DrumKitModule::padPlaysRecording(int index) const {
    const auto& p = pad(index);
    if (p.kind != synth::drums::PadKind::Kit)
        return false;
    const int kitNote = p.sound != synth::drums::kNoSound ? p.sound : p.note;
    return coverageOf(instrumentSlot.regions()).test((size_t)juce::jlimit(0, kMaxNote, kitNote));
}

bool DrumKitModule::kitHasSound(int sound) const {
    return sound >= 0 && sound <= kMaxNote && hasRecordedKit() &&
           coverageOf(instrumentSlot.regions()).test((size_t)sound);
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
