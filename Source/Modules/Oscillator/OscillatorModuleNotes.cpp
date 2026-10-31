// OscillatorModuleNotes.cpp -- MIDI note tracking and the phase restart on a new note.
// The class and channel map: OscillatorModule.h; the rendering loops that apply a restart:
// OscillatorModuleMono.cpp, OscillatorModulePoly.cpp.

#include "Modules/OscillatorModule.h"

// A note-on restarts the voice, except that in mono with Legato on an overlapping note-on carries the
// phase on. This is the one place that decision is made.
bool OscillatorModule::shouldRestartOnNoteOn(bool overlapping) const {
    return !(overlapping && legatoParam->get() && !polyParam->get());
}

namespace {
// Whether a held note other than `note` is still down once the note-offs that share sample `pos` with the
// note-on at `at` are applied. A host may write the note-off after the note-on of the same sample; two notes
// that touch must not read as overlapping whichever way round they arrive.
bool overlapsAfterSameSampleReleases(const synth::HeldNotes& held, int note, juce::MidiBufferIterator at,
                                     juce::MidiBufferIterator end, int pos) {
    int released[synth::HeldNotes::kCapacity];
    int count = 0;
    for (++at; at != end && (*at).samplePosition == pos && count < synth::HeldNotes::kCapacity; ++at) {
        const auto msg = (*at).getMessage();
        if (msg.isNoteOff())
            released[count++] = msg.getNoteNumber();
    }
    return held.anyOtherThanIf(
        note, [&](int heldNote) { return std::find(released, released + count, heldNote) == released + count; });
}
} // namespace

// The held-note stack follows every note-on/off even while the module is muted or bypassed, so a
// note released in that time is not left held. The pitch and the restart offsets are only taken
// while audible, matching the old behaviour of ignoring MIDI when muted. Offsets are clamped into
// the block, de-duplicated (a chord shares one restart) and capped at kMaxRestartsPerBlock; past
// the cap further note-ons of the block share the last restart. Each mono pitch change is also
// recorded with its own sample (`pitchEvents`), so the pitch moves where the note starts, not at the
// start of the block it lands in.
void OscillatorModule::scanMidi(const juce::MidiBuffer& midi, int numSamples, bool audible) {
    numPitchEvents = 0;
    noteAtBlockStart = voices[0].lastMidiNote;
    const bool monoLegato = legatoParam->get() && !polyParam->get();
    for (auto it = midi.begin(); it != midi.end(); ++it) {
        const auto metadata = *it;
        const auto msg = metadata.getMessage();
        const int offset = juce::jlimit(0, std::max(0, numSamples - 1), metadata.samplePosition);
        if (msg.isNoteOn()) {
            const int note = msg.getNoteNumber();
            const bool overlapping =
                overlapsAfterSameSampleReleases(heldNotes, note, it, midi.end(), metadata.samplePosition);
            heldNotes.press(note);
            if (!audible)
                continue;
            voices[0].lastMidiNote = (float)note;
            addPitchEvent(offset, (float)note, monoLegato && !overlapping);
            if (!shouldRestartOnNoteOn(overlapping))
                continue;
            voice0RestartPending = true;
            addRestartOffset(offset);
        } else if (msg.isNoteOff()) {
            heldNotes.release(msg.getNoteNumber());
            // Mono legato: letting go of the sounding note while others are held returns to the newest one.
            if (audible && monoLegato && !heldNotes.empty() && msg.getNoteNumber() == (int)voices[0].lastMidiNote) {
                voices[0].lastMidiNote = (float)heldNotes.newest();
                addPitchEvent(offset, voices[0].lastMidiNote, false);
            }
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            heldNotes.clear();
        }
    }
}

void OscillatorModule::addPitchEvent(int offset, float note, bool jumps) {
    auto& slot = pitchEvents[(size_t)std::min(numPitchEvents, kMaxPitchEvents - 1)];
    slot = {offset, note, jumps};
    numPitchEvents = std::min(numPitchEvents + 1, kMaxPitchEvents);
}

// Both the MIDI note-ons and the mono Gate edges land here, so the per-sample restart loop sees one
// ascending list. A duplicate offset (a chord, or a note-on on the same sample as a gate edge) shares
// one restart; past the cap further offsets share the last restart.
void OscillatorModule::addRestartOffset(int offset, float lead) {
    int at = numRestarts;
    while (at > 0 && restartOffsets[(size_t)at - 1] > offset)
        --at;
    if (at > 0 && restartOffsets[(size_t)at - 1] == offset)
        return;
    if (numRestarts >= kMaxRestartsPerBlock)
        return;
    for (int i = numRestarts; i > at; --i) {
        restartOffsets[(size_t)i] = restartOffsets[(size_t)i - 1];
        restartLeads[(size_t)i] = restartLeads[(size_t)i - 1];
    }
    restartOffsets[(size_t)at] = offset;
    restartLeads[(size_t)at] = lead;
    ++numRestarts;
}

// Every unison oscillator restarts at frac(Phase / 360 + Rand * random), the random drawn
// independently per voice from the module's seeded generator so a render repeats exactly. The old
// phases keep running as a "ghost" and are blended out over CROSSFADE_SAMPLES, because a restart
// while the previous note may still sound would otherwise jump the waveform. The ghost starts with
// the share the interrupted stack holds at that sample (all of it when nothing was fading, less when
// ghosts are still leaving), and every ghost fades along its own curve to nothing, so a restart during
// a fade is continuous too and an audio-rate gate cannot step the output. A ghost keeps the pitch the
// interrupted note was playing at, so a new note's pitch never reaches the tail it fades out. Past RestartGhosts'
// capacity (restarts closer than a few samples) the quietest ghost is dropped and its share goes to the current stack.
//
// A restart after a run of short gaps is oscillator sync, not a new note (a short gap or two is a note whose MIDI and
// gate edge land close together, or a quick pair of notes, which still take the full hand-over). Its wave has to repeat
// exactly once per master cycle or the ear hears noise, so three things change: the unison voices reuse the random
// draws of the note instead of drawing new ones (a fresh random phase per cycle is white noise at any Rand above 0);
// the phase and the hand-over are placed between samples at the gate's real crossing (a master period that is not a
// whole number of samples would otherwise land unevenly, which is grit); and the hand-over shortens, because a
// 64-sample fade covers most of a fast master's cycle and averages the wave down to a quiet, dull remnant instead of a
// sync sound.
void OscillatorModule::restartVoice(VoiceState& voice, bool fadeFromOld, float lead) {
    const float startPhase = phaseParam->get() / 360.0f;
    const float randomAmount = randomPhaseParam->get() * 0.01f;
    const bool syncing = voice.sync.noteRestart(syncEdgeSamples, fadeFromOld, lead);

    if (!fadeFromOld) {
        voice.ghosts.count = 0;
    } else {
        UnisonStack current;
        std::copy(std::begin(voice.unisonOscs), std::end(voice.unisonOscs), std::begin(current.oscs));
        current.baseDt = voice.lastBaseDt;
        voice.ghosts.start(current, syncing ? synth::kSyncFadeSamples : CROSSFADE_SAMPLES, syncing ? lead : 0.0f);
    }
    for (int u = 0; u < MAX_UNISON; ++u) {
        if (!syncing && randomAmount > 0.0f)
            voice.restartDraws[u] = phaseRandom.nextFloat();
        voice.unisonOscs[u].phase = synth::unisonRestartPhase(startPhase, randomAmount, voice.restartDraws[u]);
    }
}

void OscillatorModule::resetNoteState() {
    heldNotes.clear();
    for (auto& v : voices)
        v.gateEdges.reset();
    clearPendingRestarts();
    phaseRandom.setSeed(0x5EED1234);
}

void OscillatorModule::clearPendingRestarts() {
    numRestarts = 0;
    numPitchEvents = 0;
    voice0RestartPending = false;
}
