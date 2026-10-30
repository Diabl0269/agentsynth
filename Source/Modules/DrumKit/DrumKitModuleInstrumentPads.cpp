// DrumKitModuleInstrumentPads.cpp -- pads that play another module. A strike posts the pad's pitch (plus Tune, in
// whole semitones) to that module's note inbox at the pad's Level times the hit's velocity; the Note-Off follows
// when the hit's note ends, or a quarter second later for a pad played from the card. No cable and no audio bus: the
// module sounds through its own outputs, so Decay and Start do nothing here.
#include "DrumKitModule.h"
#include <map>

using synth::drums::kMaxPads;
using synth::drums::PadKind;

// ---- Message thread: which module each pad reaches ----

bool DrumKitModule::canPlayFromPad(ModuleType type) noexcept {
    return isMidiInstrumentType(type) && type != ModuleType::DrumKit && type != ModuleType::Sequencer &&
           type != ModuleType::PolySequencer;
}

// A pad keeps the inbox it was linked to only while its uuid still names that module: an edit or an undo that
// changes the uuid drops the link at once (publishPads checks padLinkUuid_), and the next link finds the new one.
void DrumKitModule::linkInstrumentPads(const std::function<synth::NoteInbox*(const juce::String&)>& inboxFor) {
    bool changed = false;
    for (int i = 0; i < kMaxPads; ++i) {
        const auto& p = pads_.pads[(size_t)i];
        synth::NoteInbox* inbox = nullptr;
        if (p.kind == PadKind::Instrument && p.instrumentUuid.isNotEmpty())
            inbox = inboxFor(p.instrumentUuid);
        auto& linked = padInboxes_[(size_t)i];
        const auto uuid = inbox != nullptr ? p.instrumentUuid : juce::String();
        if (linked.get() == inbox && padLinkUuid_[(size_t)i] == uuid)
            continue;
        linked = inbox;
        padLinkUuid_[(size_t)i] = uuid;
        changed = true;
    }
    if (changed)
        publishPads();
}

bool DrumKitModule::padInstrumentLinked(int index) const {
    if (index < 0 || index >= kMaxPads)
        return false;
    const auto& p = pads_.pads[(size_t)index];
    return p.kind == PadKind::Instrument && padInboxes_[(size_t)index] != nullptr &&
           padLinkUuid_[(size_t)index] == p.instrumentUuid;
}

// One uuid map per call, then each kit links against it. A node whose uuid was never written cannot be a pad's
// target: the card writes the uuid when it links a pad.
std::set<juce::AudioProcessorGraph::NodeID> DrumKitModule::linkAllInstrumentPads(juce::AudioProcessorGraph& graph) {
    std::map<juce::String, juce::AudioProcessorGraph::Node*> byUuid;
    std::vector<DrumKitModule*> kits;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        if (auto* kit = dynamic_cast<DrumKitModule*>(node->getProcessor()))
            kits.push_back(kit);
        const auto uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty())
            byUuid.emplace(uuid, node);
    }
    std::set<juce::AudioProcessorGraph::NodeID> played;
    for (auto* kit : kits)
        kit->linkInstrumentPads([&](const juce::String& uuid) -> synth::NoteInbox* {
            const auto found = byUuid.find(uuid);
            auto* module = found != byUuid.end() ? dynamic_cast<ModuleBase*>(found->second->getProcessor()) : nullptr;
            if (module == nullptr || !canPlayFromPad(module->getModuleType()))
                return nullptr;
            played.insert(found->second->nodeID);
            return &module->noteInbox();
        });
    return played;
}

// ---- Audio thread ----

// A strike first ends the note this pad still holds, so a fast roll never leaves notes hanging.
void DrumKitModule::strikeInstrumentPad(int index, const synth::drums::PadSnapshot::Entry& e, float velocity) noexcept {
    releaseInstrumentPad(index, eventOffset_);
    auto* inbox = e.inbox.get();
    const auto& p = padParams_[(size_t)index];
    const float amount = p.level->get() * juce::jlimit(0.0f, 1.0f, velocity);
    if (inbox == nullptr || amount <= 0.0f)
        return;
    const int pitch = juce::jlimit(0, 127, e.pitch + juce::roundToInt(p.tune->get()));
    inbox->push(pitch, juce::jlimit(1, 127, juce::roundToInt(amount * 127.0f)), eventOffset_);
    heldInbox_[(size_t)index] = inbox;
    heldPitch_[(size_t)index] = pitch;
    releaseIn_[(size_t)index] = auditioning_ ? eventOffset_ + (int)(kAuditionHoldSeconds * sampleRate_) : 0;
}

void DrumKitModule::releaseInstrumentPad(int index, int offset) noexcept {
    auto*& inbox = heldInbox_[(size_t)index];
    if (inbox != nullptr)
        inbox->push(heldPitch_[(size_t)index], 0, offset);
    inbox = nullptr;
    releaseIn_[(size_t)index] = 0;
}

// Only notes played from MIDI end on a Note-Off; a card audition ends on its own timer.
void DrumKitModule::releaseInstrumentNote(int note, int offset) noexcept {
    if (heldPads_ == nullptr)
        return;
    for (int i = 0; i < kMaxPads; ++i)
        if (heldInbox_[(size_t)i] != nullptr && releaseIn_[(size_t)i] == 0 && heldPads_->pads[(size_t)i].note == note)
            releaseInstrumentPad(i, offset);
}

void DrumKitModule::releaseAllInstrumentPads(int offset) noexcept {
    for (int i = 0; i < kMaxPads; ++i)
        releaseInstrumentPad(i, offset);
}

// Runs before a new snapshot replaces the held one: a held note whose pad no longer reaches the same inbox ends now,
// while the old snapshot still keeps that inbox alive.
void DrumKitModule::releaseMovedInstrumentPads(const synth::drums::PadSnapshot& next) noexcept {
    for (int i = 0; i < kMaxPads; ++i)
        if (heldInbox_[(size_t)i] != nullptr && next.pads[(size_t)i].inbox.get() != heldInbox_[(size_t)i])
            releaseInstrumentPad(i, 0);
}

void DrumKitModule::advanceAuditionReleases(int numSamples) noexcept {
    for (int i = 0; i < kMaxPads; ++i) {
        auto& left = releaseIn_[(size_t)i];
        if (left <= 0)
            continue;
        if (left < numSamples)
            releaseInstrumentPad(i, left);
        else
            left -= numSamples;
    }
}
