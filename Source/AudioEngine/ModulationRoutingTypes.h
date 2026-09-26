#pragma once

#include "Modules/PortRole.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Modulation-routing value types. AudioEngine produces them (AudioEngineModRouting.cpp) and
// re-exports each under its old nested name (AudioEngine::ModulationRouting, ...); they live here
// so the graph UI headers can store them without including AudioEngine.h.

struct ModRoutingInfo {
    juce::AudioProcessorGraph::NodeID attenuverterNodeID;
    juce::AudioProcessorGraph::NodeID sourceNodeID;
    int sourceChannelIndex;
    juce::AudioProcessorGraph::NodeID destNodeID;
    int destChannelIndex;
    bool isBypassed;
};

struct ModulationDisplayInfo {
    juce::AudioProcessorGraph::NodeID attenuverterNodeID;
    juce::AudioProcessorGraph::NodeID destNodeID;
    int destChannelIndex;
    float modSignalValue;
    float modSignalPeak;
    bool isBypassed;
    float amount = 1.0f;       // attenuverter "amount" (-1..1); 1.0 for DirectCV/PolyBus (no attenuverter)
    bool sourceBipolar = true; // source's ModuleBase::isModSourceBipolar() -- see AudioEngineModRouting.cpp
};

enum class ModulationRoutingKind { AttenuverterChain, DirectCV, PolyBus };

struct ModulationRouting {
    ModulationRoutingKind kind = ModulationRoutingKind::AttenuverterChain;
    juce::AudioProcessorGraph::NodeID sourceNodeID;
    int sourceChannelIndex = 0;
    int sourceVisibleJack = 0;
    juce::AudioProcessorGraph::NodeID destNodeID;
    int destChannelIndex = 0;
    int destVisibleJack = 0;
    juce::AudioProcessorGraph::NodeID attenuverterNodeID; // valid only for AttenuverterChain
    int voiceCount = 1;
    float amount = 1.0f;
    bool isBypassed = false;
    bool hasSource = false;
    bool hasDest = false;
    float modSignalValue = 0.0f;
    float modSignalPeak = 0.0f;
    PortRole role = PortRole::ModCV;
};
