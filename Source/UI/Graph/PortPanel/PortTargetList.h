#pragma once

// What a jack can be connected to from the port connections panel: the jacks (and, for a modulation output, the
// knobs) of the other modules that a cable from this jack may land on, grouped by module and nearest first, plus the
// module types the search can create. Pure data like PortConnectionList, so it is tested without a window. The rules
// are the cable drop's (GraphEditor::endConnectionDrag, now PortConnector::connectJacks): an output lists inputs, an
// input lists outputs, MIDI only with MIDI, never the same module. docs/layout/cables.md#port-connections-panel.

#include "PortConnectionList.h"
#include <vector>

namespace synth::ui {

struct PortTarget {
    enum class Kind { Jack, Knob, NewModule };

    Kind kind = Kind::Jack;
    juce::AudioProcessorGraph::NodeID node; // the module (invalid for a NewModule)
    PortRef jack;                           // Jack: the jack the cable lands on
    int knobChannel = -1;                   // Knob: the raw CV channel of the knob
    juce::String moduleTitle;               // "Filter 1"; a NewModule: "New Osc"
    juce::String portName;                  // "Cutoff", "In 1"; empty for a NewModule
    juce::String aliases;                   // other names a search matches, never shown
    juce::String newType;                   // NewModule: the factory key it creates
    bool connected = false;                 // already wired to this jack: shown greyed, not pickable

    bool isNew() const noexcept { return kind == Kind::NewModule; }
    /** "<module> . <port>"; a NewModule: its own name. */
    juce::String label() const { return portName.isEmpty() ? moduleTitle : moduleTitle + portSeparator() + portName; }
    /** What a search matches: the module and port names plus the aliases. */
    juce::String searchText() const { return aliases.isEmpty() ? label() : label() + " " + aliases; }
};

/** One module's targets, in the card's own order (MIDI, the jacks, then the knobs). */
struct PortTargetModule {
    juce::AudioProcessorGraph::NodeID node;
    juce::String title;
    std::vector<PortTarget> targets;
};

/** Every compatible target of `port`, modules nearest `port`'s card first (centre to centre on the canvas, title
 *  breaking a tie). Modules with nothing compatible are left out, as are hidden cards, Attenuverters and macro ports.
 */
std::vector<PortTargetModule> listPortTargets(GraphEditor& editor, const PortRef& port);

/** The "New <module>" rows: authorable module types with a jack that can take a cable from `port`, once per class
 *  (keys that make the same class are one row, the others become aliases), singleton IO modules left out. */
std::vector<PortTarget> listNewModuleTargets(const PortRef& port);

/** The target in `modules` for a jack of `node` / for raw CV channel `channel` of `node`; null when it is not one. */
const PortTarget* findJackTarget(const std::vector<PortTargetModule>& modules, const PortRef& jack);
const PortTarget* findKnobTarget(const std::vector<PortTargetModule>& modules, juce::AudioProcessorGraph::NodeID node,
                                 int channel);

} // namespace synth::ui
