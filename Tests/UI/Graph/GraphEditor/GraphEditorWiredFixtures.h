#pragma once

// Reverb-fronted wiring fixtures for the GraphEditor smart-connection tests. Split out of GraphEditorTestHelpers.h so
// ReverbModule.h is not pulled into every GraphEditor test (header-reach cap).

#include "GraphEditorTestHelpers.h"
#include "Modules/FX/ReverbModule.h"

/** Sink fed on both legs by a collapsed Reverb — the factory-preset shape. The sink is in the output dock, so its x is
 *  DERIVED (right of the Reverb), never the 760 a test would pick: the Reverb is parked below the sink's row so a
 *  280px-wide ghost fits just left of the sink, and `ghostTopLeft` is that spot, computed from the sink's real place.
 */
struct WiredSinkFixture {
    juce::AudioProcessorGraph::Node::Ptr outNode, reverbNode;
    juce::AudioProcessorGraph::NodeID outId, reverbId;
    juce::Point<int> ghostTopLeft;
};
inline WiredSinkFixture makeWiredSink(AudioEngine& engine, GraphEditor& editor) {
    WiredSinkFixture f;
    auto& graph = engine.getGraph();
    f.outNode = addAudioOutputNode(graph, 760, 100);
    f.reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    f.reverbNode->properties.set("x", 40);
    f.reverbNode->properties.set("y", 1100);
    editor.updateComponents();
    sizeModuleComponents(editor);
    editor.reflowOutputDock(); // the dock follows the resized cards
    f.outId = f.outNode->nodeID;
    f.reverbId = f.reverbNode->nodeID;
    editor.connectPorts(f.reverbId, 0, f.outId, 0, false, false);
    f.ghostTopLeft = {static_cast<int>(f.outNode->properties["x"]) - 280 - 40, 100};
    return f;
}

/** Reverb (upstream, 40/600) → Delay (insert target, 760/100), one collapsed cable covering both
 *  raw legs, so the target's audio input group is FULLY occupied. Leaves room for a 280px ghost at
 *  x=440 just left of the Delay. Collapsed FX on both ends keeps the group a single leg — a Filter
 *  fronts TWO audio input legs (L/R), and wiring only one leaves a mixed free/occupied group that
 *  insert deliberately refuses (see SmartConnectionCtrlDoesNotInsertIntoPartlyWiredStereoInput). */
struct WiredChainFixture {
    juce::AudioProcessorGraph::Node::Ptr upstreamNode, targetNode;
    juce::AudioProcessorGraph::NodeID upstreamId, targetId;
};
inline WiredChainFixture makeWiredChain(AudioEngine& engine, GraphEditor& editor, bool wireIt = true) {
    WiredChainFixture f;
    auto& graph = engine.getGraph();
    f.targetNode = graph.addNode(std::make_unique<DelayModule>());
    f.targetNode->properties.set("x", 760);
    f.targetNode->properties.set("y", 100);
    f.upstreamNode = graph.addNode(std::make_unique<ReverbModule>());
    f.upstreamNode->properties.set("x", 40);
    f.upstreamNode->properties.set("y", 600);
    editor.updateComponents();
    sizeModuleComponents(editor);
    f.targetId = f.targetNode->nodeID;
    f.upstreamId = f.upstreamNode->nodeID;
    if (wireIt)
        editor.connectPorts(f.upstreamId, 0, f.targetId, 0, false, false);
    return f;
}
