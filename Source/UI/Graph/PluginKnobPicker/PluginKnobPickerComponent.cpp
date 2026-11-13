// PluginKnobPickerComponent.cpp -- the hosted plugin's card layout editor: the shared editor over the
// instance's parameters. docs/control/plugin-card-layout.md#choosing-knobs.
#include "PluginKnobPickerComponent.h"
#include "UI/Graph/CardLayoutEditor/HostedCardLayoutSource.h"

namespace synth::ui {

PluginKnobPickerComponent::PluginKnobPickerComponent(HostedPluginModule& module, PluginCardLayoutStore* store,
                                                     juce::AudioProcessorGraph& graph,
                                                     juce::AudioProcessorGraph::NodeID nodeId,
                                                     AppUndoManager* undoManager, const ShortcutManager* shortcuts)
    : CardLayoutEditorComponent(std::make_unique<HostedCardLayoutSource>(module, store, graph, nodeId, undoManager),
                                shortcuts) {}

PluginKnobPickerComponent::~PluginKnobPickerComponent() = default;

} // namespace synth::ui
