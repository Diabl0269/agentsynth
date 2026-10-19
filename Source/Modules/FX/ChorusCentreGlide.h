#pragma once

#include <algorithm>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace synth {

/**
 * @brief Runs a juce::dsp::Chorus over `block` so a gliding Centre Delay moves its read head smoothly.
 *
 * juce::dsp::Chorus takes the centre delay as one value per process call, so a glide stepped once per
 * block jumps the read position by a whole block's worth of the glide at every block edge. While
 * `centre` is smoothing, the block is processed in tiny pieces and `setCentre(value)` is called for
 * each from the smoother, so the position moves by a fraction of a sample at a time. At rest it is one
 * plain process call and `centre` is left alone (no extra cost, identical output).
 */
template <typename SetCentre>
void processWithCentreGlide(juce::dsp::Chorus<float>& chorus, juce::dsp::AudioBlock<float> block,
                            juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>& centre,
                            SetCentre&& setCentre) {
    if (!centre.isSmoothing()) {
        juce::dsp::ProcessContextReplacing<float> context(block);
        chorus.process(context);
        return;
    }
    constexpr size_t kPiece = 2;
    const size_t numSamples = block.getNumSamples();
    for (size_t pos = 0; pos < numSamples; pos += kPiece) {
        const size_t length = std::min(kPiece, numSamples - pos);
        setCentre(centre.getCurrentValue());
        centre.skip((int)length);
        juce::dsp::AudioBlock<float> piece = block.getSubBlock(pos, length);
        juce::dsp::ProcessContextReplacing<float> context(piece);
        chorus.process(context);
    }
}

} // namespace synth
