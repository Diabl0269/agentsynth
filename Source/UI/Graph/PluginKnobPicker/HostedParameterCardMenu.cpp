// HostedParameterCardMenu.cpp -- the "Add to card" item of a hosted plugin window's parameter context
// menu. docs/control/plugin-card-layout.md#add-to-card-from-the-plugin-window.
#include "HostedParameterCardMenu.h"
#include "UI/Graph/CardLayoutEditor/HostedCardLayoutSource.h"

namespace synth::ui {

void appendAddToCardMenuItem(juce::PopupMenu& menu, HostedPluginModule& module, PluginCardLayoutStore* store,
                             juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                             AppUndoManager* undoManager, const juce::String& paramId) {
    if (!module.hasInstance())
        return;
    const HostedCardLayoutSource source(module, store, graph, nodeId, undoManager);
    switch (source.parameterState(paramId)) {
    case HostedCardLayoutSource::ParameterState::Unknown:
        return;
    case HostedCardLayoutSource::ParameterState::OnCard:
        menu.addItem("On the card", /*isActive*/ false, /*isTicked*/ false, nullptr);
        return;
    case HostedCardLayoutSource::ParameterState::Available:
        break;
    }

    // The action rebuilds its source: the layout may have changed while the menu was open, and a source
    // captures the instance's parameters when it is built.
    menu.addItem("Add to card", [weak = juce::WeakReference<HostedPluginModule>(&module), store, &graph, nodeId,
                                 undoManager, paramId] {
        auto* live = weak.get();
        if (live == nullptr || !live->hasInstance())
            return;
        HostedCardLayoutSource(*live, store, graph, nodeId, undoManager).showParameter(paramId);
    });
}

} // namespace synth::ui
