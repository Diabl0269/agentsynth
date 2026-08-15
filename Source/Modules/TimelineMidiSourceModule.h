#pragma once

#include "../Timeline/TimelineSnapshot.h"
#include "../Transport/TransportService.h"
#include "ModuleBase.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief "Track In" — the graph-side end of a timeline MIDI track (TL3-1).
 *
 * One node per MIDI track. The timeline's add-track flow creates it and binds the track to it by
 * the node's uuid; from then on the module turns that track's notes into MIDI events, and the
 * patch downstream of it (Poly MIDI -> oscillators -> FX) is an ordinary patch that neither knows
 * nor cares that a timeline exists.
 *
 * -- Pull, not push ---------------------------------------------------------------------------
 *
 * Nothing schedules events into this module. Every block it reads the transport's BlockTimeInfo
 * and the block's TimelineSnapshot — both off the playhead AudioEngine installs on every node
 * (see TransportService::setCurrentTimelineSnapshot) — finds the track whose bindingUuid equals
 * its own node uuid, and emits the notes whose start or end falls inside this block's beat range.
 * There is no per-module state to keep in sync with the document, so a locate, a tempo change or
 * an edit to the notes takes effect on the very next block with no invalidation step: the module
 * is a pure function of (block position, snapshot) plus the set of notes it currently holds down.
 *
 * The module is INTERNAL-ONLY: it is not in the module library, not offered by the replace menu,
 * and explicitly non-authorable by the AI (kNonAuthorableModuleTypes / validatePatch's untrusted
 * path reject it), because a model could otherwise wire itself to a track it did not create.
 *
 * -- Held-note hygiene ------------------------------------------------------------------------
 *
 * A note-on this module emits is a promise to emit the matching note-off. Anything that could
 * break that promise flushes every held note — as note-offs at sample 0 of the block where it
 * happened — before doing anything else: the transport stopping, a locate (seen as a block whose
 * start sample is not where the last block ended), the module being bypassed, the bound track
 * disappearing or being muted/soloed away. Without that, one stop mid-chord leaves an envelope
 * open forever.
 *
 * The release is always PER-NOTE, never a blanket all-notes-off (CC 123). One MIDI cable can carry
 * several sources downstream, and a CC 123 would silence notes this module never started. The
 * module emits nothing but note-ons and note-offs, ever.
 *
 * -- Loop wrap (TL3-2) -------------------------------------------------------------------------
 *
 * A block that crosses the loop end is TWO beat ranges, not one (see BlockTimeInfo): the primary
 * range [startPpq, loopEndPpq) at offsets [0, loopWrapSample), then the wrapped range starting at
 * loopStartPpq at offsets from loopWrapSample on. Between them, at exactly loopWrapSample, every
 * note still held is released: crossing the boundary ends this pass, and the wrapped range then
 * starts the next one fresh. So one block can legitimately end a note and start the same note
 * again — each loop pass re-articulates, which is what a looping sequencer sounds like.
 *
 * The wrapped range ends where the transport ACTUALLY lands (predictNextBlockStart, which mirrors
 * TransportService::tick), not at some derived beat — that is what makes the emitted ranges tile
 * the timeline exactly, with no beat emitted twice and none skipped, across the wrap.
 *
 * MULTI-WRAP BOUND: BlockTimeInfo reports only the FIRST wrap in a block. If the loop is shorter
 * than the block (possible — the minimum loop is TransportService::kMinLoopLengthBeats = 1/16 beat,
 * 1500 samples at 48 kHz / 120 BPM, against a block that may be 2048 or more), the repetitions
 * BETWEEN the first wrap and the block's end are not emitted: this module plays the primary range
 * and the final partial pass, and drops the whole passes in the middle. What it does NOT do is
 * leak a note: the release-at-wrap above runs regardless, so a pathologically short loop degrades
 * to "some repeats are missing", never to "an envelope is stuck open".
 *
 * -- Ordering: offs before ons -----------------------------------------------------------------
 *
 * Where a note-off and a note-on land on the SAME sample offset, the off is always inserted first,
 * because juce::MidiBuffer keeps insertion order among events sharing a sample position. Three
 * places enforce it: the hygiene flushes run before any emission in the block; each range emits
 * the note-offs of already-held notes in a pass of their own before any note-on; and the wrap
 * release runs before the wrapped range's note-ons. Within one range, notes are visited in
 * start-beat order, so a note's own end-of-block off is inserted before any later-starting note's
 * on at that offset. Downstream this is what Poly MIDI's same-pitch retrigger contract needs (see
 * docs/modules.md): a release, then the retrigger, never the other way round.
 */
class TimelineMidiSourceModule : public ModuleBase {
public:
    // Held notes are tracked in a fixed array — the audio thread must not allocate. 128 is one of
    // every pitch; a track holding more simultaneously is pathological, and the overflow policy is
    // to DROP the note-on (never the note-off of something already sounding, and never a resize).
    static constexpr int kMaxActiveNotes = 128;

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
            // so release anything held and go quiet.
            flushActiveNotes(midiMessages);
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
            flushActiveNotes(midiMessages);

        wasPlaying_ = info.playing;
        haveLastBlock_ = info.playing;
        expectedNextBlockStart_ = nextBlockStart;

        if (!info.playing || snapshot == nullptr || numSamples <= 0) {
            pushActivity(numSamples);
            return;
        }

        const synth::TimelineSnapshot::TrackInfo* track = findMyTrack(*snapshot);
        if (track == nullptr) {
            // The track was deleted, unbound or re-bound elsewhere while we held notes down.
            flushActiveNotes(midiMessages);
            wasSuppressed_ = false;
            pushActivity(numSamples);
            return;
        }

        const bool suppressed = track->muted || (snapshot->anySoloed && !track->soloed);
        if (suppressed) {
            if (!wasSuppressed_)
                flushActiveNotes(midiMessages);
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
        flushActiveNotes(midiMessages, wrapOffset);

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
    // offset is 0 for the hygiene flushes (stop, locate, bypass, track lost) and loopWrapSample for
    // the loop-boundary release.
    void flushActiveNotes(juce::MidiBuffer& midiMessages, int offset = 0) {
        for (int i = 0; i < numActive_; ++i)
            midiMessages.addEvent(juce::MidiMessage::noteOff(active_[i].channel, active_[i].pitch), offset);
        numActive_ = 0;
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

    std::int64_t expectedNextBlockStart_ = 0;
    bool haveLastBlock_ = false;
    bool wasBypassed_ = false;
    bool wasPlaying_ = false;
    bool wasSuppressed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineMidiSourceModule)
};
