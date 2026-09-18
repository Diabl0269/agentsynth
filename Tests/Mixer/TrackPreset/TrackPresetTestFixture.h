#pragma once

// TrackPresetTestFixture.h
//
// Shared fixtures/helpers for the TrackPreset test suite (Tests/Mixer/TrackPreset/TrackPreset*Tests.cpp).
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt. Reuses
// ChannelFlowTestFixture.h's HostedPatchCFT/addPlainNodeCFT/modulatesCFT/isModuleOfTypeCFT rather
// than duplicating them.

#include "../ChannelFlow/ChannelFlowTestFixture.h"
#include "Mixer/TrackPresetManager.h"
#include "Modules/ChannelStripModule.h"

// A minimal track (Track In -> Oscillator -> Filter, boxed via "Make Channel", no outside
// modulator) -- for tests that only need SOME channel to save/insert, not the outside-modulator-
// capture shape TrackPresetRigCFT below exists for (TrackPresetTests.cpp's round-trip/renumbering
// tests, TrackPresetDefaultsTests.cpp's per-type-default preset).
struct SimpleTrackRigCFT {
    juce::AudioProcessorGraph::Node* trackIn = nullptr;
    juce::AudioProcessorGraph::Node* osc = nullptr;
    juce::AudioProcessorGraph::Node* filter = nullptr;
    synth::Macro* macro = nullptr;
};

inline SimpleTrackRigCFT buildSimpleTrackRigCFT(GraphEditor& editor, AudioEngine& engine,
                                                juce::AudioProcessorGraph::Node* output) {
    auto& graph = engine.getGraph();
    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    SimpleTrackRigCFT result;
    juce::String unused;
    result.trackIn = addPlainNodeCFT(graph, "Track In", {0, 0}, unused);
    result.osc = addPlainNodeCFT(graph, "Oscillator", {200, 0}, unused);
    result.filter = addPlainNodeCFT(graph, "Filter", {500, 0}, unused);

    graph.addConnection({{result.trackIn->nodeID, midi}, {result.osc->nodeID, midi}});
    const int oscRight = dynamic_cast<ModuleBase*>(result.osc->getProcessor())->rightAudioLegChannel();
    const int filterRight = dynamic_cast<ModuleBase*>(result.filter->getProcessor())->rightAudioLegChannel();
    for (const auto [oscLeg, filterLeg, outLeg] : {std::array<int, 3>{0, 0, 0}, {oscRight, filterRight, 1}}) {
        graph.addConnection({{result.osc->nodeID, oscLeg}, {result.filter->nodeID, filterLeg}});
        graph.addConnection({{result.filter->nodeID, filterLeg}, {output->nodeID, outLeg}});
    }

    editor.makeChannelFromNode(result.trackIn->nodeID, "SimpleTrackRig");
    result.macro = editor.getMacros().findByMember(nodeUuid(result.trackIn));
    return result;
}

// Finds the freshly-inserted Channel Strip among `added` and wires it straight to `output` -- this
// harness has no Master singleton, so it stands in for insertTrackFromPresetVar's own
// spliceMasterNode + Strip->Master wiring (MainComponentTrackPresets.cpp).
inline juce::AudioProcessorGraph::Node*
wireInsertedStripToOutputCFT(juce::AudioProcessorGraph& graph,
                             const std::vector<juce::AudioProcessorGraph::NodeID>& added,
                             juce::AudioProcessorGraph::Node* output) {
    juce::AudioProcessorGraph::Node* stripNode = nullptr;
    for (const auto id : added) {
        auto* node = graph.getNodeForId(id);
        if (node != nullptr && dynamic_cast<ChannelStripModule*>(node->getProcessor()) != nullptr)
            stripNode = node;
    }
    if (stripNode != nullptr) {
        graph.addConnection({{stripNode->nodeID, 0}, {output->nodeID, 0}});
        graph.addConnection({{stripNode->nodeID, ChannelStripModule::kRightBase}, {output->nodeID, 1}});
    }
    return stripNode;
}

// A single-track channel (Track In -> Oscillator -> Filter, boxed via "Make Channel") with an LFO
// modulating the filter's cutoff. The LFO also feeds a second, otherwise-unused Filter ("sideTap")
// so it has a consumer outside the macro and is NOT absorbed as a side input (planMakeChannel's
// "every consumer already a member" absorption rule) — the same "outside module reached through a
// port" shape docs/mixer/track-presets.md describes for a shared LFO, without needing a second full track.
//
// `otherChannelStrip` is a bare (unboxed) Channel Strip modulating the same filter's Resonance —
// AudioEngine::addModRouting always wraps a hidden AttenuverterModule around EVERY mod leg
// (Source/Mixer/ChannelFlows/ChannelFlows.h's own comment), so both the LFO->Cutoff and this
// Strip->Resonance leg seed collectOutsideModulatorsForTrackPreset's walk through an attenuverter
// it must enter but never add — while a bare ChannelStripModule is exactly what isStrip() stops
// the walk at ("another channel's own strip: that channel's business, not this preset's"), letting
// a single-macro rig exercise that stop rule without building a second full track.
struct TrackPresetRigCFT {
    juce::AudioProcessorGraph::Node* trackIn = nullptr;
    juce::AudioProcessorGraph::Node* osc = nullptr;
    juce::AudioProcessorGraph::Node* filter = nullptr;
    juce::AudioProcessorGraph::Node* lfo = nullptr;
    juce::AudioProcessorGraph::Node* sideTap = nullptr;           // the LFO's other consumer; never reaches output
    juce::AudioProcessorGraph::Node* otherChannelStrip = nullptr; // another channel's strip; never captured
    int cutoffChannel = -1;
    int resonanceChannel = -1;
    synth::Macro* macro = nullptr; // track's channel macro, owned by `editor`
};

inline TrackPresetRigCFT buildTrackPresetRigCFT(GraphEditor& editor, AudioEngine& engine,
                                                juce::AudioProcessorGraph::Node* output) {
    auto& graph = engine.getGraph();
    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    TrackPresetRigCFT result;
    juce::String unused;
    result.trackIn = addPlainNodeCFT(graph, "Track In", {0, 0}, unused);
    result.osc = addPlainNodeCFT(graph, "Oscillator", {200, 0}, unused);
    result.filter = addPlainNodeCFT(graph, "Filter", {500, 0}, unused);
    result.lfo = addPlainNodeCFT(graph, "LFO", {0, 300}, unused);
    result.sideTap = addPlainNodeCFT(graph, "Filter", {0, 500}, unused);
    result.otherChannelStrip = addPlainNodeCFT(graph, "Channel Strip", {0, 700}, unused);
    result.cutoffChannel = cutoffChannelCFT(result.filter);
    if (auto* filterModule = dynamic_cast<ModuleBase*>(result.filter->getProcessor())) {
        const auto targets = filterModule->getModulationTargets();
        for (const auto& target : targets)
            if (target.name == "Resonance")
                result.resonanceChannel = target.channelIndex;
    }

    graph.addConnection({{result.trackIn->nodeID, midi}, {result.osc->nodeID, midi}});
    const int oscRight = dynamic_cast<ModuleBase*>(result.osc->getProcessor())->rightAudioLegChannel();
    const int filterRight = dynamic_cast<ModuleBase*>(result.filter->getProcessor())->rightAudioLegChannel();
    for (const auto [oscLeg, filterLeg, outLeg] : {std::array<int, 3>{0, 0, 0}, {oscRight, filterRight, 1}}) {
        graph.addConnection({{result.osc->nodeID, oscLeg}, {result.filter->nodeID, filterLeg}});
        graph.addConnection({{result.filter->nodeID, filterLeg}, {output->nodeID, outLeg}});
    }

    engine.addModRouting(result.lfo->nodeID, 0, result.filter->nodeID, result.cutoffChannel);
    // The LFO's second consumer: reaches nothing (never wired to output), so it never becomes part
    // of any track's own region — its only job is to keep the LFO from being absorbed as a side
    // input of the track's macro.
    const int sideCutoff = cutoffChannelCFT(result.sideTap);
    engine.addModRouting(result.lfo->nodeID, 0, result.sideTap->nodeID, sideCutoff);
    // Another channel's strip modulating this track's Resonance: the walk must enter the hidden
    // attenuverter this leg gets wrapped in, then stop at the strip itself (never add it, never
    // walk past it) — the test-side counterpart to collectOutsideModulatorsForTrackPreset's
    // "another channel's own strip" stop rule.
    engine.addModRouting(result.otherChannelStrip->nodeID, 0, result.filter->nodeID, result.resonanceChannel);

    editor.makeChannelFromNode(result.trackIn->nodeID, "TrackPresetRig");
    result.macro = editor.getMacros().findByMember(nodeUuid(result.trackIn));
    return result;
}
