#include "Timeline/Melody/HumToMelody.h"

namespace synth::melody {

std::vector<MelodyNote> humToMelody(const float* mono, int numSamples, double sampleRate, const MelodyOptions& options,
                                    MusicalScale* usedKey) {
    const auto frames = trackPitch(mono, numSamples, sampleRate);
    return notesToMelody(cutNotes(frames), options, usedKey);
}

} // namespace synth::melody
