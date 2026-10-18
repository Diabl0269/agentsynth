#pragma once

// The rows of the Sampler's instrument picker: the built-in sound pack instruments and the "Your files" loaders.

#include "UI/Graph/ModMatrixPicker.h"
#include <vector>

namespace synth::ui {

struct InstrumentChoice {
    enum class Kind { Pack, SfzFile, Recordings };
    Kind kind = Kind::Pack;
    juce::String packId, instrumentId;
};

struct InstrumentChoices {
    std::vector<InstrumentChoice> choices;
    // Item id n + 1 = choices[n]; a row with no choice (the empty-pack note) has id 0.
    std::vector<ModMatrixPicker::Item> items;
};

/** The rows for the instruments the pack library lists now; `currentPack` ("<packId>/<instrumentId>", or empty) is
 *  ticked. */
InstrumentChoices collectInstrumentChoices(const juce::String& currentPack);

} // namespace synth::ui
