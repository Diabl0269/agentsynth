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
    event.specNumber = static_cast<std::uint16_t>(classified.number);
    event.timeMs = static_cast<std::uint16_t>(juce::Time::getMillisecondCounter() & 0xffffu);
    event.rawValue = static_cast<std::uint8_t>(classified.rawValue);
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
    case MessageType::nrpn:
    default:
        // Not a meaningful button encoding; decode as an inert release rather than fabricate a
        // press (docs/control/midi-remote.md#data-model never assigns a button-like control to these message types).
        event.kind = RemoteEventKind::buttonRelease;
        event.value = 0.0f;
        break;
    }
}

// Step 6a (FRO140): a paired-CC or nrpn message carries only one half of its value. The value is
// committed when the encoding's SECOND half arrives (abs14: the LSB, abs14LsbFirst: the MSB), using
// the other half as last seen; the first half is remembered by the lane and produces no event, so
// a controller that sends MSB-only never moves the parameter (docs/control/midi-remote.md
// #14-bit-and-nrpn-encodings). An nrpn on abs7 means "CC 6 only". Returns false for a held half.
bool decodeTwoHalves(const RemoteMappingSnapshot::Slot& slot, const detail::ClassifiedMessage& classified,
                     RemoteEvent& event) noexcept {
    using Half = detail::ClassifiedMessage::Half;
    event.kind = RemoteEventKind::absolute;
    if (slot.encoding == Encoding::abs7) { // nrpn only: reconcile never aliases a cc onto abs7
        if (classified.half != Half::msb)
            return false;
        event.value = detail::decodeAbs7(classified.rawValue);
        return true;
    }
    const bool commits = slot.encoding == Encoding::abs14LsbFirst ? classified.half == Half::msb && classified.haveLsb
                                                                  : classified.half == Half::lsb && classified.haveMsb;
    if (!commits)
        return false;
    event.value = detail::decodeAbs14(classified.value14);
    return true;
}

// Step 6: decode a message assigned to `slot` into the RemoteEvent handleMessage pushes. Returns
// false when the message is a held half of a two-message value: it is consumed but moves nothing.
bool decodeEvent(const RemoteMappingSnapshot::Slot& slot, const detail::ClassifiedMessage& classified, int sourceIndex,
                 int slotIndex, std::uint32_t generation, RemoteEvent& event) noexcept {
    event = buildBaseEvent(classified, sourceIndex, generation);
    event.slotIndex = slotIndex;

    if (slot.buttonLike) {
        decodeButtonLike(event, classified);
        return true;
    }

    if (classified.half != detail::ClassifiedMessage::Half::none)
        return decodeTwoHalves(slot, classified, event);

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
    case Encoding::abs14:
    case Encoding::abs14LsbFirst:
        // A paired slot only ever sees a cc/nrpn message with a half set (returned above); reaching
        // here means the slot's encoding disagrees with its message type -- inert, not a guess.
        return false;
    }
    return true;
}

} // namespace

bool RemoteEngine::handleMessage(const juce::String& sourceKey, const juce::MidiMessage& message) noexcept {
    SnapshotPublisher::ReadGuard guard(publisher_);
    const auto* snap = guard.get();
    if (snap == nullptr)
        return false;

    detail::ClassifiedMessage classified = detail::classifyMessage(message);
    if (!classified.eligible)
        return false;

    const int sourceIndex = snap->findSource(sourceKey);
    if (sourceIndex < 0)
        return false;

    SourceLane& lane =
        *lanes_[static_cast<std::size_t>(snap->sources[static_cast<std::size_t>(sourceIndex)].laneIndex)];

    // Step 2b (FRO140): NRPN address CCs update the lane's state and pass through untouched; CC 6/38
    // under an armed address become one nrpn message that everything below treats like any other.
    if (detail::advanceNrpn(lane.state, *snap, sourceIndex, classified) == detail::NrpnStep::addressConsumed)
        return false;

    if (learnToken_.load(std::memory_order_seq_cst) != 0) {
        pushLearnCandidate(lane, classified, sourceIndex, snap->generation);
        return false;
    }

    const int slotIndex = snap->findSlot(sourceIndex, classified.type, classified.channel, classified.number);
    if (slotIndex < 0) {
        pushActivityOnly(lane, classified, sourceIndex, snap->generation);
        return false;
    }

    const auto& slot = snap->slots[static_cast<std::size_t>(slotIndex)];
    detail::foldPairedHalf(lane.state, slot, classified);

    const bool consumed = !snap->sources[static_cast<std::size_t>(sourceIndex)].passMapped;
    RemoteEvent event;
    if (!decodeEvent(slot, classified, sourceIndex, slotIndex, snap->generation, event))
        return consumed; // a held half: swallowed like any mapped message, but nothing to apply yet
    lane.events.push(event);
    lane.activity.push(event);

    return consumed;
}

} // namespace synth::midi
