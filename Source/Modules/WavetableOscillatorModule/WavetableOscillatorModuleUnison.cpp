// WavetableOscillatorModuleUnison.cpp -- the Unison knob's cable. Unison is a whole number of voices, so a cable
// does not slide it: the cable is read where a note starts and rounded to the nearest voice count, and that count
// stays until the next note starts. The class and channel map: WavetableOscillatorModule.h; the gain glide that
// keeps a change of count from clicking: UnisonMix.h.

#include "WavetableOscillatorModule.h"

// The first sample of the block on which some voice starts a note: a MIDI note-on, a rising Gate edge, or a poly
// voice that begins to sound (which restarts on the block's first sample). `numSamples` when none does.
int WavetableOscillatorModule::firstNoteStart(const float* pitchCV, bool poly, int numSamples) const {
    int first = numSamples;
    for (int v = 0; v < (poly ? MAX_VOICES : 1); ++v) {
        if (voices[v].numMidiRestarts > 0)
            first = std::min(first, voices[v].midiRestarts[0]);
        if (voices[v].gateEdges.count > 0)
            first = std::min(first, voices[v].gateEdges.offsets[0]);
        if (poly && !voices[v].active && (v == 0 || pitchCV[v] >= 20.0f))
            first = 0;
    }
    return first;
}

// The count is the knob until a note starts with a cable present: then it is the knob moved by the cable on the sample
// the first note of the block starts, rounded to a whole voice, and it stays that until the next note starts (a cable
// that changes mid-note, or is removed, takes effect at the next note). A note that starts with the cable silent
// hands the count back to the knob, which then follows it live. With no note yet (a drone), a cable is read once at
// the block start so it is not ignored.
void WavetableOscillatorModule::holdUnisonCable(const juce::AudioBuffer<float>& buffer, bool poly, int numSamples) {
    const int channel = mods_.firstChannel();
    const bool patched = channel < buffer.getNumChannels() && isChannelActive(buffer, channel, numSamples);
    float pitch[MAX_VOICES]{};
    if (poly)
        for (int v = 0; v < MAX_VOICES && v < buffer.getNumChannels(); ++v)
            pitch[v] = buffer.getReadPointer(v)[0];
    const int start = firstNoteStart(pitch, poly, numSamples);
    const auto wholeVoices = [&](int sample) {
        return juce::jlimit(1, MAX_UNISON, juce::roundToInt(mods_.valueAt(buffer, *unisonParam, sample)));
    };
    if (start < numSamples) {
        noteSeen_ = true;
        heldUnison_ = patched ? wholeVoices(start) : -1;
    } else if (!noteSeen_ && heldUnison_ < 0 && patched) {
        heldUnison_ = wholeVoices(0);
    }
    unisonNow_ = heldUnison_ >= 0 ? heldUnison_ : juce::jlimit(1, MAX_UNISON, unisonParam->get());
}
