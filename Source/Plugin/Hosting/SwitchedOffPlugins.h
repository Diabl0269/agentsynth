#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// SwitchedOffPlugins.h (docs/architecture/plugin-layer.md#when-a-plugin-takes-the-app-down): which hosted plugins that
// feed a channel strip were switched off by "Open without it". The track header and the mixer column both show a
// badge from this one answer, so the two can never disagree. Runtime only: nothing here is saved with the project.
namespace synth {

struct SwitchedOffPlugin {
    juce::String title;    // the card's default title, e.g. "Serum 2"
    juce::String nodeUuid; // the node's stable uuid, what "show on the canvas" takes
};

/** Every Hosted Plugin that is isSwitchedOffAfterCrash() and plays into `strip`, sorted by title, found by walking the
 *  cables backward from the strip. The walk stops at another channel strip (a bus's sources are listed on their own
 *  strips) and never lists the same node twice. Empty when `strip` is not in the graph. Message thread. */
std::vector<SwitchedOffPlugin> switchedOffPluginsFeeding(juce::AudioProcessorGraph& graph,
                                                         juce::AudioProcessorGraph::NodeID strip);

/** The titles of `plugins`, in order. */
juce::StringArray titlesOf(const std::vector<SwitchedOffPlugin>& plugins);

} // namespace synth
