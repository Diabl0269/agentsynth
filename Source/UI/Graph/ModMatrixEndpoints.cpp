// ModMatrixEndpoints.cpp -- what the Mod Matrix's source and destination lists offer per module.
#include "ModMatrixEndpoints.h"

#include "Modules/MacroInletModule.h"
#include <algorithm>

namespace synth::ui {

namespace {
juce::String roleLabel(PortRole role) {
    switch (role) {
    case PortRole::Pitch:
        return "Pitch";
    case PortRole::Gate:
        return "Gate";
    case PortRole::ModCV:
        return "CV";
    case PortRole::Midi:
        return "MIDI";
    case PortRole::Sidechain:
        return "Sidechain";
    case PortRole::Audio:
        return "Audio";
    default:
        return {};
    }
}
} // namespace

std::vector<ModSourceOutput> modSourceOutputs(const ModuleBase& module) {
    std::vector<ModSourceOutput> out;
    const int numRaw = module.getTotalNumOutputChannels();
    for (int jack = 0; jack < module.getVisibleOutputPortCount(); ++jack) {
        const auto targets = module.getJackTargets(jack, /*isInput=*/false);
        for (const auto& t : targets) {
            if (t.rawHeadChannel < 0 || t.rawHeadChannel >= numRaw)
                continue;
            if (std::any_of(out.begin(), out.end(), [&](const auto& o) { return o.channel == t.rawHeadChannel; }))
                continue;
            auto label = module.getOutputPortLabel(jack);
            if (targets.size() > 1 && roleLabel(t.role).isNotEmpty())
                label += " " + roleLabel(t.role);
            out.push_back({t.rawHeadChannel, label});
        }
    }
    // A single entry reads as just the module's name.
    if (out.size() == 1)
        out.front().label = {};
    return out;
}

// MacroInletModule deliberately declares NO getModulationTargets() — GraphEditor::connectPorts()
// relies on that empty list to keep a plain cable drop onto a Macro In's jack a plain connection,
// never auto-wrapped in a fresh attenuverter (Tests/Macros/MacroPortFlowTests.cpp's drop-a-cable tests
// pin that). A modulation cable through an attenuverter
// (docs/macros/auto-ports.md#a-modulation-cable-through-an-attenuverter) can still
// splice a MacroInletModule in as the DESTINATION of an EXISTING AttenuverterChain crossing a macro boundary, so a
// matrix row's destination combo needs something to show and match against — without changing what the module declares
// globally (which would resurrect the auto-wrap problem for ordinary drops). Display-only: this never becomes a real
// ModulationTarget the module advertises anywhere else. A spliced port is always Mono with its one active raw channel
// at 0 (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed /
// docs/macros/auto-ports.md#a-modulation-cable-through-an-attenuverter — the internal jack an AttenuverterChain lands
// on is never poly-fanned), so channel 0 is the only candidate.
std::vector<ModulationTarget> modDestinationCandidates(ModuleBase* module) {
    auto targets = module->getModulationTargets();
    if (targets.empty() && dynamic_cast<MacroInletModule*>(module) != nullptr)
        targets.push_back({"In", 0});
    return targets;
}

} // namespace synth::ui
