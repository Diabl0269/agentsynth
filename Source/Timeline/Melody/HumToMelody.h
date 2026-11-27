// The whole conversion of a hummed take to melody notes: pitch tracking, note cutting, then as-sung or snapped
// quantising. Pure; call it off the message thread, it costs about a second per minute of audio.
#pragma once

#include "Timeline/Melody/MelodyQuantize.h"

#include <vector>

namespace synth::melody {

/** The melody in `numSamples` mono samples at `sampleRate`. `usedKey` receives the key snapped to (see
 *  notesToMelody). */
std::vector<MelodyNote> humToMelody(const float* mono, int numSamples, double sampleRate, const MelodyOptions& options,
                                    MusicalScale* usedKey = nullptr);

} // namespace synth::melody
