#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth {

/**
 * The size a freshly built card of factory type `typeName` takes, measured from its card-body plan
 * and the card's port and chrome rules without building a component. Nullopt for a type whose body
 * is bespoke (Sequencer, MIDI Keyboard, Macros, Attenuverter, Parametric EQ, Wavetable, External
 * MIDI, Hosted Plugin, macro ports, Audio Input/Output) or that has no factory entry; the caller
 * falls back to its own table. Cached per type. Message thread only.
 */
std::optional<juce::Point<int>> measureDataDrivenCardSize(const juce::String& typeName);

} // namespace synth
