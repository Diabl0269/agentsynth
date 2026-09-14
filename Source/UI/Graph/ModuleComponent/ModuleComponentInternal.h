#pragma once

// Private to the ModuleComponent.cpp / ModuleComponent*.cpp translation units (FRO65 split of the
// former single ModuleComponent.cpp). Holds the file-local constants and free helpers used by more
// than one of those units — everything used by only one unit stays in that unit's own file instead.
// Not part of the public API: nothing outside the ModuleComponent units should include this header.
// Assumes ModuleComponent.h is included first (for ModuleComponent::kMacroPortWidgetHeaderY, and
// the JUCE module headers these declarations depend on).

#include "Modules/ModuleBase.h"

namespace detail {

// Sentinel wavetable tab-strip page ids for controls that live outside the strip (see
// wavetablePageFor and the WavetablePage table in ModuleComponentWavetable.cpp). Shared with
// layoutDefaultContent (ModuleComponentLayout.cpp), which tests kTabChrome to tell whether a
// tabbed card has replaced the flat combo grid.
inline constexpr int kTabPinned = -1; // always visible, above the strip
inline constexpr int kTabChrome = -2; // laid out with the display band instead

// ---- Default body-layout metrics (see layoutDefaultContent) ----------------------------------
// Three knobs per row instead of two: the body sits below every jack, so it can use nearly the
// full card width, and the extra column removes a whole row of height from most modules.
inline constexpr int kKnobColumns = 3;
inline constexpr int kContentMargin = 12;       // left/right gutter for body content
inline constexpr int kNarrowContentWidth = 200; // combos/toggles/load row stay this narrow, centred
inline constexpr int kLabelHeight = 18;
inline constexpr int kRowHeight = 24;  // combo box / toggle / button
inline constexpr int kKnobHeight = 58; // rotary + its text box
inline constexpr int kWaveformHeight = 72;
inline constexpr int kBottomPadding = 12;
// A port label box spans its jack centre ± 10; clear it by a bit more before placing any content.
inline constexpr int kPortLabelClearance = 15;
// Horizontal step between input-jack columns on a multi-column gutter. A jack sits at x, its
// label runs from x+10 for 60px, so 100 leaves a 30px gap before the next column's jack.
inline constexpr int kPortColumnStride = 100;

inline ModuleType getType(juce::AudioProcessor* module) {
    if (auto* mb = dynamic_cast<ModuleBase*>(module))
        return mb->getModuleType();
    return ModuleType::Oscillator;
}

/** The four macro-boundary node types (Macro In/Out, Macro MIDI In/Out — docs/macros_ports.md §5.1),
 *  which render as the compact docked port widget (P8-15 founder-review fix F2) rather than an
 *  ordinary module card: no header chrome, no body, a small tinted row docked to their macro's
 *  hull edge instead of freely placed. See layoutMacroPortWidget()/paintMacroPortWidget(). */
inline bool isMacroPortType(ModuleType t) {
    return t == ModuleType::MacroInlet || t == ModuleType::MacroOutlet || t == ModuleType::MacroMidiInlet ||
           t == ModuleType::MacroMidiOutlet;
}

/** The MIDI jack's fixed y — the generic layout's own "38, below the header" convention, compacted
 *  for the macro-port widget (which has no header). Shared by paint()'s MIDI dot and
 *  getPortForPoint()'s MIDI hit-test so the two can never disagree, mirroring every other
 *  paint/hit-test pairing in this file. */
inline int midiJackY(juce::AudioProcessor* module) {
    return isMacroPortType(getType(module)) ? ModuleComponent::kMacroPortWidgetHeaderY : 38;
}

/** True for the graph's terminal audio sink (Audio Output). Mirrors GraphEditor.cpp's
 *  isTerminalAudioSink — detected by TYPE, not by name, so a ModuleBase happening to be titled
 *  "Audio Output" cannot impersonate it. A bare juce::AudioGraphIOProcessor is never a ModuleBase,
 *  so getType() above always falls back to Oscillator for it; this is the one place in this file
 *  that actually cares which IO node it is (the output-card identity treatment in paint()). */
inline bool isAudioOutputIONode(juce::AudioProcessor* module) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    auto* io = dynamic_cast<IOProcessor*>(module);
    return io != nullptr && io->getType() == IOProcessor::audioOutputNode;
}

} // namespace detail
