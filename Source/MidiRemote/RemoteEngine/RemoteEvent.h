#pragma once

// The POD the MIDI path hands to the message thread, and the lock-free rings that carry it
// (docs/control/midi-remote.md#threading-the-mapping-table-crosses-threads, docs/control/midi-remote.md#the-engine).
//
// WHY A POOL OF RINGS AND NOT ONE. AutomationUiFeed (Source/Timeline/AutomationUiFeed.h) is the
// pattern this copies, but it has exactly one producer — the audio thread. MIDI Remote does not:
// standalone opens one juce::MidiInput per profiled device and each may deliver on its own driver
// thread. juce::AbstractFifo is single-producer, so a shared ring would be torn by two devices
// moving at once. Instead every *source* owns its own lane, and a source's lane index is assigned
// once on the message thread and never reused for another key while the engine lives — so a lane
// can never acquire a second concurrent producer. Sequential producers on different threads are
// fine: AbstractFifo's write index is an atomic with release semantics, so a later thread sees the
// earlier one's writes.

#include "MidiRemote/RemoteEngine/RemoteLaneState.h"
#include "MidiRemote/RemoteModel.h"

#include <atomic>
#include <cstdint>
#include <juce_core/juce_core.h>
#include <memory>
#include <type_traits>
#include <vector>

namespace synth::midi {

enum class RemoteEventKind : std::uint8_t {
    absolute,      // value = 0..1, already scaled by the control's encoding
    relativeDelta, // value = signed delta in normalised units
    buttonPress,
    buttonRelease,
    learnCandidate, // no assignment: the spec fields carry the candidate message key
};

/** One decoded hardware event. Trivially copyable and small on purpose — this is written on the
 *  MIDI/audio thread, so it must be a plain memcpy into a pre-allocated slot. */
struct RemoteEvent {
    std::int32_t slotIndex = -1; // index into RemoteMappingSnapshot::slots, or -1 when unassigned
    /** Which snapshot `slotIndex` and `sourceIndex` were resolved against. A republish between the
     *  MIDI-thread push and the message-thread drain would otherwise reinterpret a queued index
     *  against a shifted table and move the WRONG parameter; the drain discards events whose
     *  generation is not the live one. Losing at most one frame of events at the exact moment an
     *  assignment changes is invisible, and a gesture left mid-sweep still ends correctly via the
     *  kGestureIdleMs timeout. */
    std::uint32_t generation = 0;
    float value = 0.0f;
    std::uint8_t sourceIndex = 0;
    RemoteEventKind kind = RemoteEventKind::absolute;
    std::uint8_t specType = 0;    // static_cast<std::uint8_t>(synth::MessageType)
    std::uint8_t specChannel = 0; // 1..16 exactly as received (never the profile's 0 = "any")
    /** cc/note number; 0 for pitchBend / channelPressure; the 14-bit ADDRESS for an nrpn event. For a
     *  paired-CC control this is whichever half (MSB n or LSB n+32) completed the value. */
    std::uint16_t specNumber = 0;
    /** Low 16 bits of juce::Time::getMillisecondCounter() when the MIDI path decoded this event
     *  (wraps every ~65 s; compare with a modular difference). Detect mode's "both halves within
     *  5 ms" rule (docs/control/midi-remote-ui.md#detect-mode) is the only reader. */
    std::uint16_t timeMs = 0;
    /** The message's own 7-bit data byte exactly as received (cc value / note velocity / pressure --
     *  for a paired/nrpn event, the completing half's byte), never scaled or decoded -- what `value` is NOT for a
     * relative encoding or a range-mapped assignment. Encoder auto-detect (docs/control/midi-remote-ui.md#detect-mode)
     * classifies the raw pattern, so it must see this whatever encoding the control currently claims. */
    std::uint8_t rawValue = 0;
};

static_assert(std::is_trivially_copyable_v<RemoteEvent>, "RemoteEvent rides a lock-free ring");
static_assert(sizeof(RemoteEvent) <= 24, "keep RemoteEvent small: it is memcpy'd on the MIDI path");

/** Lock-free SPSC ring of RemoteEvent, pre-allocated at construction. push() never allocates,
 *  never blocks and never logs; a full ring drops the *newest* event, matching AutomationUiFeed —
 *  a dropped event costs one frame of knob movement, a blocked MIDI thread costs an xrun.
 *
 *  WHY THIS IS HAND-ROLLED INSTEAD OF juce::AbstractFifo. AutomationUiFeed wraps
 *  juce::AbstractFifo, and that wrapping is NOT ThreadSanitizer-clean: a one-producer /
 *  one-consumer test over it reports a data race on the slot payload (see
 *  Tests/MidiRemote/RemoteEngineThreadingTests.cpp, and FRO187 for the AutomationUiFeed
 *  instance of the same finding). AbstractFifo publishes its indices through juce::Atomic,
 *  whose get()/set() are seq_cst, but it exposes no release/acquire pairing between the
 *  *payload* write and the index publication that a sanitizer can follow, so the payload
 *  handoff is not provably ordered. docs/control/midi-remote.md#threading-the-mapping-table-crosses-threads asks for a
 * table crossing threads with no lock and no race, and "no race" has to mean provable, not untested — so this ring
 * publishes the payload explicitly: the producer stores the event, then releases the write index; the consumer acquires
 * the write index, then reads the event. The consumer advances the read index only after it has finished with every
 * slot it read, and the producer acquires the read index before it may overwrite one, so a slot in flight is never
 *  reclaimed. One slot is always left empty, which is what makes full and empty distinct. */
class RemoteEventFifo {
public:
    explicit RemoteEventFifo(int capacity)
        : slots(static_cast<std::size_t>(capacity) + 1) {} // +1: one slot stays empty by design

    // MIDI/AUDIO THREAD.
    void push(const RemoteEvent& event) noexcept {
        const int write = writeIndex.load(std::memory_order_relaxed);
        const int next = advance(write);
        if (next == readIndex.load(std::memory_order_acquire))
            return; // full: drop the newest, silently
        slots[static_cast<std::size_t>(write)] = event;
        writeIndex.store(next, std::memory_order_release);
    }

    // MESSAGE THREAD.
    template <typename Fn>
    void drain(Fn&& fn) {
        int read = readIndex.load(std::memory_order_relaxed);
        const int write = writeIndex.load(std::memory_order_acquire);
        while (read != write) {
            fn(slots[static_cast<std::size_t>(read)]);
            read = advance(read);
        }
        readIndex.store(read, std::memory_order_release);
    }

    int getNumReady() const noexcept {
        const int write = writeIndex.load(std::memory_order_acquire);
        const int read = readIndex.load(std::memory_order_relaxed);
        const int size = static_cast<int>(slots.size());
        return write >= read ? write - read : size - (read - write);
    }

private:
    int advance(int index) const noexcept { return index + 1 == static_cast<int>(slots.size()) ? 0 : index + 1; }

    std::vector<RemoteEvent> slots;
    std::atomic<int> writeIndex{0};
    std::atomic<int> readIndex{0};

    static_assert(std::atomic<int>::is_always_lock_free, "the MIDI path may not take a lock to queue");
};

/** Everything one source (one device, or "host") writes. `activity` mirrors every decoded event,
 *  assigned or not, for the MIDI Remote panel's live surface (docs/control/midi-remote.md#the-engine) — a separate
 *  ring so the panel drawing at its own rate can never steal an event from apply(). */
struct SourceLane {
    static constexpr int kEventCapacity = 512;
    static constexpr int kActivityCapacity = 256;

    SourceLane()
        : events(kEventCapacity)
        , activity(kActivityCapacity) {}

    RemoteEventFifo events;
    RemoteEventFifo activity;
    /** The MIDI path's per-source memory (paired-CC halves, NRPN address). Producer thread only. */
    LaneState state;
};

/** Upper bound on distinct sources. Lanes are allocated once, at engine construction: a lane index
 *  is handed out on the message thread when a source key is first seen and is never recycled, so a
 *  17th controller is simply not routed (and says so once, on the message thread) rather than
 *  risking two producers sharing a lane. "host" always takes index 0. */
inline constexpr int kMaxRemoteSources = 16;

} // namespace synth::midi
