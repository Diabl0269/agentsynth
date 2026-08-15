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
 * TODO(TL3-2): loop wrap. This increment emits the block's PRIMARY beat range only — a block that
 * wraps stops at loopEndPpq and the post-wrap remainder is dropped (the wrap itself already
 * flushes held notes via the discontinuity check, so nothing hangs). TL3-2 adds the second range,
 * plus boundary fuzz for notes landing exactly on a block edge.
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

        // A stop, or a jump in the block's start sample (a locate, a loop wrap, a host that
        // repositions us), means the position we emitted against last block does not continue into
        // this one. Release everything first, then carry on against the new position.
        //
        // Continuity is only tracked WHILE PLAYING: a stopped transport republishes the same start
        // sample every block, which would otherwise read as a fresh discontinuity every callback.
        const bool discontinuous = haveLastBlock_ && info.blockStartSample != expectedNextBlockStart_;
        if ((!info.playing && wasPlaying_) || (info.playing && discontinuous))
            flushActiveNotes(midiMessages);

        wasPlaying_ = info.playing;
        haveLastBlock_ = info.playing;
        expectedNextBlockStart_ = info.blockStartSample + info.numSamples;

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

        emitRange(*snapshot, *track, info, midiMessages);
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

    // Emits every note edge inside this block's primary beat range. Notes are sorted by startBeat
    // within a track's run, so the scan could stop early; it does not, because a note that STARTED
    // before this block may still END inside it and the note-off scan has to see it.
    void emitRange(const synth::TimelineSnapshot& snapshot, const synth::TimelineSnapshot::TrackInfo& track,
                   const synth::BlockTimeInfo& info, juce::MidiBuffer& midiMessages) {
        const double startPpq = info.startPpq;
        // TODO(TL3-2): the wrapped remainder [loopStartPpq, ...) is not emitted yet.
        const double endPpq = (info.loopWrapSample >= 0) ? info.loopEndPpq : info.endPpq;
        if (!(endPpq > startPpq))
            return;

        const double beatsPerSample = info.beatsPerSample();
        if (!(beatsPerSample > 0.0))
            return;

        const int lastSample = juce::jmax(0, info.numSamples - 1);

        // Note-offs first at each position: a re-articulation of the same pitch inside one block
        // must land off-then-on, and MidiBuffer keeps insertion order for equal sample positions
        // only within one addEvent sequence — so the two passes are separated deliberately.
        for (int i = numActive_ - 1; i >= 0; --i) {
            const ActiveNote& note = active_[i];
            if (note.endBeat < endPpq) {
                // An end beat BEFORE this block's start can only come of a tempo change moving the
                // grid under a held note; the clamp inside beatOffsetToSample releases it at 0.
                const int offset = beatOffsetToSample(note.endBeat, startPpq, beatsPerSample, lastSample);
                midiMessages.addEvent(juce::MidiMessage::noteOff(note.channel, note.pitch), offset);
                removeActiveAt(i);
            }
        }

        const int firstNote = track.firstNote;
        const int lastNote = track.firstNote + track.numNotes;
        for (int i = firstNote; i < lastNote; ++i) {
            const auto& note = snapshot.notes[static_cast<std::size_t>(i)];
            if (note.startBeat < startPpq)
                continue;
            if (note.startBeat >= endPpq)
                break; // sorted by startBeat: everything after this one is later still

            // A zero- or negative-length note would emit an on with no matching off in range.
            if (!(note.endBeat > note.startBeat))
                continue;

            const int offset = beatOffsetToSample(note.startBeat, startPpq, beatsPerSample, lastSample);
            const int channel = juce::jlimit(1, 16, note.channel);
            const int pitch = juce::jlimit(0, 127, note.pitch);
            const int velocity = juce::jlimit(1, 127, note.velocity);

            if (!pushActive({pitch, channel, note.endBeat}))
                continue; // overflow: drop the note-on so we never owe an off we can't track

            midiMessages.addEvent(juce::MidiMessage::noteOn(channel, pitch, (juce::uint8)velocity), offset);

            // A note whose end also falls inside this block closes here. Emitted in this pass (not
            // the one above, which ran before this note existed) and therefore after its own
            // note-on, which is the order MidiBuffer preserves for equal sample positions.
            if (note.endBeat < endPpq) {
                const int offOffset = beatOffsetToSample(note.endBeat, startPpq, beatsPerSample, lastSample);
                midiMessages.addEvent(juce::MidiMessage::noteOff(channel, pitch), offOffset);
                removeActiveAt(numActive_ - 1);
            }
        }
    }

    static int beatOffsetToSample(double beat, double startPpq, double beatsPerSample, int lastSample) noexcept {
        return juce::jlimit(0, lastSample, (int)std::llround((beat - startPpq) / beatsPerSample));
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

    // Every held note released at sample 0, then forgotten. The one place the note-on/note-off
    // promise is honoured when the normal end-beat path can't run.
    void flushActiveNotes(juce::MidiBuffer& midiMessages) {
        for (int i = 0; i < numActive_; ++i)
            midiMessages.addEvent(juce::MidiMessage::noteOff(active_[i].channel, active_[i].pitch), 0);
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
