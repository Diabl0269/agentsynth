// DrumKitModulePads.cpp -- the Drum Kit's pads on the message thread: the per-pad parameters, the group parameters
// that write their pads, pad edits, audio-file decoding, the snapshot handed to the audio thread, and saving.
#include "DrumKitModule.h"
#include "Modules/ParameterText.h"
#include "Modules/Sampler/SamplerInstrumentPlayer.h"
#include "Project/DeferredAssetLoads.h"
#include <algorithm>

namespace {

using synth::drums::kMaxPads;
using synth::drums::kNoSound;
using synth::drums::PadKind;

constexpr std::array<const char*, 4> kPadParamSuffixes = {"Level", "Tune", "Decay", "Start"};
constexpr std::array<const char*, 4> kPadParamWords = {"level", "tune", "decay", "start"};
constexpr double kMaxFileSeconds = 20.0;

// Pads that never had a group sound (21-32) start here.
constexpr std::array<float, 4> kFreePadDefaults = {0.8f, 0.0f, 0.3f, 0.0f};

bool onMessageThread() {
    const auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    return mm == nullptr || mm->isThisTheMessageThread();
}

} // namespace

// Shared ranges for every pad: Level and Tune as the groups have them; Decay spans every group's range (0.02 s for
// a tight hat up to 4 s for a cymbal), skewed so the short end gets most of the knob; Start is a fraction of the file.
static juce::NormalisableRange<float> padRangeFor(DrumKitModule::PadParam which) {
    switch (which) {
    case DrumKitModule::PadParam::Level:
    case DrumKitModule::PadParam::Start:
        return {0.0f, 1.0f};
    case DrumKitModule::PadParam::Tune:
        return {-12.0f, 12.0f};
    case DrumKitModule::PadParam::Decay: {
        juce::NormalisableRange<float> range(0.02f, 4.0f);
        range.setSkewForCentre(0.4f);
        return range;
    }
    }
    return {0.0f, 1.0f};
}

bool DrumKitModule::isPadParamId(const juce::String& id) {
    if (!id.startsWith("pad"))
        return false;
    const auto rest = id.substring(3);
    const auto digits = rest.initialSectionContainingOnly("0123456789");
    if (digits.isEmpty() || digits.getIntValue() < 1 || digits.getIntValue() > kMaxPads)
        return false;
    const auto suffix = rest.substring(digits.length());
    return std::any_of(kPadParamSuffixes.begin(), kPadParamSuffixes.end(), [&](const char* s) { return suffix == s; });
}

void DrumKitModule::addGroupLink(juce::AudioParameterFloat* param, PadParam field, Group group, int hats) {
    auto& link = links_[(size_t)numLinks_++];
    link.param = param;
    link.field = field;
    link.group = group;
    link.hats = hats;
    link.lastSeen = param->get();
    link.audioSeen.store(param->get());
    param->addListener(this);
}

bool DrumKitModule::linkReaches(const GroupLink& link, int sound) const noexcept {
    if (sound == kNoSound || groupForNote(sound) != link.group)
        return false;
    if (link.hats == 1)
        return synth::drums::chokesOpenHat(sound);
    if (link.hats == 2)
        return synth::drums::isOpenHat(sound);
    return true;
}

// 128 parameters after every older one; each pad starts with its group's defaults so a new kit sounds as it did.
namespace {

// Each pad knob reads in its own unit: Level and Start as a percent, Tune in semitones, Decay (held in seconds) as a
// time.
juce::AudioParameterFloatAttributes padAttributes(DrumKitModule::PadParam which) {
    using PadParam = DrumKitModule::PadParam;
    if (which == PadParam::Tune)
        return synth::semitonesAttributes();
    if (which == PadParam::Decay)
        return juce::AudioParameterFloatAttributes()
            .withLabel("s")
            .withStringFromValueFunction([](float v, int) { return synth::millisecondsToText(v * 1000.0f); })
            .withValueFromStringFunction(
                [](const juce::String& t) { return synth::millisecondsFromText(t) / 1000.0f; });
    return synth::fractionPercentAttributes();
}

} // namespace

void DrumKitModule::addPadParameters() {
    for (int i = 0; i < kMaxPads; ++i) {
        const int sound = synth::drums::defaultPad(i).sound;
        std::array<juce::AudioParameterFloat*, 4> made{};
        for (size_t f = 0; f < 4; ++f) {
            const auto which = (PadParam)f;
            float def = kFreePadDefaults[f];
            for (int l = 0; l < numLinks_; ++l)
                if (links_[(size_t)l].field == which && linkReaches(links_[(size_t)l], sound))
                    def = links_[(size_t)l].param->get();
            const auto range = padRangeFor(which);
            made[f] = new juce::AudioParameterFloat(
                juce::ParameterID("pad" + juce::String(i + 1) + kPadParamSuffixes[f]),
                "Pad " + juce::String(i + 1) + " " + kPadParamWords[f], range, def, padAttributes(which));
            addParameter(made[f]);
        }
        padParams_[(size_t)i] = {made[0], made[1], made[2], made[3]};
    }
}

juce::AudioParameterFloat* DrumKitModule::padParameter(int index, PadParam which) const {
    const auto& p = padParams_[(size_t)juce::jlimit(0, kMaxPads - 1, index)];
    switch (which) {
    case PadParam::Level:
        return p.level;
    case PadParam::Tune:
        return p.tune;
    case PadParam::Decay:
        return p.decay;
    case PadParam::Start:
        return p.start;
    }
    return p.level;
}

// A group parameter set on the message thread writes its pads at once, so a restore that sets the group and then
// the saved pad values ends with the pad values. Only a real change writes: a group restated at the value it
// already had (an undo, a patch repeating it) leaves pads the user set apart alone. Off the message thread the
// audio thread's per-block poll (pollGroupLinks) does the write.
void DrumKitModule::parameterValueChanged(int parameterIndex, float newValue) {
    if (!onMessageThread())
        return;
    for (int l = 0; l < numLinks_; ++l) {
        auto& link = links_[(size_t)l];
        if (link.param->getParameterIndex() != parameterIndex)
            continue;
        const float v = link.param->get();
        const float audioPrev = link.audioSeen.exchange(v);
        if (v == link.lastSeen && v == audioPrev)
            return;
        link.lastSeen = v;
        writeLinkToPads(link, v, true);
        return;
    }
    for (size_t g = 0; g < recorded_.size(); ++g) {
        if (recorded_[g] == nullptr || recorded_[g]->getParameterIndex() != parameterIndex)
            continue;
        const bool on = newValue >= 0.5f;
        if (on != lastRecorded_[g]) {
            lastRecorded_[g] = on;
            applyRecordedSwitch((int)g, on);
        }
        return;
    }
}

void DrumKitModule::writeLinkToPads(GroupLink& link, float value, bool notify) {
    for (int i = 0; i < kMaxPads; ++i) {
        if (!linkReaches(link, pads_.pads[(size_t)i].sound))
            continue;
        auto* p = padParameter(i, link.field);
        const float normalised = p->convertTo0to1(value);
        if (notify)
            p->setValueNotifyingHost(normalised);
        else
            static_cast<juce::AudioProcessorParameter*>(p)->setValue(normalised);
    }
}

// On: the group's built-in pads play the kit; off: its kit pads play the built-in drum. File and instrument pads
// keep what they play.
void DrumKitModule::applyRecordedSwitch(int group, bool on) {
    bool changed = false;
    for (auto& p : pads_.pads) {
        if (p.sound == kNoSound || (int)groupForNote(p.sound) != group)
            continue;
        if (on && p.kind == PadKind::BuiltIn) {
            p.kind = PadKind::Kit;
            changed = true;
        } else if (!on && p.kind == PadKind::Kit) {
            p.kind = PadKind::BuiltIn;
            changed = true;
        }
    }
    if (changed)
        publishPads();
}

synth::drums::PadTable DrumKitModule::defaultTableForSwitches() const {
    auto table = synth::drums::defaultPadTable();
    for (auto& p : table.pads) {
        const auto group = p.sound != kNoSound ? groupForNote(p.sound) : Group::None;
        if (group != Group::None && !recorded_[(size_t)group]->get())
            p.kind = PadKind::BuiltIn;
    }
    return table;
}

// A free pad shown for the first time gets a built-in sound and its group's current values, so it plays at once.
void DrumKitModule::revealPad(int index) {
    auto& p = pads_.pads[(size_t)index];
    const int sound = synth::drums::revealSoundFor(index);
    if (p.sound != kNoSound || p.kind != PadKind::BuiltIn || sound == kNoSound)
        return;
    p.sound = sound;
    const auto group = groupForNote(sound);
    if (group != Group::None && recorded_[(size_t)group]->get() && hasRecordedKit())
        p.kind = PadKind::Kit;
    for (int l = 0; l < numLinks_; ++l)
        if (linkReaches(links_[(size_t)l], sound)) {
            auto* param = padParameter(index, links_[(size_t)l].field);
            param->setValueNotifyingHost(param->convertTo0to1(links_[(size_t)l].param->get()));
        }
}

void DrumKitModule::setPadCount(int count) {
    count = synth::drums::clampPadCount(count);
    for (int i = pads_.count; i < count; ++i)
        revealPad(i);
    pads_.count = count;
    publishPads();
}

bool DrumKitModule::setPad(int index, const synth::drums::Pad& pad) {
    if (index < 0 || index >= kMaxPads)
        return false;
    synth::sampler::SampleData::Ptr sample;
    if (pad.kind == PadKind::File) {
        const auto& loaded = padSamples_[(size_t)index];
        sample = loaded != nullptr && loaded->filePath == pad.file
                     ? loaded
                     : synth::sampler::decodeSampleFile(juce::File(pad.file), kMaxFileSeconds);
        if (sample == nullptr)
            return false;
    }
    pads_.pads[(size_t)index] = pad;
    padSamples_[(size_t)index] = sample;
    ++padLoadSerial_[(size_t)index];
    publishPads();
    return true;
}

bool DrumKitModule::setPadFile(int index, const juce::File& file) {
    if (index < 0 || index >= kMaxPads || !juce::File::isAbsolutePath(file.getFullPathName()))
        return false;
    auto p = pads_.pads[(size_t)index];
    p.kind = PadKind::File;
    p.file = file.getFullPathName();
    p.name = file.getFileNameWithoutExtension().substring(0, 64);
    return setPad(index, p);
}

// The kit is a user's pick: every pad, whatever it played, now plays the kit, and every group's Recorded switch is
// on. A load that is not a pick (a restore, a sound swap) leaves the pads as they were saved.
void DrumKitModule::fillPadsWithKit() {
    for (size_t i = 0; i < pads_.pads.size(); ++i) {
        pads_.pads[i].kind = PadKind::Kit;
        padSamples_[i] = nullptr;
        ++padLoadSerial_[i];
    }
    for (auto* r : recorded_)
        if (!r->get())
            r->setValueNotifyingHost(1.0f);
    publishPads();
}

void DrumKitModule::auditionPad(int index) noexcept {
    if (index >= 0 && index < kMaxPads)
        auditionMask_.fetch_or(1u << (unsigned)index, std::memory_order_acq_rel);
}

// File pads decode now, or on the worker while a project opens on screen (installed here later unless the pad was
// edited since or the module is gone).
void DrumKitModule::loadPadSamples(bool deferIfOpening) {
    auto* loads = deferIfOpening ? synth::DeferredAssetLoads::active() : nullptr;
    for (int i = 0; i < kMaxPads; ++i) {
        const auto& p = pads_.pads[(size_t)i];
        auto& sample = padSamples_[(size_t)i];
        if (p.kind != PadKind::File || p.file.isEmpty()) {
            sample = nullptr;
            continue;
        }
        if (sample != nullptr && sample->filePath == p.file)
            continue;
        sample = nullptr;
        const juce::File file(p.file);
        if (loads == nullptr) {
            sample = synth::sampler::decodeSampleFile(file, kMaxFileSeconds);
            continue;
        }
        const int serial = padLoadSerial_[(size_t)i];
        juce::WeakReference<DrumKitModule> self(this);
        loads->enqueue(synth::DeferredAssetLoads::Kind::Sample, this, [self, i, serial, file] {
            auto decoded = synth::sampler::decodeSampleFile(file, kMaxFileSeconds);
            return synth::DeferredAssetLoads::Install([self, i, serial, decoded] {
                auto* module = self.get();
                if (module == nullptr || module->padLoadSerial_[(size_t)i] != serial)
                    return;
                module->padSamples_[(size_t)i] = decoded;
                module->publishPads();
            });
        });
    }
}

// Replaced snapshots and samples stay in the retain lists until the audio thread has let go of them (reference
// count 1 = only the list), so the audio thread never frees either.
void DrumKitModule::publishPads() {
    synth::drums::PadSnapshot::Ptr snap = new synth::drums::PadSnapshot();
    for (int i = 0; i < kMaxPads; ++i) {
        const auto& p = pads_.pads[(size_t)i];
        auto& e = snap->pads[(size_t)i];
        e.kind = p.kind;
        e.sound = p.sound;
        e.note = p.note;
        e.sample = p.kind == PadKind::File ? padSamples_[(size_t)i] : nullptr;
        if (e.sample != nullptr && !retainedSamples_.contains(e.sample.get()))
            retainedSamples_.add(e.sample);
        snap->tickOrder[(size_t)i] = (std::int8_t)i;
    }
    std::stable_sort(snap->tickOrder.begin(), snap->tickOrder.end(), [&](std::int8_t a, std::int8_t b) {
        return synth::drums::tickRankForSound(snap->pads[(size_t)a].sound) <
               synth::drums::tickRankForSound(snap->pads[(size_t)b].sound);
    });
    retainedSnapshots_.add(snap);
    {
        const juce::SpinLock::ScopedLockType lock(padLock_);
        publishedPads_ = snap;
    }
    for (int i = retainedSnapshots_.size(); --i >= 0;)
        if (retainedSnapshots_[i]->getReferenceCount() == 1)
            retainedSnapshots_.remove(i);
    for (int i = retainedSamples_.size(); --i >= 0;)
        if (retainedSamples_[i]->getReferenceCount() == 1)
            retainedSamples_.remove(i);
}

// ---- Saving ----

// A source still decoding is what the module holds, so a save or an undo snapshot taken before it lands keeps it.
// The pads are saved only when they differ from a new kit's (with the Recorded switches applied), so an untouched
// Drum Kit adds no state and an older project reads back unchanged.
juce::var DrumKitModule::getExtraState() const {
    juce::DynamicObject::Ptr state = new juce::DynamicObject();
    const auto& source = instrumentSlot.sourceToSave();
    if (source.kind != synth::sampler::InstrumentSource::Kind::None)
        synth::sampler::writeInstrumentState(source, *state);
    const auto plain = defaultTableForSwitches();
    if (pads_.count != plain.count || pads_.pads != plain.pads)
        synth::drums::writePadState(pads_, *state);
    if (state->getProperties().isEmpty())
        return {};
    return juce::var(state.get());
}

// A state with no pad table (a project from before pads, or a kit saved with untouched pads) opens as a new kit's
// pads with the Recorded switches the parameters already restored.
void DrumKitModule::setExtraState(const juce::var& state) {
    const auto* obj = state.getDynamicObject();
    const auto source = obj != nullptr ? synth::sampler::readInstrumentState(*obj) : synth::sampler::InstrumentSource{};
    if (source.kind == synth::sampler::InstrumentSource::Kind::None)
        instrumentSlot.clear(); // a state saved with no kit: undo of the first load
    else
        instrumentSlot.restore(source);

    synth::drums::PadTable table;
    if (obj == nullptr || !synth::drums::readPadState(*obj, table))
        table = defaultTableForSwitches();
    pads_ = table;
    for (auto& serial : padLoadSerial_)
        ++serial;
    loadPadSamples(true);
    publishPads();
}
