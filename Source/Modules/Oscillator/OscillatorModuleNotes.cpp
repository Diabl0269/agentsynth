// OscillatorModuleNotes.cpp -- MIDI note tracking and the phase restart on a new note.
// The class and channel map: OscillatorModule.h; the rendering loops that apply a restart:
// OscillatorModuleMono.cpp, OscillatorModulePoly.cpp.

#include "Modules/OscillatorModule.h"

// A note-on restarts the voice, except that in mono with Legato on an overlapping note-on carries the
// phase on. This is the one place that decision is made.
bool OscillatorModule::shouldRestartOnNoteOn(bool overlapping) const {
    return !(overlapping && legatoParam->get() && !polyParam->get());
}

// The held-note stack follows every note-on/off even while the module is muted or bypassed, so a
// note released in that time is not left held. The pitch and the restart offsets are only taken
// while audible, matching the old behaviour of ignoring MIDI when muted. Offsets are clamped into
// the block, de-duplicated (a chord shares one restart) and capped at kMaxRestartsPerBlock; past the
// cap further note-ons of the block share the last restart.
void OscillatorModule::scanMidi(const juce::MidiBuffer& midi, int numSamples, bool audible) {
    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            const int note = msg.getNoteNumber();
            const bool overlapping = heldNotes.anyOtherThan(note);
            heldNotes.press(note);
            if (!audible)
                continue;
            voices[0].lastMidiNote = (float)note;
            noteJumps = legatoParam->get() && !polyParam->get() && !overlapping;
            if (!shouldRestartOnNoteOn(overlapping))
                continue;
            voice0RestartPending = true;
            addRestartOffset(juce::jlimit(0, std::max(0, numSamples - 1), metadata.samplePosition));
        } else if (msg.isNoteOff()) {
            heldNotes.release(msg.getNoteNumber());
            // Mono legato: letting go of the sounding note while others are held returns to the newest one.
            if (audible && legatoParam->get() && !polyParam->get() && !heldNotes.empty() &&
                msg.getNoteNumber() == (int)voices[0].lastMidiNote) {
                voices[0].lastMidiNote = (float)heldNotes.newest();
                noteJumps = false;
            }
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            heldNotes.clear();
        }
    }
}

// Both the MIDI note-ons and the mono Gate edges land here, so the per-sample restart loop sees one
// ascending list. A duplicate offset (a chord, or a note-on on the same sample as a gate edge) shares
// one restart; past the cap further offsets share the last restart.
void OscillatorModule::addRestartOffset(int offset) {
    int at = numRestarts;
    while (at > 0 && restartOffsets[(size_t)at - 1] > offset)
        --at;
    if (at > 0 && restartOffsets[(size_t)at - 1] == offset)
        return;
    if (numRestarts >= kMaxRestartsPerBlock)
        return;
    for (int i = numRestarts; i > at; --i)
        restartOffsets[(size_t)i] = restartOffsets[(size_t)i - 1];
    restartOffsets[(size_t)at] = offset;
    ++numRestarts;
}

// Every unison oscillator restarts at frac(Phase / 360 + Rand * random), the random drawn
// independently per voice from the module's seeded generator so a render repeats exactly. The old
// phases keep running in the fade copy and are blended out over CROSSFADE_SAMPLES, because a restart
// while the previous note may still sound would otherwise jump the waveform.
void OscillatorModule::restartVoice(VoiceState& voice, bool fadeFromOld) {
    const float startPhase = phaseParam->get() / 360.0f;
    const float randomAmount = randomPhaseParam->get() * 0.01f;

    std::copy(std::begin(voice.unisonOscs), std::end(voice.unisonOscs), std::begin(voice.fadeOscs));
    voice.restartFadeRemaining = fadeFromOld ? CROSSFADE_SAMPLES : 0;
    for (auto& osc : voice.unisonOscs)
        osc.phase = synth::unisonRestartPhase(startPhase, randomAmount, phaseRandom);
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
    noteJumps = false;
    voice0RestartPending = false;
}
