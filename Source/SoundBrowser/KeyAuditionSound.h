#pragma once

// The built-in sound the key pickers play a scale with when no instrument is to be heard: a soft plucked tone per
// note, rendered whole into one stereo buffer and handed to the sound preview (after the master, never in a bounce).

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace synth::soundbrowser {

class SoundPreviewPlayer;

/** Length of each note's slot in the rendered run, in milliseconds (the same step the instrument audition uses). */
inline constexpr int kKeyAuditionStepMs = 140;

/** The whole run as a stereo buffer at `sampleRate`: one soft pluck per pitch, one step apart, the last one ringing
 *  out a little longer. Pure: the same input gives the same samples; no click at the start or the end; peaks well
 *  under half scale so the preview's own gain keeps it quiet. Empty for no pitches or a rate of zero or less. */
juce::AudioBuffer<float> renderKeyAuditionSound(const std::vector<int>& pitches, double sampleRate);

/** Renders `pitches` at the preview's output rate and plays them, replacing any preview sound. Message thread. */
void playKeyAuditionSound(SoundPreviewPlayer& player, const std::vector<int>& pitches);

} // namespace synth::soundbrowser
