// OscillatorModuleNotes.cpp -- MIDI note tracking and the phase restart on a new note.
// The class and channel map: OscillatorModule.h; the rendering loops that apply a restart:
// OscillatorModuleMono.cpp, OscillatorModulePoly.cpp.

#include "Modules/OscillatorModule.h"

// A note-on restarts the voice by default, overlapping or not. This is the one place a legato or
// mono-retrigger mode decides that an overlapping note-on carries the phase on instead.
bool OscillatorModule::shouldRestartOnNoteOn(bool overlapping) const {
    juce::ignoreUnused(overlapping);
    return true;
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
            if (!shouldRestartOnNoteOn(overlapping))
                continue;
            voice0RestartPending = true;
            const int offset = juce::jlimit(0, std::max(0, numSamples - 1), metadata.samplePosition);
            if (numRestarts > 0 && restartOffsets[(size_t)numRestarts - 1] == offset)
                continue;
            if (numRestarts < kMaxRestartsPerBlock)
                restartOffsets[(size_t)numRestarts++] = offset;
        } else if (msg.isNoteOff()) {
            heldNotes.release(msg.getNoteNumber());
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            heldNotes.clear();
        }
    }
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
    clearPendingRestarts();
    phaseRandom.setSeed(0x5EED1234);
}

void OscillatorModule::clearPendingRestarts() {
    numRestarts = 0;
    voice0RestartPending = false;
}
