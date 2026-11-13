#pragma once

// Reader for the SFZ subset that free sample packs use: regions with key/velocity ranges, loops, round robin,
// release triggers and tuning.

#include "Modules/Sampler/SamplerInstrument.h"
#include <juce_core/juce_core.h>

namespace synth::sampler {

struct SfzParseResult {
    std::vector<RegionSpec> regions;
    juce::StringArray warnings;
};

/** Parses SFZ `text`; sample paths resolve against `sfzDirectory`. Does no file I/O. */
SfzParseResult parseSfz(const juce::String& text, const juce::File& sfzDirectory);

/** Reads, parses and builds `sfzFile`; the instrument is named after the file. Null with `errors` filled on
    failure. Message thread. */
Instrument::Ptr loadSfzInstrument(const juce::File& sfzFile, juce::StringArray& errors);

} // namespace synth::sampler
