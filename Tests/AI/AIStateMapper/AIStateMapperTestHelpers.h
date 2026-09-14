// Shared helper used across AIStateMapper*Tests.cpp topic files.
#pragma once

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Helper function to create a basic graph for testing
static void createBasicGraph(juce::AudioProcessorGraph& graph) {
    graph.clear();

    auto audioInputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    auto audioOutputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    // Connect input to osc, osc to filter, filter to output
    graph.addConnection({{audioInputNode->nodeID, 0}, {oscNode->nodeID, 0}});
    graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}});
    graph.addConnection({{filterNode->nodeID, 0}, {audioOutputNode->nodeID, 0}});
}
