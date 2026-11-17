#pragma once

// Finds the key of a loop from its audio when neither its ACID chunk nor its name gives one, so Match key
// and the Key filter work on the user's own loops. A pitch-class profile (chroma) of the first seconds is compared with
// the Krumhansl-Kessler major and minor key profiles; audio with no clear tonal centre (drums, noise, silence) gets
// no key rather than a guess. Pure Core: no UI, no threads; the index scan calls it on its own thread.

#include <array>
#include <juce_audio_formats/juce_audio_formats.h>

namespace synth::soundbrowser {

/** Twelve pitch-class weights, C first, summed over the analysed audio. */
using Chroma = std::array<double, 12>;

/** The chroma of `numSamples` mono samples at `sampleRate`; all zeros for silence or too little audio. */
Chroma chromaOf(const float* samples, int numSamples, double sampleRate);

/** "A minor", "C# major", or empty when the chroma names no key clearly enough to trust. */
juce::String keyFromChroma(const Chroma& chroma);

/** The key of the first `maxSeconds` of `reader`'s audio (channels mixed), or empty. */
juce::String detectKey(juce::AudioFormatReader& reader, double maxSeconds = 30.0);

} // namespace synth::soundbrowser
