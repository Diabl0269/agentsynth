#pragma once

#include "AppUndoManager.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"

namespace synth::ui {

/**
 * Adds the "Add to card" item to the context menu a hosted VST3 plugin's own window shows for one of
 * its parameters: "Add to card" when the parameter is not on the card, a disabled "On the card" when it
 * already is, nothing for an id the instance does not have. Choosing it shows the parameter through
 * HostedCardLayoutSource::showParameter -- the picker's own write, so the card rebuilds and the change
 * is one undo step. The module is held weakly by the item's action, which runs on the message thread
 * after the menu closes. `store` and `undoManager` may be null; `graph` must outlive the menu.
 * docs/control/plugin-card-layout.md#add-to-card-from-the-plugin-window.
 */
void appendAddToCardMenuItem(juce::PopupMenu& menu, HostedPluginModule& module, PluginCardLayoutStore* store,
                             juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                             AppUndoManager* undoManager, const juce::String& paramId);

} // namespace synth::ui
