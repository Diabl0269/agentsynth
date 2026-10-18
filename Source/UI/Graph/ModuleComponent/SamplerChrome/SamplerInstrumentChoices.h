#pragma once

// The rows of an instrument picker: the built-in sound pack instruments (all of them, or one category) and the "Your
// files" loaders; the Drum Kit's adds a "Synth drums" row that drops the kit.

#include "UI/Graph/ModMatrixPicker.h"
#include <vector>

namespace synth::ui {

struct InstrumentChoice {
    enum class Kind { Pack, SfzFile, Recordings, SynthDrums };
    Kind kind = Kind::Pack;
    juce::String packId, instrumentId;
};

struct InstrumentChoiceOptions {
    juce::String category;        ///< Only the pack instruments of this category (empty = all).
    bool offerSynthDrums = false; ///< A first "Synth drums" row that clears the instrument.
    bool noKitLoaded = false;     ///< Ticks the "Synth drums" row.
};

struct InstrumentChoices {
    std::vector<InstrumentChoice> choices;
    // Item id n + 1 = choices[n]; a row with no choice (the empty-pack note) has id 0.
    std::vector<ModMatrixPicker::Item> items;
};

/** The rows for the instruments the pack library lists now; `currentPack` ("<packId>/<instrumentId>", or empty) is
 *  ticked. */
InstrumentChoices collectInstrumentChoices(const juce::String& currentPack,
                                           const InstrumentChoiceOptions& options = {});

} // namespace synth::ui
