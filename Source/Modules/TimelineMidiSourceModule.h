#pragma once

#include "../Timeline/TimelineSnapshot.h"
#include "../Transport/TransportService.h"
#include "ModuleBase.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <juce_audio_basics/juce_audio_basics.h>
#include <limits>

/**
 * @brief "Track In" — the graph-side end of a timeline MIDI track.
 *
 * One node per MIDI track, bound by uuid at creation. Playback schedules nothing into it: every
 * block it reads the transport's BlockTimeInfo and TimelineSnapshot off the playhead
 * (TransportService::setCurrentTimelineSnapshot), finds the track whose bindingUuid matches its
 * own node uuid, and emits the notes whose start or end falls inside the block's beat range —
 * a pure function of (block position, snapshot, currently-held notes), so a locate/tempo/edit
 * takes effect next block with no invalidation step.
 *
 * AUDITION is the ONE exception, and the only thing anything outside pushes IN:
 * pushAuditionNote() lets the piano roll sound a note the user clicked. It emits from THIS node,
 * which is what makes "audition reaches the same modules the track plays through" true by
 * construction rather than by re-deriving the destination list — a track's MIDI destinations are
 * graph connections whose SOURCE is this node's MIDI output (MainComponent::
 * setMidiDestinationConnected), so anything leaving here goes exactly where the clip's notes go.
 * The handoff is a fixed-capacity juce::AbstractFifo of POD events (TransportService's command-FIFO
 * idiom), drained once per block: no lock, no allocation and no logging on the audio thread, and a
 * full FIFO DROPS the event rather than blocking. An auditioned note is parked in the same
 * active_ table a played note is, with an INFINITE end beat (kAuditionEndBeat) so emitRange's
 * end-beat scan can never release it — its own note-off does, and so does every hygiene flush
 * below, which is what keeps a held preview from surviving a stop/locate/bypass/loop wrap.
 * Deliberately NOT gated on the track's mute/solo: audition is a monitor path, the same way
 * clicking a key on a MIDI Keyboard module is.
 *
 * INTERNAL-ONLY: not in the module library, not offered by the replace menu, and explicitly
 * non-authorable by the AI (kNonAuthorableModuleTypes) — a model could otherwise wire itself to a
 * track it did not create.
 *
 * Held-note hygiene: a note-on emitted here is a promise to emit the matching note-off. Anything
 * that could break that promise (stop, locate, bypass, track deleted/muted/soloed away) flushes
 * every held note as note-offs at sample 0 first. Always PER-NOTE, never a blanket CC 123 — a MIDI
 * cable can carry several sources downstream.
 *
 * Loop wrap: a block crossing the loop end is TWO beat ranges (see BlockTimeInfo); every held note
 * is released exactly at the wrap, then the wrapped range starts fresh, so a note can legitimately
 * end and restart in the same block. BlockTimeInfo reports only the FIRST wrap per block, so a
 * loop shorter than the block drops whole intermediate passes (never leaks a note — the
 * release-at-wrap still runs).
 *
 * Ordering: where a note-off and note-on land on the same sample offset, the off is always
 * inserted first (juce::MidiBuffer preserves insertion order at equal positions) — required by
 * Poly MIDI's same-pitch retrigger contract (docs/modules.md).
 */
class TimelineMidiSourceModule : public ModuleBase {
public:
    // Held notes are tracked in a fixed array — the audio thread must not allocate. 128 is one of
    // every pitch; a track holding more simultaneously is pathological, and the overflow policy is
    // to DROP the note-on (never the note-off of something already sounding, and never a resize).
    static constexpr int kMaxActiveNotes = 128;

    // The audition hand-off FIFO's depth. A preview is one event per mouse-down/up/drag-step, so
    // even a frantic drag posts a handful per audio block; 64 is generous and keeps the slot array
    // trivially small. Mirrors TransportService::kFifoCapacity's role exactly.
    static constexpr int kAuditionFifoCapacity = 64;

    TimelineMidiSourceModule()
        : ModuleBase("Track In", 0, 0) {
        enableVisualBuffer(true);
    }

    ~TimelineMidiSourceModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
        numActive_ = 0;
        haveLastBlock_ = false;
        wasBypassed_ = false;
        wasPlaying_ = false;
        wasSuppressed_ = false;
    }

    /** AUDITION — pushes one preview note edge into this track's MIDI stream, to be emitted at
     *  sample 0 of the next block. Callable from ANY non-audio thread (in practice the message
     *  thread: the piano roll's note click, via TrackHeaderHost::auditionTrackNote) — the transfer is
     *  a wait-free single-producer FIFO write, so it never blocks the audio thread and never
     *  allocates on it.
     *
     *  A note-on for a pitch already being auditioned releases the old one first, so the held-note
     *  table cannot drift however the caller sequences a drag retrigger. A note-off for a pitch that
     *  is NOT currently auditioned emits nothing — it can only mean the note was already released by
     *  a hygiene flush, and a stray note-off would cut a timeline note of the same pitch short.
     *
     *  @return false when the FIFO is full and the event was DROPPED. The caller may re-post; it must
     *  not block. (A dropped note-ON is silence, which is survivable; a dropped note-OFF cannot
     *  strand a note, because every flush path releases audition notes too.) */
    bool pushAuditionNote(int pitch, int velocity, bool noteOn, int channel = 1) noexcept {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        auditionFifo_.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 + size2 < 1)
            return false;
        auditionSlots_[(std::size_t)start1] = {pitch, velocity, channel, noteOn};
        auditionFifo_.finishedWrite(1);
        return true;
    }

    /** How many auditioned notes are held right now (a subset of getActiveNoteCount()).
     *  Diagnostics and tests — not part of the audio contract. */
    int getAuditionNoteCount() const noexcept {
        int count = 0;
        for (int i = 0; i < numActive_; ++i)
            if (isAuditionNote(active_[i]))
                ++count;
        return count;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        // This module REPLACES the graph-supplied MIDI buffer (the ExternalMidiModule contract):
        // it is a source, so whatever the graph handed it is not ours to forward.
        midiMessages.clear();

        const int numSamples = buffer.getNumSamples();

        // Bypass: a pure MIDI source with no dry audio path, so the audio buffer is cleared on
        // both branches (see the bypass/mute contract in docs/architecture.md). The FIRST bypassed
        // block still emits the note-offs for whatever was sounding — dropping them silently would
        // leave every downstream envelope open.
        if (isBypassed()) {
            if (!wasBypassed_) {
                flushActiveNotes(midiMessages);
                wasBypassed_ = true;
            }
            // A bypassed source emits nothing, so anything the UI posted while we were bypassed is
            // DISCARDED rather than queued: replaying a stale click the moment bypass comes off
            // would be a note nobody asked for. The flush above already released whatever was held.
            discardAuditionEvents();
            haveLastBlock_ = false; // resuming is a discontinuity, not a continuation
            wasPlaying_ = false;
            buffer.clear();
            pushActivity(numSamples);
            return;
        }
        wasBypassed_ = false;

        auto* transport = dynamic_cast<synth::TransportService*>(getPlayHead());
        if (transport == nullptr) {
            // No transport at all (a foreign host's playhead, or none): nothing can be positioned,
            // so release anything held and go quiet. Audition goes with it — this node is not
            // usable at all in that state, and queueing the events for whenever a transport shows up
            // would sound a click the user made minutes ago.
            flushActiveNotes(midiMessages);
            discardAuditionEvents();
            haveLastBlock_ = false;
            wasPlaying_ = false;
            pushActivity(numSamples);
            return;
        }

        const synth::BlockTimeInfo& info = transport->getCurrentBlockInfo();
        // Borrowed for THIS BLOCK ONLY — never cached in a member (the exchange frees superseded
        // snapshots two callbacks after retirement).
        const synth::TimelineSnapshot* snapshot = transport->getCurrentTimelineSnapshot();

        // A stop, or a jump in the block's start sample (a locate, a tempo change re-deriving the
        // sample position, a host that repositions us), means the position we emitted against last
        // block does not continue into this one. Release everything first, then carry on against
        // the new position.
        //
        // A loop wrap is NOT such a jump: it is handled inside the block, so the prediction below
        // folds at the loop end exactly as the transport does. Predicting it wrongly would make the
        // block after every wrap look like a locate and kill the notes the wrapped range just
        // started.
        //
        // Continuity is only tracked WHILE PLAYING: a stopped transport republishes the same start
        // sample every block, which would otherwise read as a fresh discontinuity every callback.
        const std::int64_t nextBlockStart = predictNextBlockStart(info);
        const bool discontinuous = haveLastBlock_ && info.blockStartSample != expectedNextBlockStart_;
        if ((!info.playing && wasPlaying_) || (info.playing && discontinuous))
            flushTimelineNotes(midiMessages);

        wasPlaying_ = info.playing;
        haveLastBlock_ = info.playing;
        expectedNextBlockStart_ = nextBlockStart;

        // AUDITION, drained here — AFTER the stop/locate hygiene flush above (so a flush can never
        // land on top of a preview the same block started) and BEFORE every early return below, so a
        // click sounds with the transport STOPPED, on an unbound track, and on a muted one. That is
        // the point of a monitor path: the user asked to hear this note, not to hear the timeline.
        drainAuditionEvents(midiMessages);

        if (!info.playing || snapshot == nullptr || numSamples <= 0) {
            pushActivity(numSamples);
            return;
        }

        const synth::TimelineSnapshot::TrackInfo* track = findMyTrack(*snapshot);
        if (track == nullptr) {
            // The track was deleted, unbound or re-bound elsewhere while we held notes down.
            flushTimelineNotes(midiMessages);
            wasSuppressed_ = false;
            pushActivity(numSamples);
            return;
        }

        const bool suppressed = track->muted || (snapshot->anySoloed && !track->soloed);
        if (suppressed) {
            if (!wasSuppressed_)
                flushTimelineNotes(midiMessages);
            wasSuppressed_ = true;
            pushActivity(numSamples);
            return;
        }
        wasSuppressed_ = false;

        emitBlock(*snapshot, *track, info, midiMessages, nextBlockStart);
        pushActivity(numSamples);
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }

    ModuleType getModuleType() const override { return ModuleType::TimelineMidiSource; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Sequencer; }

    /** Held notes right now. Diagnostics and tests — not part of the audio contract. */
    int getActiveNoteCount() const noexcept { return numActive_; }

private:
    struct ActiveNote {
        int pitch = 0;
        int channel = 1;
        double endBeat = 0.0;
    };

    // The snapshot track bound to THIS node: a MIDI track whose bindingUuid equals our node uuid.
    // An empty node uuid matches nothing — an unsaved node has no identity yet, and "" would
    // otherwise match every unbound track in the document.
    const synth::TimelineSnapshot::TrackInfo* findMyTrack(const synth::TimelineSnapshot& snapshot) const noexcept {
        const char* myUuid = getNodeUuid();
        if (myUuid[0] == '\0')
            return nullptr;

        for (const auto& track : snapshot.tracks)
            if (track.kind == static_cast<int>(synth::TrackKind::Midi) && std::strcmp(track.bindingUuid, myUuid) == 0)
                return &track;

        return nullptr;
    }

    // One block = one beat range, or two when it wraps the loop. See the class comment.
    void emitBlock(const synth::TimelineSnapshot& snapshot, const synth::TimelineSnapshot::TrackInfo& track,
                   const synth::BlockTimeInfo& info, juce::MidiBuffer& midiMessages,
                   std::int64_t nextBlockStartSample) {
        const double beatsPerSample = info.beatsPerSample();
        if (!(beatsPerSample > 0.0))
            return;

        const int lastSample = juce::jmax(0, info.numSamples - 1);
        const bool wraps = info.loopWrapSample >= 0;

        // PRIMARY range. Capped at the loop end when this block wraps: everything from
        // loopWrapSample on belongs to the next pass, and info.endPpq is the UNWRAPPED virtual end
        // (it overshoots loopEndPpq), so using it here would emit beats the transport never plays.
        emitRange(snapshot, track, midiMessages, info.startPpq, wraps ? info.loopEndPpq : info.endPpq,
                  /*baseOffset=*/0, beatsPerSample, lastSample);

        if (!wraps)
            return;

        const int wrapOffset = juce::jlimit(0, lastSample, info.loopWrapSample);

        // The boundary itself: release everything this source still holds, per note, right at the
        // wrap. Anything alive here has an end beat at or beyond loopEndPpq — its note-off would
        // otherwise land outside the loop and never be reached. The wrapped range below then starts
        // the next pass from scratch, so a note that spans the boundary re-articulates rather than
        // being sustained through it.
        flushTimelineNotes(midiMessages, wrapOffset);

        // WRAPPED range: from the loop start to where the transport ACTUALLY lands after this block
        // — which is also where the next block's primary range begins, so the two tile exactly with
        // no beat emitted twice and none skipped. For a loop shorter than the block that end is the
        // final partial pass and the whole passes in between are simply not emitted (the multi-wrap
        // bound in the class comment); note hygiene is unaffected, the release above saw to that.
        emitRange(snapshot, track, midiMessages, info.loopStartPpq, beatFromSample(nextBlockStartSample, info),
                  wrapOffset, beatsPerSample, lastSample);
    }

    // Emits every note edge inside one beat range, at offsets measured from `baseOffset`. Notes are
    // sorted by startBeat within a track's run, so the scan could stop early; it does not, because a
    // note that STARTED before this range may still END inside it and the note-off scan has to see
    // it.
    void emitRange(const synth::TimelineSnapshot& snapshot, const synth::TimelineSnapshot::TrackInfo& track,
                   juce::MidiBuffer& midiMessages, double rangeStart, double rangeEnd, int baseOffset,
                   double beatsPerSample, int lastSample) {
        if (!(rangeEnd > rangeStart))
            return;

        // Note-offs first at each position: a re-articulation of the same pitch inside one range
        // must land off-then-on, and MidiBuffer keeps insertion order for equal sample positions
        // only within one addEvent sequence — so the two passes are separated deliberately.
        for (int i = numActive_ - 1; i >= 0; --i) {
            const ActiveNote& note = active_[i];
            if (note.endBeat < rangeEnd) {
                // An end beat BEFORE this range's start can only come of a tempo change moving the
                // grid under a held note; the clamp inside beatToOffset releases it at baseOffset.
                const int offset = beatToOffset(note.endBeat, rangeStart, beatsPerSample, baseOffset, lastSample);
                midiMessages.addEvent(juce::MidiMessage::noteOff(note.channel, note.pitch), offset);
                removeActiveAt(i);
            }
        }

        const int firstNote = track.firstNote;
        const int lastNote = track.firstNote + track.numNotes;
        for (int i = firstNote; i < lastNote; ++i) {
            const auto& note = snapshot.notes[static_cast<std::size_t>(i)];
            if (note.startBeat < rangeStart)
                continue;
            if (note.startBeat >= rangeEnd)
                break; // sorted by startBeat: everything after this one is later still

            // A zero- or negative-length note would emit an on with no matching off in range.
            if (!(note.endBeat > note.startBeat))
                continue;

            const int offset = beatToOffset(note.startBeat, rangeStart, beatsPerSample, baseOffset, lastSample);
            const int channel = juce::jlimit(1, 16, note.channel);
            const int pitch = juce::jlimit(0, 127, note.pitch);
            const int velocity = juce::jlimit(1, 127, note.velocity);

            if (!pushActive({pitch, channel, note.endBeat}))
                continue; // overflow: drop the note-on so we never owe an off we can't track

            midiMessages.addEvent(juce::MidiMessage::noteOn(channel, pitch, (juce::uint8)velocity), offset);

            // A note whose end also falls inside this range closes here. Emitted in this pass (not
            // the one above, which ran before this note existed) and therefore after its own
            // note-on, which is the order MidiBuffer preserves for equal sample positions.
            if (note.endBeat < rangeEnd) {
                const int offOffset = beatToOffset(note.endBeat, rangeStart, beatsPerSample, baseOffset, lastSample);
                midiMessages.addEvent(juce::MidiMessage::noteOff(channel, pitch), offOffset);
                removeActiveAt(numActive_ - 1);
            }
        }
    }

    // Beat -> block-relative sample offset, clamped into [baseOffset, lastSample]. The intermediate
    // clamp on the beat distance keeps llround away from an out-of-range double (a locate plus a
    // tempo change can make the raw difference astronomically large).
    static int beatToOffset(double beat, double rangeStart, double beatsPerSample, int baseOffset,
                            int lastSample) noexcept {
        const double rel = juce::jlimit(-1.0e9, 1.0e9, (beat - rangeStart) / beatsPerSample);
        const std::int64_t offset = (std::int64_t)baseOffset + std::llround(rel);
        return (int)juce::jlimit<std::int64_t>(baseOffset, juce::jmax(baseOffset, lastSample), offset);
    }

    // ConstantTempoMap's two conversions, recomputed from BlockTimeInfo (which publishes both the
    // bpm and the sample rate the transport used for THIS block).
    static double beatFromSample(std::int64_t samplePosition, const synth::BlockTimeInfo& info) noexcept {
        return (info.sampleRate > 0.0) ? (double)samplePosition * info.bpm / (60.0 * info.sampleRate) : 0.0;
    }

    static std::int64_t sampleFromBeat(double beat, const synth::BlockTimeInfo& info) noexcept {
        return (info.bpm > 0.0) ? (std::int64_t)std::llround(beat * 60.0 * info.sampleRate / info.bpm) : 0;
    }

    // Where the NEXT block will start, mirroring TransportService::tick()'s loop fold exactly (same
    // inputs — all of them published in BlockTimeInfo — and the same rounding). Two callers depend
    // on it: the continuity check, which must not read a wrap as a locate, and the wrapped range,
    // whose end is precisely this position expressed in beats.
    static std::int64_t predictNextBlockStart(const synth::BlockTimeInfo& info) noexcept {
        if (!info.playing || info.numSamples <= 0)
            return info.blockStartSample; // a stopped transport republishes the same position

        std::int64_t endSample = info.blockStartSample + info.numSamples;
        if (info.looping && info.loopEndPpq > info.loopStartPpq) {
            const std::int64_t loopStartSample = sampleFromBeat(info.loopStartPpq, info);
            const std::int64_t loopEndSample = sampleFromBeat(info.loopEndPpq, info);
            const std::int64_t loopLength = juce::jmax<std::int64_t>(1, loopEndSample - loopStartSample);
            // Wrap only when the block crosses the loop end from INSIDE the loop; a locate past the
            // end just plays on. The modulo is what folds a loop shorter than the block.
            if (info.blockStartSample < loopEndSample && endSample >= loopEndSample)
                endSample = loopStartSample + (endSample - loopEndSample) % loopLength;
        }
        return endSample;
    }

    bool pushActive(const ActiveNote& note) noexcept {
        if (numActive_ >= kMaxActiveNotes)
            return false;
        active_[numActive_++] = note;
        return true;
    }

    void removeActiveAt(int index) noexcept {
        if (index < 0 || index >= numActive_)
            return;
        active_[index] = active_[numActive_ - 1];
        --numActive_;
    }

    // Every held note released — one note-off each, never a blanket CC 123 — then forgotten. The one
    // place the note-on/note-off promise is honoured when the normal end-beat path can't run. The
    // offset is 0 for the hygiene flushes and loopWrapSample for the loop-boundary release.
    //
    // Used only where this node is about to stop being usable at all (bypass, no transport), because
    // it takes AUDITION notes with it — see flushTimelineNotes for everything else.
    void flushActiveNotes(juce::MidiBuffer& midiMessages, int offset = 0) {
        for (int i = 0; i < numActive_; ++i)
            midiMessages.addEvent(juce::MidiMessage::noteOff(active_[i].channel, active_[i].pitch), offset);
        numActive_ = 0;
    }

    // The same release, restricted to notes that came from the TIMELINE. Every positional hygiene
    // flush (stop, locate, loop wrap, track lost/muted/soloed away) uses this one: those events say
    // something about where the transport is, and an audition note is not on the transport's clock
    // at all — cutting a preview short because the user pressed Stop, or because their track is
    // muted, would break exactly the monitor path audition exists to provide. The preview's own
    // note-off (or a bypass) is what ends it.
    void flushTimelineNotes(juce::MidiBuffer& midiMessages, int offset = 0) {
        for (int i = numActive_ - 1; i >= 0; --i) {
            if (isAuditionNote(active_[i]))
                continue;
            midiMessages.addEvent(juce::MidiMessage::noteOff(active_[i].channel, active_[i].pitch), offset);
            removeActiveAt(i);
        }
    }

    // ---- Audition (see pushAuditionNote and the class comment) ----

    // An end beat no beat range can ever reach, so emitRange's "endBeat < rangeEnd" release scan
    // never fires for an auditioned note. It is also the MARKER that tells the two flush variants
    // apart — a timeline note always carries a finite end beat.
    static constexpr double kAuditionEndBeat = std::numeric_limits<double>::infinity();

    static bool isAuditionNote(const ActiveNote& note) noexcept { return !std::isfinite(note.endBeat); }

    struct AuditionEvent {
        int pitch = 60;
        int velocity = 100;
        int channel = 1;
        bool on = false;
    };

    // One FIFO read per event, applied immediately: the whole batch lands at sample 0 of this block,
    // which is the correct granularity for a gesture the user made between two callbacks.
    void drainAuditionEvents(juce::MidiBuffer& midiMessages) {
        for (;;) {
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            auditionFifo_.prepareToRead(1, start1, size1, start2, size2);
            if (size1 + size2 < 1)
                return;
            const AuditionEvent event = auditionSlots_[(std::size_t)(size1 > 0 ? start1 : start2)];
            auditionFifo_.finishedRead(1);
            applyAuditionEvent(event, midiMessages);
        }
    }

    // Empties the FIFO without emitting anything — the bypass / no-transport path (see their
    // comments). Reads through the same finishedRead accounting, never by resetting the FIFO.
    void discardAuditionEvents() {
        for (;;) {
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            auditionFifo_.prepareToRead(1, start1, size1, start2, size2);
            if (size1 + size2 < 1)
                return;
            auditionFifo_.finishedRead(1);
        }
    }

    void applyAuditionEvent(const AuditionEvent& event, juce::MidiBuffer& midiMessages) {
        const int pitch = juce::jlimit(0, 127, event.pitch);
        const int channel = juce::jlimit(1, 16, event.channel);

        // A second note-on for the same pitch (a drag retrigger the caller sequenced without a
        // release, or a lost note-off) releases the old one first: off-then-on is the order Poly
        // MIDI's same-pitch retrigger contract needs, and it keeps active_ from accumulating.
        releaseAuditionNote(pitch, channel, midiMessages);
        if (!event.on)
            return;

        if (!pushActive({pitch, channel, kAuditionEndBeat}))
            return; // overflow: drop the on rather than owe an off we cannot track
        midiMessages.addEvent(
            juce::MidiMessage::noteOn(channel, pitch, (juce::uint8)juce::jlimit(1, 127, event.velocity)), 0);
    }

    // Releases a held AUDITION note of this pitch/channel and nothing else. The isAuditionNote guard
    // is load-bearing: a clip sounding the same pitch must not be cut short because the user let go
    // of a note they clicked.
    void releaseAuditionNote(int pitch, int channel, juce::MidiBuffer& midiMessages) {
        for (int i = numActive_ - 1; i >= 0; --i) {
            const ActiveNote& note = active_[i];
            if (note.pitch != pitch || note.channel != channel || !isAuditionNote(note))
                continue;
            midiMessages.addEvent(juce::MidiMessage::noteOff(channel, pitch), 0);
            removeActiveAt(i);
            return;
        }
    }

    // Activity LED, same shape as ExternalMidiModule's: high while anything is sounding.
    void pushActivity(int numSamples) {
        if (auto* vb = getVisualBuffer()) {
            const float activity = numActive_ > 0 ? 1.0f : 0.0f;
            for (int i = 0; i < numSamples; ++i)
                vb->pushSample(activity);
        }
    }

    ActiveNote active_[kMaxActiveNotes];
    int numActive_ = 0;

    // Message thread -> audio thread, single producer / single consumer. Same shape as
    // TransportService's command FIFO: a fixed POD slot array plus a juce::AbstractFifo, so a push
    // is wait-free and the drain allocates nothing.
    juce::AbstractFifo auditionFifo_{kAuditionFifoCapacity};
    std::array<AuditionEvent, (std::size_t)kAuditionFifoCapacity> auditionSlots_{};

    std::int64_t expectedNextBlockStart_ = 0;
    bool haveLastBlock_ = false;
    bool wasBypassed_ = false;
    bool wasPlaying_ = false;
    bool wasSuppressed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineMidiSourceModule)
};
