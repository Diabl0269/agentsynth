#pragma once

#include "../Timeline/TimelineSnapshot.h"
#include "../Transport/TransportService.h"
#include "ModuleBase.h"
#include <array>
#include <atomic>
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
 * end-beat scan can never release it.
 *
 * WHAT RELEASES A PREVIEW, precisely — this list is the whole contract, and getting it wrong is how
 * a note sounds forever: its own note-off; a bypass or a lost transport (flushActiveNotes, which
 * takes auditions with it); or the panic path below. NOT a stop, a locate or a loop wrap — those go
 * through flushTimelineNotes, which deliberately SPARES auditions, because cutting a preview short
 * because the user pressed Stop would break the monitor path audition exists to provide.
 *
 * That sparing is also the trap: since a dropped note-OFF can never be cleaned up by a later
 * positional event, pushAuditionNote raises auditionPanic_ when the FIFO cannot take an off, and the
 * next block releases every preview. Without it, one dropped off leaves a note held for the life of
 * the process and Stop — the one thing a user reaches for — provably cannot clear it.
 *
 * Deliberately NOT gated on the track's mute/solo: audition is a monitor path, the same way
 * clicking a key on a MIDI Keyboard module is.
 *
 * INTERNAL-ONLY: not in the module library, not offered by the replace menu, and explicitly
 * non-authorable by the AI (kNonAuthorableModuleTypes) — a model could otherwise wire itself to a
 * track it did not create.
 *
 * CLIP PREVIEW is a third emitter beside the transport and audition: startClipPreview() loops one clip's notes while
 * the transport is STOPPED, on a beat clock of its own that advances at the transport's tempo. It reads the same
 * published snapshot through the same emitRange as playback, so per-pitch mute/solo and edits apply on the next
 * pass, and a muted or soloed-away track is silent. Its notes are ordinary timeline notes (finite end beat), so
 * flushTimelineNotes releases them: that is correct only because preview never runs with the transport playing.
 * The module ends the preview itself the block it sees the transport playing (flush, then the song plays alone);
 * stopClipPreview, bypass, no transport and a lost track release it too. The request crosses threads through a
 * seqlock (like TransportService's snapshot), the UI beat through an atomic.
 *
 * Held-note hygiene: a note-on emitted here is a promise to emit the matching note-off. Anything
 * that could break that promise (stop, locate, bypass, track deleted/muted/soloed away) flushes
 * every held note as note-offs at sample 0 first. Always PER-NOTE, never a blanket CC 123 — a MIDI
 * cable can carry several sources downstream.
 *
 * Loop wrap: a block crossing the loop end is TWO beat ranges (see BlockTimeInfo); every held note
 * is released exactly at the wrap, then the wrapped range starts fresh, so a note can legitimately
 * end and restart in the same block. A loop end landing exactly on the end of a block is one range
 * (the next block starts the new pass) but the release still happens, at the block's last sample:
 * a note ending at the loop end is otherwise never reached by the note-off scan, and the next
 * pass's note-on would stack on it. As a safety net the first block after any fold also releases a
 * held note whose end lies at or beyond the loop end. BlockTimeInfo reports only the FIRST wrap per block, so a
 * loop shorter than the block drops whole intermediate passes (never leaks a note — the
 * release-at-wrap still runs).
 *
 * Ordering: where a note-off and note-on land on the same sample offset, the off is always
 * inserted first (juce::MidiBuffer preserves insertion order at equal positions) — required by
 * Poly MIDI's same-pitch retrigger contract (docs/modules/modules.md#poly-note-contract-machine-midi).
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
        lastBlockFolded_ = false;
        wasBypassed_ = false;
        wasPlaying_ = false;
        wasSuppressed_ = false;
        dropClipPreview(); // a device restart cancels a running preview rather than resuming it
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
     *  not block. A dropped note-ON is silence, which is survivable. A dropped note-OFF is NOT: it
     *  used to be safe because every flush released audition notes, but the positional flushes now
     *  deliberately spare them, so nothing downstream would ever clean one up. That case raises
     *  auditionPanic_ instead, and the next block releases every preview — see the class comment. */
    bool pushAuditionNote(int pitch, int velocity, bool noteOn, int channel = 1) noexcept {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        auditionFifo_.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 + size2 < 1) {
            // A dropped note-ON is harmless: nothing sounded, so nothing is owed. A dropped
            // note-OFF strands a note FOREVER — the audition sentinel makes every hygiene flush
            // (stop, locate, loop wrap) skip it, so no later event can ever clean it up, and Stop
            // (the one thing a user reaches for when a note hangs) is specifically the thing that
            // cannot help. So an off that cannot be queued raises a PANIC instead, on an atomic
            // that has no capacity to overflow: the next block releases every held audition note.
            if (!noteOn)
                auditionPanic_.store(true, std::memory_order_release);
            return false;
        }
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

    /** CLIP PREVIEW: loops the notes in [clipStartBeat, clipEndBeat) of this track while the transport is stopped.
     *  Message thread only (single writer); a new call replaces the running one, and nothing sounds once the
     *  transport plays. */
    void startClipPreview(double clipStartBeat, double clipEndBeat) noexcept;
    /** Ends the preview; its held notes are released on the next block. Message thread only. */
    void stopClipPreview() noexcept;
    /** The absolute beat the preview is at, or NaN when none runs. Any thread. */
    double clipPreviewBeat() const noexcept { return previewBeat_.load(std::memory_order_relaxed); }

    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        // This module REPLACES the graph-supplied MIDI buffer (the ExternalMidiModule contract):
        // it is a source, so whatever the graph handed it is not ours to forward.
        midiMessages.clear();

        const int numSamples = buffer.getNumSamples();

        // Bypass: a pure MIDI source with no dry audio path, so the audio buffer is cleared on
        // both branches (see docs/architecture/module-base.md#bypassmute-contract). The FIRST bypassed
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
            dropClipPreview();
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
            dropClipPreview();
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
        // The pass counter (chance and conditions) counts loop wraps since play started or was located.
        if (!info.playing || !wasPlaying_ || discontinuous)
            transportPass_ = 0;

        // A fold that landed exactly at the end of the previous block, read before the bookkeeping
        // below moves on: this block opens a pass and must not still hold anything the old one left.
        // (A fold inside a block is not this: notes its wrapped range started are legitimately held.)
        const bool afterFold = haveLastBlock_ && !discontinuous && lastBlockFolded_;
        lastBlockFolded_ = info.playing && info.numSamples > 0 && info.loopWrapSample < 0 &&
                           nextBlockStart != info.blockStartSample + info.numSamples;

        wasPlaying_ = info.playing;
        haveLastBlock_ = info.playing;
        expectedNextBlockStart_ = nextBlockStart;

        // AUDITION, drained here — AFTER the stop/locate hygiene flush above (so a flush can never
        // land on top of a preview the same block started) and BEFORE every early return below, so a
        // click sounds with the transport STOPPED, on an unbound track, and on a muted one. That is
        // the point of a monitor path: the user asked to hear this note, not to hear the timeline.
        // PANIC first: a note-off the FIFO could not take (see pushAuditionNote) would otherwise
        // strand its note forever, because the hygiene flush above deliberately spares auditions.
        // Runs BEFORE the drain so anything queued after the panic still applies normally.
        if (auditionPanic_.exchange(false, std::memory_order_acq_rel))
            releaseAllAuditionNotes(midiMessages);

        drainAuditionEvents(midiMessages);
        runClipPreview(info, snapshot, numSamples, midiMessages);

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

        if (afterFold)
            releaseNotesPastLoopEnd(midiMessages, info.loopEndPpq);
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
                  /*baseOffset=*/0, beatsPerSample, lastSample, transportPass_);

        if (info.wrapsAtBlockEnd) {
            // The loop end is the end of this block: the primary range above already ran to it. Release
            // what is still held on the last sample, the nearest one the block has.
            flushTimelineNotes(midiMessages, lastSample);
            ++transportPass_;
            return;
        }

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
        ++transportPass_;
        emitRange(snapshot, track, midiMessages, info.loopStartPpq, beatFromSample(nextBlockStartSample, info),
                  wrapOffset, beatsPerSample, lastSample, transportPass_);
    }

    // Emits every note edge inside one beat range, at offsets measured from `baseOffset`. Notes are
    // sorted by startBeat within a track's run, so the scan could stop early; it does not, because a
    // note that STARTED before this range may still END inside it and the note-off scan has to see
    // it.
    void emitRange(const synth::TimelineSnapshot& snapshot, const synth::TimelineSnapshot::TrackInfo& track,
                   juce::MidiBuffer& midiMessages, double rangeStart, double rangeEnd, int baseOffset,
                   double beatsPerSample, int lastSample, std::int64_t pass) {
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

            // Chance and condition: a skipped hit pushes no active note, so it owes no note-off.
            if (!synth::TimelineSnapshot::notePlays(note, pass))
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

    // Safety net for the first block of a pass: whatever the previous pass left held with an end at or
    // beyond the loop end is released at sample 0 instead of stacking under the pass's own note-ons.
    void releaseNotesPastLoopEnd(juce::MidiBuffer& midiMessages, double loopEndPpq) {
        for (int i = numActive_ - 1; i >= 0; --i) {
            if (isAuditionNote(active_[i]) || !(active_[i].endBeat >= loopEndPpq))
                continue;
            midiMessages.addEvent(juce::MidiMessage::noteOff(active_[i].channel, active_[i].pitch), 0);
            removeActiveAt(i);
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
    //
    // Clears any pending PANIC too: both callers have just run flushActiveNotes, which already took
    // the audition notes with it, so a panic left armed here would fire a release for notes that no
    // longer exist the next time this node became usable.
    void discardAuditionEvents() {
        auditionPanic_.store(false, std::memory_order_release);
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

    // Every held AUDITION note released, timeline notes untouched — the mirror of
    // flushTimelineNotes. Reached only from the panic path (a note-off the FIFO could not take),
    // which is the one case where the preview's own off is never coming.
    void releaseAllAuditionNotes(juce::MidiBuffer& midiMessages, int offset = 0) {
        for (int i = numActive_ - 1; i >= 0; --i) {
            if (!isAuditionNote(active_[i]))
                continue;
            midiMessages.addEvent(juce::MidiMessage::noteOff(active_[i].channel, active_[i].pitch), offset);
            removeActiveAt(i);
        }
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

    // ---- Clip preview (see startClipPreview and the class comment); out of line in TimelineMidiSourceModulePreview
    // ----
    void runClipPreview(const synth::BlockTimeInfo& info, const synth::TimelineSnapshot* snapshot, int numSamples,
                        juce::MidiBuffer& midiMessages);
    bool syncPreviewRequest(juce::MidiBuffer& midiMessages);
    void emitPreviewBlock(const synth::TimelineSnapshot& snapshot, const synth::TimelineSnapshot::TrackInfo& track,
                          double beatsPerSample, int numSamples, juce::MidiBuffer& midiMessages);
    void endClipPreview(juce::MidiBuffer& midiMessages);
    void dropClipPreview() noexcept;

    // Activity LED, same shape as ExternalMidiModule's: high while anything is sounding.
    void pushActivity(int numSamples) {
        if (auto* vb = getVisualBuffer()) {
            const float activity = numActive_ > 0 ? 1.0f : 0.0f;
            vb->pushConstant(activity, numSamples);
        }
    }

    ActiveNote active_[kMaxActiveNotes];
    int numActive_ = 0;

    // Message thread -> audio thread, single producer / single consumer. Same shape as
    // TransportService's command FIFO: a fixed POD slot array plus a juce::AbstractFifo, so a push
    // is wait-free and the drain allocates nothing.
    juce::AbstractFifo auditionFifo_{kAuditionFifoCapacity};
    std::array<AuditionEvent, (std::size_t)kAuditionFifoCapacity> auditionSlots_{};

    // "An audition note-off was dropped — release every preview." A bool, not a queued event, for
    // the reason it exists: it must be impossible to lose, and an atomic cannot be full.
    std::atomic<bool> auditionPanic_{false};

    // Clip preview. The request is written by the message thread inside an odd/even version pair and read by the
    // audio thread; everything after it (running, position, range) belongs to the audio thread alone.
    std::atomic<std::uint32_t> previewVersion_{0};
    std::atomic<bool> previewWanted_{false};
    std::atomic<double> previewStart_{0.0};
    std::atomic<double> previewEnd_{0.0};
    std::atomic<double> previewBeat_{std::numeric_limits<double>::quiet_NaN()};
    std::uint32_t previewSeen_ = 0;
    bool previewRunning_ = false;
    bool previewSuppressed_ = false;
    double previewLo_ = 0.0;
    double previewHi_ = 0.0;
    double previewPos_ = 0.0;
    std::int64_t previewPass_ = 0;   // completed clip wraps since the preview started
    std::int64_t transportPass_ = 0; // loop wraps since play started or was located

    std::int64_t expectedNextBlockStart_ = 0;
    bool haveLastBlock_ = false;
    bool lastBlockFolded_ = false; // the previous block ended exactly on the loop end
    bool wasBypassed_ = false;
    bool wasPlaying_ = false;
    bool wasSuppressed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineMidiSourceModule)
};

// ---- Clip preview ------------------------------------------------------------------------------------------------

inline void TimelineMidiSourceModule::startClipPreview(double clipStartBeat, double clipEndBeat) noexcept {
    if (!std::isfinite(clipStartBeat) || !std::isfinite(clipEndBeat) || !(clipEndBeat > clipStartBeat))
        return stopClipPreview();
    previewVersion_.fetch_add(1, std::memory_order_acq_rel); // odd: write in flight
    previewStart_.store(clipStartBeat, std::memory_order_relaxed);
    previewEnd_.store(clipEndBeat, std::memory_order_relaxed);
    previewWanted_.store(true, std::memory_order_relaxed);
    previewVersion_.fetch_add(1, std::memory_order_release); // even: request complete
    previewBeat_.store(clipStartBeat, std::memory_order_relaxed);
}

inline void TimelineMidiSourceModule::stopClipPreview() noexcept {
    previewVersion_.fetch_add(1, std::memory_order_acq_rel);
    previewWanted_.store(false, std::memory_order_relaxed);
    previewVersion_.fetch_add(1, std::memory_order_release);
    previewBeat_.store(std::numeric_limits<double>::quiet_NaN(), std::memory_order_relaxed);
}

// Forgets the preview and any request already seen, so it cannot restart on its own; the caller flushes the notes.
inline void TimelineMidiSourceModule::dropClipPreview() noexcept {
    previewRunning_ = false;
    previewSuppressed_ = false;
    previewSeen_ = previewVersion_.load(std::memory_order_acquire);
    previewBeat_.store(std::numeric_limits<double>::quiet_NaN(), std::memory_order_relaxed);
}

// Ends a running preview: its notes are released at sample 0 and the UI beat goes NaN. The request stays "seen", so
// nothing restarts until the UI asks again.
inline void TimelineMidiSourceModule::endClipPreview(juce::MidiBuffer& midiMessages) {
    flushTimelineNotes(midiMessages);
    dropClipPreview();
}

// Takes up a new request, if one landed whole since the last block. Returns whether a preview is running afterwards.
// A request caught mid-write (odd version) or rewritten under the read is left for the next block.
inline bool TimelineMidiSourceModule::syncPreviewRequest(juce::MidiBuffer& midiMessages) {
    const std::uint32_t before = previewVersion_.load(std::memory_order_acquire);
    if (before != previewSeen_ && (before & 1u) == 0u) {
        const bool wanted = previewWanted_.load(std::memory_order_relaxed);
        const double lo = previewStart_.load(std::memory_order_relaxed);
        const double hi = previewEnd_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (previewVersion_.load(std::memory_order_relaxed) == before) {
            previewSeen_ = before;
            if (previewRunning_)
                flushTimelineNotes(midiMessages); // a restart or a stop: nothing of the old pass may hang on
            previewRunning_ = wanted && hi > lo;
            previewSuppressed_ = false;
            previewLo_ = lo;
            previewHi_ = hi;
            previewPos_ = lo;
            previewPass_ = 0;
            if (!previewRunning_)
                previewBeat_.store(std::numeric_limits<double>::quiet_NaN(), std::memory_order_relaxed);
        }
    }
    return previewRunning_;
}

inline void TimelineMidiSourceModule::runClipPreview(const synth::BlockTimeInfo& info,
                                                     const synth::TimelineSnapshot* snapshot, int numSamples,
                                                     juce::MidiBuffer& midiMessages) {
    if (!syncPreviewRequest(midiMessages))
        return;
    if (info.playing) { // the song takes over: it must start from a clean slate
        endClipPreview(midiMessages);
        return;
    }
    if (snapshot == nullptr || numSamples <= 0 || !(info.beatsPerSample() > 0.0))
        return; // nothing to read yet: hold the position
    const auto* track = findMyTrack(*snapshot);
    if (track == nullptr) { // the track went away: there is nothing left to preview
        endClipPreview(midiMessages);
        return;
    }
    if (track->muted || (snapshot->anySoloed && !track->soloed)) {
        if (!previewSuppressed_)
            flushTimelineNotes(midiMessages);
        previewSuppressed_ = true;
    } else {
        previewSuppressed_ = false;
    }
    emitPreviewBlock(*snapshot, *track, info.beatsPerSample(), numSamples, midiMessages);
}

// One block of the loop: a single beat range, or two when it crosses the clip end (released at the wrap exactly as the
// transport's loop wrap does). The clock advances even when the track is silenced, so unmuting rejoins the loop.
inline void TimelineMidiSourceModule::emitPreviewBlock(const synth::TimelineSnapshot& snapshot,
                                                       const synth::TimelineSnapshot::TrackInfo& track,
                                                       double beatsPerSample, int numSamples,
                                                       juce::MidiBuffer& midiMessages) {
    const int lastSample = juce::jmax(0, numSamples - 1);
    const double length = previewHi_ - previewLo_;
    if (!(previewPos_ >= previewLo_ && previewPos_ < previewHi_)) {
        flushTimelineNotes(midiMessages); // the previous block ended exactly on the clip end
        previewPos_ = previewLo_;
        ++previewPass_;
    }
    const double end = previewPos_ + static_cast<double>(numSamples) * beatsPerSample;
    const auto emit = [&](double from, double to, int baseOffset) {
        if (!previewSuppressed_)
            emitRange(snapshot, track, midiMessages, from, to, baseOffset, beatsPerSample, lastSample, previewPass_);
    };
    if (end <= previewHi_) {
        emit(previewPos_, end, 0);
        previewPos_ = end;
    } else {
        emit(previewPos_, previewHi_, 0);
        const int wrapOffset =
            juce::jlimit(0, lastSample, static_cast<int>(std::llround((previewHi_ - previewPos_) / beatsPerSample)));
        flushTimelineNotes(midiMessages, wrapOffset);
        const double into = std::fmod(end - previewHi_, length); // a clip shorter than the block folds, as a loop does
        previewPass_ += 1 + static_cast<std::int64_t>(std::floor((end - previewHi_) / length));
        emit(previewLo_, previewLo_ + into, wrapOffset);
        previewPos_ = previewLo_ + into;
    }
    previewBeat_.store(previewPos_, std::memory_order_relaxed);
}
