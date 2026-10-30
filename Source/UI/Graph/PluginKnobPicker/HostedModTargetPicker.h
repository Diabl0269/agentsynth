#pragma once

#include "UI/Graph/ModMatrixPicker.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

class GraphEditor;
class ModuleComponent;

namespace synth {
class HostedPluginModule;
}

namespace synth::ui {

/** Every parameter of `module`'s live instance as picker rows: "On the card" first, then "All parameters". Row id
 *  is the parameter's instance index + 1. Empty with no live instance. */
std::vector<ModMatrixPicker::Item> hostedModTargetItems(HostedPluginModule& module, GraphEditor& editor,
                                                        juce::AudioProcessorGraph::NodeID nodeId);

/** A modulation cable released on a plugin card's body: opens a search over every parameter of the plugin at
 *  `screenPos`. A pick runs applyHostedModTarget; Escape or a click away changes nothing. */
void showHostedModTargetPicker(GraphEditor& editor, ModuleComponent& card, juce::AudioProcessorGraph::NodeID sourceId,
                               int sourceJack, juce::Point<int> screenPos);

/** Puts `paramId` on the card when it is not there, claims its modulation slot and cables `sourceId`'s output jack
 *  `sourceJack` to it with the cable-drop rules, all as one undo step. False when nothing was cabled. */
bool applyHostedModTarget(GraphEditor& editor, juce::AudioProcessorGraph::NodeID nodeId,
                          juce::AudioProcessorGraph::NodeID sourceId, int sourceJack, const juce::String& paramId);

namespace test_hooks {
/** Tests: receives the picker instead of a call-out box (none can open headless). */
std::function<void(std::unique_ptr<ModMatrixPicker>)>& hostedModTargetPickerHookForTest();
} // namespace test_hooks

} // namespace synth::ui
