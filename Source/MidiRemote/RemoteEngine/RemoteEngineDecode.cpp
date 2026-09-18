// MIDI / audio thread. handleMessage() is the whole point of this ticket: no lock, no allocation,
// no logging, no juce::String construction, no AsyncUpdater. See RemoteEngine.h's class comment
// and docs/control/midi-remote.md#are-mapped-messages-consumed-or-also-forwarded-to-the-graph /
// docs/control/midi-remote.md#threading-the-mapping-table-crosses-threads for the contract this file must not violate.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "MidiRemote/RemoteEngine/RemoteEngineInternal.h"

namespace synth::midi {

namespace {

RemoteEvent buildBaseEvent(const detail::ClassifiedMessage& classified, int sourceIndex,
                           std::uint32_t generation) noexcept {
    RemoteEvent event;
    event.generation = generation;
    event.sourceIndex = static_cast<std::uint8_t>(sourceIndex);
    event.specType = static_cast<std::uint8_t>(classified.type);
    event.specChannel = static_cast<std::uint8_t>(classified.channel);
    event.specNumber = static_cast<std::uint8_t>(classified.number);
    return event;
}

// Step 4: a learn never consumes -- push to BOTH rings and let the caller return false regardless.
void pushLearnCandidate(SourceLane& lane, const detail::ClassifiedMessage& classified, int sourceIndex,
                        std::uint32_t generation) noexcept {
    RemoteEvent event = buildBaseEvent(classified, sourceIndex, generation);
    event.slotIndex = -1;
    event.kind = RemoteEventKind::learnCandidate;
    event.value = detail::rawNormalisedValue(classified);
    lane.events.push(event);
    lane.activity.push(event);
}

// Step 5: an unassigned control is never consumed -- activity ring only, so the panel's Detect
// surface still lights up without stealing the message from applyEvent.
void pushActivityOnly(SourceLane& lane, const detail::ClassifiedMessage& classified, int sourceIndex,
                      std::uint32_t generation) noexcept {
    RemoteEvent event = buildBaseEvent(classified, sourceIndex, generation);
    event.slotIndex = -1;
    event.kind = classified.isNoteOff  ? RemoteEventKind::buttonRelease
                 : classified.isNoteOn ? RemoteEventKind::buttonPress
                                       : RemoteEventKind::absolute;
    event.value = detail::rawNormalisedValue(classified);
    lane.activity.push(event);
}

void decodeButtonLike(RemoteEvent& event, const detail::ClassifiedMessage& classified) noexcept {
    switch (classified.type) {
    case MessageType::note:
        if (classified.isNoteOn) {
            event.kind = RemoteEventKind::buttonPress;
            event.value = static_cast<float>(classified.rawValue) / 127.0f;
        } else {
            event.kind = RemoteEventKind::buttonRelease;
            event.value = 0.0f;
        }
        break;
    case MessageType::cc:
        if (classified.rawValue >= 64) {
            event.kind = RemoteEventKind::buttonPress;
            event.value = static_cast<float>(classified.rawValue) / 127.0f;
        } else {
            event.kind = RemoteEventKind::buttonRelease;
            event.value = 0.0f;
        }
        break;
    case MessageType::programChange:
        event.kind = RemoteEventKind::buttonPress;
        event.value = 1.0f;
        break;
    case MessageType::pitchBend:
    case MessageType::channelPressure:
    default:
        // Not a meaningful button encoding; decode as an inert release rather than fabricate a
        // press (docs/control/midi-remote.md#data-model never assigns a button-like control to these message types).
        event.kind = RemoteEventKind::buttonRelease;
        event.value = 0.0f;
        break;
    }
}

// Step 6: decode a message assigned to `slot` into the RemoteEvent handleMessage pushes.
RemoteEvent decodeEvent(const RemoteMappingSnapshot::Slot& slot, const detail::ClassifiedMessage& classified,
                        int sourceIndex, int slotIndex, std::uint32_t generation) noexcept {
    RemoteEvent event = buildBaseEvent(classified, sourceIndex, generation);
    event.slotIndex = slotIndex;

    if (slot.buttonLike) {
        decodeButtonLike(event, classified);
        return event;
    }

    switch (slot.encoding) {
    case Encoding::abs7:
        event.kind = RemoteEventKind::absolute;
        event.value = classified.type == MessageType::pitchBend
                          ? detail::decodeAbs7PitchBend(classified.pitchWheelValue)
                          : detail::decodeAbs7(classified.rawValue);
        break;
    case Encoding::relTwos:
        event.kind = RemoteEventKind::relativeDelta;
        event.value = detail::decodeRelTwos(classified.rawValue);
        break;
    case Encoding::relBinOffset:
        event.kind = RemoteEventKind::relativeDelta;
        event.value = detail::decodeRelBinOffset(classified.rawValue);
        break;
    case Encoding::relSignMag:
        event.kind = RemoteEventKind::relativeDelta;
        event.value = detail::decodeRelSignMag(classified.rawValue);
        break;
    }
    return event;
}

} // namespace

bool RemoteEngine::handleMessage(const juce::String& sourceKey, const juce::MidiMessage& message) noexcept {
    SnapshotPublisher::ReadGuard guard(publisher_);
    const auto* snap = guard.get();
    if (snap == nullptr)
        return false;

    const detail::ClassifiedMessage classified = detail::classifyMessage(message);
    if (!classified.eligible)
        return false;

    const int sourceIndex = snap->findSource(sourceKey);
    if (sourceIndex < 0)
        return false;

    SourceLane& lane =
        *lanes_[static_cast<std::size_t>(snap->sources[static_cast<std::size_t>(sourceIndex)].laneIndex)];

    if (learnToken_.load(std::memory_order_seq_cst) != 0) {
        pushLearnCandidate(lane, classified, sourceIndex, snap->generation);
        return false;
    }

    const int slotIndex = snap->findSlot(sourceIndex, classified.type, classified.channel, classified.number);
    if (slotIndex < 0) {
        pushActivityOnly(lane, classified, sourceIndex, snap->generation);
        return false;
    }

    const RemoteEvent event = decodeEvent(snap->slots[static_cast<std::size_t>(slotIndex)], classified, sourceIndex,
                                          slotIndex, snap->generation);
    lane.events.push(event);
    lane.activity.push(event);

    return !snap->sources[static_cast<std::size_t>(sourceIndex)].passMapped;
}

} // namespace synth::midi
