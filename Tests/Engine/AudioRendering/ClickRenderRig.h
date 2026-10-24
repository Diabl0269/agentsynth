#pragma once

// Renders a sound source through an ADSR-gated VCA for a list of notes, block by block with the notes
// landing wherever they fall, and hands back each stage's samples for the shared click detector
// (ClickDetector.h). Header-only; used by the click audit and the Oscillator click tests.

#include "AudioRenderingTestHelpers.h"
#include "ClickDetector.h"
#include "Modules/VCAModule.h"
#include <vector>

namespace clickrig {

struct Note {
    int on = 0;  ///< first sample of the note
    int off = 0; ///< sample the note ends on (a touching note starts on the same sample)
    int pitch = 60;
};

struct Rendered {
    std::vector<float> source;      ///< the source's own left leg, before the envelope
    std::vector<float> sourceRight; ///< its right leg; empty when it has none
    std::vector<float> out;         ///< left leg after ADSR and VCA
    std::vector<float> outRight;    ///< right leg after ADSR and VCA; empty when the source has none
};

struct Envelope {
    float attack = 0.001f;
    float decay = 0.0735f;
    float sustain = 1.0f;
    float release = 0.015f;
};

/** One note per pitch, `stepSamples` apart and each touching the next (`staccato` ends it after a third). */
inline std::vector<Note> notesEvery(const std::vector<int>& pitches, int stepSamples, bool staccato = false) {
    std::vector<Note> notes;
    for (size_t i = 0; i < pitches.size(); ++i) {
        const int on = (int)i * stepSamples;
        notes.push_back({on, staccato ? on + stepSamples / 3 : on + stepSamples, pitches[i]});
    }
    return notes;
}

/** Sixteenth notes at 120 bpm: 6000 samples at 48 kHz, so most note starts land mid-block. */
inline std::vector<Note> touchingSixteenths(const std::vector<int>& pitches, double sampleRate, bool staccato = false) {
    return notesEvery(pitches, (int)std::lround(0.125 * sampleRate), staccato);
}

inline Rendered renderThroughEnvelope(juce::AudioProcessor& source, const std::vector<Note>& notes, int total,
                                      const Envelope& env = {}, double sampleRate = kSampleRate,
                                      int blockSize = kBlockSize) {
    ADSRModule adsr;
    VCAModule vca;
    setParamValue(&adsr, "attack", env.attack);
    setParamValue(&adsr, "decay", env.decay);
    setParamValue(&adsr, "sustain", env.sustain);
    setParamValue(&adsr, "release", env.release);
    source.setPlayConfigDetails(source.getTotalNumInputChannels(), source.getTotalNumOutputChannels(), sampleRate,
                                blockSize);
    source.prepareToPlay(sampleRate, blockSize);
    adsr.prepareToPlay(sampleRate, blockSize);
    vca.prepareToPlay(sampleRate, blockSize);

    const auto channelsOf = [](juce::AudioProcessor& p) {
        return std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    };
    const int rightLeg =
        dynamic_cast<ModuleBase*>(&source) != nullptr ? dynamic_cast<ModuleBase*>(&source)->rightAudioLegChannel() : -1;
    const bool stereo = rightLeg > 1 && rightLeg < channelsOf(source);

    Rendered r;
    for (int pos = 0; pos < total; pos += blockSize) {
        const int n = std::min(blockSize, total - pos);
        juce::MidiBuffer midi;
        for (const auto& note : notes) {
            if (note.off >= pos && note.off < pos + n)
                midi.addEvent(juce::MidiMessage::noteOff(1, note.pitch, 0.0f), note.off - pos);
        }
        for (const auto& note : notes) {
            if (note.on >= pos && note.on < pos + n)
                midi.addEvent(juce::MidiMessage::noteOn(1, note.pitch, 0.8f), note.on - pos);
        }
        juce::AudioBuffer<float> srcBuf(channelsOf(source), n), adsrBuf(channelsOf(adsr), n),
            vcaBuf(channelsOf(vca), n);
        srcBuf.clear();
        adsrBuf.clear();
        vcaBuf.clear();
        juce::MidiBuffer forAdsr = midi;
        source.processBlock(srcBuf, midi);
        adsr.processBlock(adsrBuf, forAdsr);
        vcaBuf.copyFrom(0, 0, srcBuf, 0, 0, n);
        vcaBuf.copyFrom(1, 0, adsrBuf, 0, 0, n);
        if (stereo)
            vcaBuf.copyFrom(VCAModule::kRightBase, 0, srcBuf, rightLeg, 0, n);
        juce::MidiBuffer none;
        vca.processBlock(vcaBuf, none);

        const auto append = [n](std::vector<float>& to, const juce::AudioBuffer<float>& from, int ch) {
            to.insert(to.end(), from.getReadPointer(ch), from.getReadPointer(ch) + n);
        };
        append(r.source, srcBuf, 0);
        append(r.out, vcaBuf, 0);
        if (stereo) {
            append(r.sourceRight, srcBuf, rightLeg);
            append(r.outRight, vcaBuf, VCAModule::kRightBase);
        }
    }
    return r;
}

} // namespace clickrig
