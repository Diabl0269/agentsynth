#pragma once

// AudioEngine's single RemoteMessageSink (docs/control/midi-remote-mcu-hui.md#1-one-seam-routed-by-source): a port
// bound to a protocol surface is consumed whole by that surface, everything else goes to the fallback
// (RemoteEngine) unchanged.

#include "MidiRemote/RemoteEngine/RemoteMessageSink.h"
#include "MidiRemote/RemoteEngine/SnapshotPublisher.h"
#include "MidiRemote/Surface/ProtocolSurface.h"

#include <memory>
#include <vector>

namespace synth::midi {

/** The immutable owned-port table; built on the message thread, read on the MIDI thread. */
struct OwnedSourceTable {
    struct Entry {
        juce::String sourceKey; // the juce::MidiInput identifier
        std::shared_ptr<ProtocolSurface> surface;
    };
    std::vector<Entry> entries;

    ProtocolSurface* find(const juce::String& sourceKey) const noexcept;
};

class RemoteSinkRouter final : public RemoteMessageSink {
public:
    /** `fallback` must outlive the router. */
    explicit RemoteSinkRouter(RemoteMessageSink& fallback) noexcept
        : fallback_(fallback) {}

    bool handleMessage(const juce::String& sourceKey, const juce::MidiMessage& message) noexcept override;

    /** Message thread. Replaces the owned-port table; an empty vector owns nothing. */
    void setOwnedSources(std::vector<OwnedSourceTable::Entry> entries);
    /** Message thread. Frees retired tables (and any surface only they held) once no reader can hold one. */
    void collectRetired() noexcept { publisher_.collectRetired(); }
    /** Message thread. */
    bool owns(const juce::String& sourceKey) const noexcept;

    const SnapshotPublisherOf<OwnedSourceTable>& publisher() const noexcept { return publisher_; }

private:
    RemoteMessageSink& fallback_;
    SnapshotPublisherOf<OwnedSourceTable> publisher_;
};

} // namespace synth::midi
