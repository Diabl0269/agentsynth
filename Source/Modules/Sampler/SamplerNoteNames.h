#pragma once

// Note-name parsing and the guesses made from dropped file names.

#include "Modules/Sampler/SamplerInstrument.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace synth::sampler {

/** "C4", "c#3", "Db3", "Bb-1" or a plain integer, as MIDI 0..127; nullopt otherwise. `octaveForMiddleC` is the
    octave number of MIDI 60 (SFZ uses 4, the app's display 3). */
std::optional<int> parseNoteName(const juce::String& text, int octaveForMiddleC);

struct FileNameHints {
    std::optional<int> note;
    int layer = 0; // -1 soft, 0 unknown, +1 loud
};

/** Note (app convention, C3 = 60) and loudness found in a file name without its extension. */
FileNameHints hintsFromFileName(const juce::String& fileNameWithoutExtension);

/** Lays dropped files (given in name order) out across the keyboard and velocity range. */
std::vector<RegionSpec> placeDroppedFiles(const juce::StringArray& sortedFilePaths);

} // namespace synth::sampler
