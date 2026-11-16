// RemoteSinkRouter: the per-message owner lookup. The table is tiny (one entry per bound surface port,
// at most kMaxRemoteSources), so a linear scan of juce::String compares is both allocation-free and
// faster than anything cleverer.
#include "RemoteSinkRouter.h"

namespace synth::midi {

ProtocolSurface* OwnedSourceTable::find(const juce::String& sourceKey) const noexcept {
    for (const auto& entry : entries)
        if (entry.sourceKey == sourceKey)
            return entry.surface.get();
    return nullptr;
}

// MIDI/AUDIO THREAD. An owned port's every message -- note, CC, sysex, anything -- is consumed here,
// so it never reaches the collector, ExternalMidiModule, a recording take or a learn. The ReadGuard
// spans the decode, so the surface (owned by the table) cannot be freed under it.
bool RemoteSinkRouter::handleMessage(const juce::String& sourceKey, const juce::MidiMessage& message) noexcept {
    {
        SnapshotPublisherOf<OwnedSourceTable>::ReadGuard guard(publisher_);
        if (const auto* table = guard.get(); table != nullptr)
            if (auto* surface = table->find(sourceKey); surface != nullptr) {
                surface->decode(message);
                return true;
            }
    }
    return fallback_.handleMessage(sourceKey, message);
}

void RemoteSinkRouter::setOwnedSources(std::vector<OwnedSourceTable::Entry> entries) {
    auto table = std::make_unique<OwnedSourceTable>();
    table->entries = std::move(entries);
    publisher_.publish(std::move(table));
    publisher_.collectRetired();
}

bool RemoteSinkRouter::owns(const juce::String& sourceKey) const noexcept {
    const auto* table = publisher_.live();
    return table != nullptr && table->find(sourceKey) != nullptr;
}

} // namespace synth::midi
