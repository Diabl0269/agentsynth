#include "PatchEval.h"

#include <cmath>
#include <set>
#include <vector>

namespace synth {

namespace {

const juce::AudioProcessorGraph::Node* findNodeByType(const juce::AudioProcessorGraph& graph,
                                                      const juce::String& type) {
    for (auto* node : graph.getNodes())
        if (node->getProcessor() != nullptr && node->getProcessor()->getName() == type)
            return node;
    return nullptr;
}

// Backward reachability over audio-only connections (MIDI edges don't carry sound) starting at
// `startId`. Answers "is anything actually plugged into this node's audio input, all the way
// back to something that makes sound" rather than just "does it have one wire".
std::set<juce::AudioProcessorGraph::NodeID> reachableBackward(const juce::AudioProcessorGraph& graph,
                                                              juce::AudioProcessorGraph::NodeID startId) {
    std::set<juce::AudioProcessorGraph::NodeID> visited;
    std::vector<juce::AudioProcessorGraph::NodeID> stack{startId};
    const auto connections = graph.getConnections();
    while (!stack.empty()) {
        const auto current = stack.back();
        stack.pop_back();
        for (const auto& conn : connections) {
            if (conn.destination.nodeID == current && !conn.destination.isMIDI() &&
                visited.find(conn.source.nodeID) == visited.end()) {
                visited.insert(conn.source.nodeID);
                stack.push_back(conn.source.nodeID);
            }
        }
    }
    return visited;
}

// Whether one parameter's current denormalised value sits within the range (or choice count) it
// declares. Always true today — JUCE clamps on every write path via
// NormalisableRange::convertFrom0to1 — but the eval harness exercises the real production path
// with real model output rather than the values a unit test thinks to try, so it is worth
// checking rather than assuming.
bool paramInRange(juce::RangedAudioParameter* p) {
    const auto range = p->getNormalisableRange();
    const float value = range.convertFrom0to1(p->getValue());
    if (!std::isfinite(value) || value < range.start || value > range.end)
        return false;
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p))
        return choice->getIndex() >= 0 && choice->getIndex() < choice->choices.size();
    return true;
}

} // namespace

PatchEvalResult evaluatePatch(const juce::AudioProcessorGraph& graph) {
    PatchEvalResult result;
    juce::StringArray reasons;

    const auto* output = findNodeByType(graph, "Audio Output");
    result.hasAudioOutput = output != nullptr;
    if (!result.hasAudioOutput)
        reasons.add("no Audio Output node in the patch");

    if (result.hasAudioOutput) {
        for (auto id : reachableBackward(graph, output->nodeID)) {
            const auto* node = graph.getNodeForId(id);
            if (node != nullptr && node->getProcessor() != nullptr && node->getProcessor()->getName() == "Oscillator") {
                result.sourceReachesOutput = true;
                break;
            }
        }
        if (!result.sourceReachesOutput)
            reasons.add("Audio Output is not reachable from any Oscillator");
    }

    for (auto* node : graph.getNodes()) {
        if (node->getProcessor() == nullptr)
            continue;
        for (auto* param : node->getProcessor()->getParameters()) {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
                ranged != nullptr && !paramInRange(ranged)) {
                result.allParamsInRange = false;
                reasons.add(node->getProcessor()->getName() + "." + ranged->paramID + " out of declared range");
            }
        }
    }

    result.detail = reasons.joinIntoString("; ");
    return result;
}

void prepareGraphForPatchEval(juce::AudioProcessorGraph& graph) {
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    graph.prepareToPlay(44100.0, 512);
}

} // namespace synth
