#pragma once

#include <juce_core/juce_core.h>

/**
 * @brief Channel shape of an audio/CV Macro port (docs/macros.md §5.3, P8-15 Macro I/O).
 *
 * Meaningless for a MIDI port — MacroMidiInletModule/MacroMidiOutletModule carry no shape at all,
 * just acceptsMidi()/producesMidi() (see that class's comment). Chosen once, at port-creation
 * time (GraphEditor::addMacroPort), and immutable thereafter: changing a live port's shape means
 * deleting the node and constructing a new one — GraphEditor::changeMacroPortShape does exactly
 * that, as one undo step, never a live renegotiation of MacroInletModule/MacroOutletModule's
 * fixed kMaxChannels bus.
 */
enum class MacroPortShape { Mono, Stereo, Poly };

inline juce::String macroPortShapeToString(MacroPortShape shape) {
    switch (shape) {
    case MacroPortShape::Stereo:
        return "stereo";
    case MacroPortShape::Poly:
        return "poly";
    case MacroPortShape::Mono:
    default:
        return "mono";
    }
}

/** Unrecognised/absent input parses as Mono — every pre-P8-15c save (and every MIDI-kind port,
 *  which never writes this key at all) has no "shape" property, and Mono is the shape those saves
 *  already behave as (docs/macros.md §5.3's implementation note: "today the only value anything
 *  ever sets is Mono"). */
inline MacroPortShape macroPortShapeFromString(const juce::String& s) {
    if (s == "stereo")
        return MacroPortShape::Stereo;
    if (s == "poly")
        return MacroPortShape::Poly;
    return MacroPortShape::Mono;
}
