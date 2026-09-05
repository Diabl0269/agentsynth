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
 *
 * `Stereo` vs `StereoCollapsed` (founder-review fix G2, docs/macros.md §5.3/§7 item 7): these
 * are deliberately TWO values, not one overloaded meaning, because they answer a genuinely
 * different question — "how many visible jacks does this port show" — for two different sources
 * of a stereo pair:
 *
 *   - `Stereo` is the HAND-PICKED shape, reachable only from the Configure I/O modal
 *     (MacroPortConfigDialog never offers `StereoCollapsed` as a choice). It presents TWO visible
 *     jacks (Left/Right on raw ch0 and the dedicated `kRightBase` block) — the split-block
 *     convention every Dual-I/O-capable module in this codebase uses when its two legs are on
 *     SEPARATE visible jacks (`ModuleBase::rightAudioLegChannel()`, never ch1). It is also what a
 *     Dual-I/O-ON crossing (two separately-jacked Left/Right cables) is spliced into by
 *     `GraphEditor::buildMacroPortCrossingPlan`'s merge pass, because that module genuinely shows
 *     two jacks too.
 *   - `StereoCollapsed` is AUTO-DERIVED ONLY, produced when `buildMacroPortCrossingPlan` splices a
 *     port for a crossing cable that lands on an ordinary module's own COLLAPSED stereo jack — one
 *     visible "Audio" jack whose raw ch0/ch1 are a contiguous pair (`ModuleBase::
 *     mapStereoPairOutput`'s Dual-I/O-off branch; every FX module defaults here). Carrying BOTH
 *     raw channels while presenting only ONE visible jack is exactly what that internal jack
 *     itself does — a port that fronts it must present the same shape, not silently grow a second
 *     jack the module it stands for never had. Never selectable by hand: it exists purely so an
 *     auto-created port can mirror an existing collapsed jack; a user picking "Stereo" in the
 *     modal always means the two-jack shape above.
 *
 * Raw layout for `StereoCollapsed` mirrors `mapStereoPairOutput`'s collapsed branch exactly:
 * raw ch0 (head, `polyVoiceSpan` 2) and raw ch1 (follower) are CONTIGUOUS, unlike `Stereo`'s
 * split-block ch0/kRightBase pair — contiguity is load-bearing, since `GraphEditor::
 * getJackTargets()` and every caller that expands a `JackTarget` walk `rawHeadChannel .. +
 * voiceSpan - 1` assuming adjacency.
 */
enum class MacroPortShape { Mono, Stereo, Poly, StereoCollapsed };

inline juce::String macroPortShapeToString(MacroPortShape shape) {
    switch (shape) {
    case MacroPortShape::Stereo:
        return "stereo";
    case MacroPortShape::StereoCollapsed:
        return "stereo_collapsed";
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
    if (s == "stereo_collapsed")
        return MacroPortShape::StereoCollapsed;
    if (s == "poly")
        return MacroPortShape::Poly;
    return MacroPortShape::Mono;
}
