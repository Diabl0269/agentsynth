#pragma once

#include "Modules/ModuleBase.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::ui {

/** One Mod Matrix source entry: the raw output channel a routing reads, and the jack name shown
 *  after the module's title (empty when the module offers a single entry). */
struct ModSourceOutput {
    int channel = 0;
    juce::String label;
};

/** The sources a module offers the Mod Matrix: one per visible output jack, the ones its card draws.
 *  A raw channel that is no jack (an LFO's silent pass-throughs) is no source either. A jack that
 *  fronts two poly heads (Poly MIDI's Pitch and Gate) lists each, named by role. See
 *  docs/modules/modulation.md#smart-cables-poly-bus-wires-and-the-mod-matrix. */
std::vector<ModSourceOutput> modSourceOutputs(const ModuleBase& module);

/** The destinations a module offers the Mod Matrix: its modulation targets, plus channel 0 of a
 *  Macro In port spliced into an existing routing (display only, see the definition). */
std::vector<ModulationTarget> modDestinationCandidates(ModuleBase* module);

} // namespace synth::ui
