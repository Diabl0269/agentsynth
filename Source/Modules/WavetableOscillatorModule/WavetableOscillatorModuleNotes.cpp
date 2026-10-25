// WavetableOscillatorModuleNotes.cpp -- note starts: the MIDI note-ons and Gate edges that restart a voice, and the
// hand-over from the old sound. The class and channel map: WavetableOscillatorModule.h; the loop that applies a
// restart on its own sample: WavetableOscillatorModuleRender.cpp.

#include "WavetableOscillatorModule.h"

// Records the sample offset of a MIDI note-on for voice 0, clamped into the block. A chord shares one restart;
// past the cap further note-ons of the block share the last one.
void WavetableOscillatorModule::VoiceState::addMidiRestart(int offset) {
    if (numMidiRestarts > 0 && midiRestarts[(size_t)numMidiRestarts - 1] == offset)
        return;
    if (numMidiRestarts >= kMaxMidiRestarts)
        return;
    midiRestarts[(size_t)numMidiRestarts++] = offset;
}

// True once per restart on sample `s`: a Gate edge or a MIDI note-on, both consumed so that two on one sample are
// one restart. Call with ascending `s` within a block.
bool WavetableOscillatorModule::VoiceState::restartFiresAt(int s) {
    bool fires = gateEdges.firesAt(s);
    while (nextMidiRestart < numMidiRestarts && midiRestarts[(size_t)nextMidiRestart] <= s) {
        fires = fires || midiRestarts[(size_t)nextMidiRestart] == s;
        ++nextMidiRestart;
    }
    return fires;
}

// Restarts at frac(Phase / 360 + Spread * voice share + Rand * random), the random drawn per voice from the
// module's seeded generator so a render repeats exactly. Phase, Rand and Spread are sampled here, at the note-on
// instant (their CV at the block's first sample): they shape the attack, not the sustain. With `fadeFromOld` the
// stack as it stood runs on as a ghost and is blended out over kRestartFadeSamples, because a restart while the
// previous note still sounds would otherwise jump the waveform; a restart inside a hand-over keeps the ghosts
// that are still leaving, so even an audio-rate gate cannot step the output.
void WavetableOscillatorModule::restartVoice(VoiceState& voice, int unisonCount, bool fadeFromOld) {
    if (fadeFromOld)
        voice.ghosts.start(voice.stack);
    else
        voice.ghosts.count = 0;
    const float startPhase = juce::jlimit(0.0f, 1.0f, phaseParam->get() / 360.0f + cvAt(kJackPhase, 0));
    const float spread = juce::jlimit(0.0f, 1.0f, spreadParam->get() + cvAt(kJackSpread, 0));
    const float randomAmount = juce::jlimit(0.0f, 1.0f, randomPhaseParam->get() + cvAt(kJackRand, 0));
    voice.stack.resetPhases(startPhase, spread, randomAmount, unisonCount, phaseRandom);
}

// Notes arrive as MIDI in the fallback pitch of voice 0 (mono, and poly without a pitch CV) and as Gate edges
// on every voice. Only the pitch is taken at the block start; the restart waits for its sample.
void WavetableOscillatorModule::scanMidi(const juce::MidiBuffer& midi, int numSamples) {
    voices[0].numMidiRestarts = 0;
    voices[0].nextMidiRestart = 0;
    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            voices[0].lastMidiNote = (float)msg.getNoteNumber();
            voices[0].addMidiRestart(juce::jlimit(0, std::max(0, numSamples - 1), metadata.samplePosition));
        }
    }
}

void WavetableOscillatorModule::scanGateEdges(const juce::AudioBuffer<float>& buffer, bool poly, int numSamples) {
    const int numCh = buffer.getNumChannels();
    for (int v = 0; v < MAX_VOICES; ++v) {
        const int ch = poly ? kGateBase + v : kJackGate;
        const bool read = (poly || v == 0) && ch < numCh;
        voices[v].gateEdges.scan(read ? buffer.getReadPointer(ch) : nullptr, numSamples);
    }
}
