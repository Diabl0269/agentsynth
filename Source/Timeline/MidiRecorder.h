#pragma once

#include "../Transport/BlockTimeInfo.h"
#include "TimelineDoc.h"
#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth {

/**
 * @brief TL3-3: records external MIDI into a new timeline clip, as one undo step.
 *
 * -- Why the collector-drained buffer is the only correct capture point ------------------------
 *
 * External MIDI reaches the graph by TWO paths out of AudioEngine::handleIncomingMidiMessage: a
 * direct `pushMidiMessage()` copy into any ExternalMidiModule node bound to the same device, AND
 * `midiMessageCollector.addMessageToQueue()`, which is drained once per callback (in
 * `audioDeviceIOCallbackWithContext`, or supplied directly by the host in hosted mode) into the
 * buffer `AudioEngine::renderNextBlock` hands the graph. `captureBlock()` is called from exactly
 * that one site — see AudioEngine::setMidiCaptureSink / renderNextBlock — and reads ONLY that
 * buffer. It never sees the ExternalMidiModule push-path copies, which stay internal to that
 * module's own processing and never reach the top-level buffer this class is handed. Since every
 * message handleIncomingMidiMessage receives goes into the collector regardless of whether an
 * ExternalMidiModule also received a copy, recording from the collector-merged buffer alone
 * captures every external note exactly once — recording from both paths would double-record any
 * note whose source also has an ExternalMidi node bound to it.
 *
 * -- Threading contract -------------------------------------------------------------------------
 *
 * captureBlock() — AUDIO THREAD ONLY, called once per callback. Lock-free, no allocation, no
 * logging (see the "No high-frequency logging" rule in CLAUDE.md).
 *
 * startRecording() / stopAndCommit() — MESSAGE THREAD ONLY.
 *
 * The state captureBlock() reads that the message thread writes is the armed-and-recording flag and
 * (TL5-6) the punch-in beat — both plain relaxed atomics, written once by startRecording() before
 * `recording` flips true and never mutated again until the next startRecording(), so captureBlock()
 * never observes a punch-in value from a DIFFERENT take than the one it is currently capturing. The
 * only data that crosses from the audio thread to the message thread is the SPSC ring. Nothing else
 * is shared between the two sides.
 *
 * -- What a "take" is -----------------------------------------------------------------------------
 *
 * stopAndCommit() pairs NoteOn/NoteOff events by (pitch, channel) into notes, then creates ONE new
 * clip on the armed track spanning the captured notes, named "Take". A note still held down when
 * recording stops is closed at the last event beat seen (the best proxy for "now" this class has,
 * since it deliberately shares no timing state with the audio thread beyond the ring itself), with
 * a floor of 1/32 beat so a note is never emitted with zero or negative length. A stray NoteOff
 * with no matching NoteOn is ignored. An empty take (no events captured) commits nothing and
 * returns false — no clip, no undo step.
 */
class MidiRecorder {
public:
    MidiRecorder() = default;
    ~MidiRecorder() = default;

    // Capacity of the lock-free audio-thread -> message-thread event ring. Sized well above one
    // real take's worth of events; overflowing it drops the excess (see hadOverrun()) rather than
    // blocking the audio thread.
    static constexpr int kRingCapacity = 4096;

    // Floor applied to a dangling note's length (an on with no matching off by stop time) so a
    // note recorded right at the moment of stopping is never emitted with zero length.
    static constexpr double kMinNoteLengthBeats = 1.0 / 32.0;

    // -- Audio thread --------------------------------------------------------
    // Called once per callback (see AudioEngine::renderNextBlock). A no-op unless armed-and-
    // recording AND the transport is playing. For every NoteOn/NoteOff in `midi`, computes its
    // absolute beat from `info` (wrap-aware: a sample at or after info.loopWrapSample uses the
    // post-wrap range); an event whose beat is BEFORE `punchInBeat` (TL5-6: a count-in pre-roll) is
    // dropped rather than pushed — the pre-roll bars a performer plays along with the click must
    // never land in the committed take. Never allocates, never blocks, never logs.
    void captureBlock(const juce::MidiBuffer& midi, const BlockTimeInfo& info) noexcept;

    // -- Message thread --------------------------------------------------------
    // Clears the ring and any prior take's state, arms recording onto `armedTrack`. `punchInBeat` is
    // BOTH the caller's own bookkeeping value (e.g. UI punch-in display) AND the audio thread's
    // filter threshold (TL5-6): captureBlock() drops any event before it, which is what makes a
    // count-in's pre-roll bars silent in the committed take even though the transport (and the
    // recorder's armed-and-recording flag) are already live through them. Pass the CURRENT position
    // for "no pre-roll" (record engaged while already playing, or count-in off) — nothing before
    // "now" can arrive anyway, so the filter is then a no-op. The committed clip's bounds are still
    // derived from the captured notes themselves, never from this value.
    void startRecording(TrackId armedTrack, double punchInBeat);

    // Message thread (or any thread — see the class comment: this is the one piece of "message-
    // thread-only" bookkeeping a caller also needs to read live, to know when a count-in pre-roll is
    // over). Mirrors whatever punchInBeat startRecording() was last called with.
    double getPunchInBeat() const noexcept { return punchInBeat_.load(std::memory_order_relaxed); }

    // Disarms recording, drains the ring, and — if anything was captured — commits ONE new clip on
    // the armed track as a single undo step via undo.recordTimelineChange(). Returns false (and
    // commits nothing) if no events were captured, mirroring recordTimelineChange's own "no-op
    // commits nothing" contract.
    bool stopAndCommit(TimelineDoc& doc, AppUndoManager& undo);

    // True if the ring overflowed since the last startRecording() — the take is still committed,
    // just truncated. Surfaced by the caller as a dropped-take warning.
    bool hadOverrun() const noexcept { return overrunFlag.load(std::memory_order_relaxed); }

    // TL5-5: message-thread probe for "is a take currently armed?" — MainComponent's 10 Hz poll
    // uses this to detect a transport stop happening WHILE recording (Space/Stop rather than the
    // record button itself) and route it through the same stopAndCommit path. Safe from the
    // message thread: it only ever reads its own atomic, which the message thread itself writes
    // (startRecording/stopAndCommit); the audio thread never writes `recording` from here.
    bool isRecording() const noexcept { return recording.load(std::memory_order_relaxed); }

private:
    struct Event {
        double beat = 0.0;
        int pitch = 0;
        int velocity = 0;
        int channel = 1;
        bool isNoteOn = false;
    };

    std::atomic<bool> recording{false};
    std::atomic<bool> overrunFlag{false};

    // SPSC ring: audio thread writes (captureBlock), message thread reads (stopAndCommit). Modelled
    // on TransportService's command FIFO — a fixed-size juce::AbstractFifo, no allocation.
    juce::AbstractFifo ring{kRingCapacity};
    std::array<Event, kRingCapacity> ringSlots;

    // Message-thread-only bookkeeping, set by startRecording and read by stopAndCommit. Never
    // touched by the audio thread.
    TrackId armedTrack;

    // TL5-6: written by startRecording() (message thread), read by BOTH stopAndCommit() (message
    // thread, informational) and captureBlock() (audio thread, the actual pre-roll filter) — a
    // relaxed atomic double, the same cross-thread-signal-value convention TransportService's
    // simpler settings (masterMuted_, transportEnabled_) use, rather than the full seqlock its own
    // multi-field PositionSnapshot needs.
    std::atomic<double> punchInBeat_{0.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRecorder)
};

} // namespace synth
