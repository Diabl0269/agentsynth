#pragma once

#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

// One modulation routing into an automated parameter's CV jack, as the timeline sees it: the graph
// is never stored in the timeline doc, so a lane's modulator rows are re-derived from these every
// time the graph changes. Every node is named by uuid (a node id does not survive an undo restore).
struct ModulatorInfo {
    juce::String sourceUuid;         // the module driving the jack (an LFO, an envelope, a macro port...)
    juce::String sourceTitle;        // its card title
    int sourceChannel = 0;           // raw output channel of the source
    bool isLfo = false;              // an LFO gets the full row; anything else is read-only
    juce::String attenuverterUuid;   // the hidden depth node; empty for a direct cable (no depth control)
    juce::String targetUuid;         // the lane's node
    juce::String paramId;            // the lane's parameter
    int targetChannel = 0;           // raw CV channel on the target
    juce::Colour colour{0xff00D1FF}; // the modulation wire's resolved colour

    /** What identifies the routing across refreshes (values and titles may change, this may not). */
    juce::String key() const {
        return sourceUuid + "/" + juce::String(sourceChannel) + "/" + attenuverterUuid + "/" +
               juce::String(targetChannel);
    }
};

// Where a parameter edit from a modulator row is in its gesture: Begin opens the undo step, Change
// moves the live value, End closes the step; Once is all three for a single click or key press.
enum class ParameterEditPhase { Begin, Change, End, Once };

} // namespace synth::ui
