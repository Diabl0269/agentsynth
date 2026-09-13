#pragma once

// Shared fixture and helpers for the GraphEditor test suite (Tests/GraphEditor/GraphEditor*Tests.cpp).
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include "../../Source/Modules/FX/DelayModule.h"
#include "../../Source/Modules/FX/ReverbModule.h"
#include "../../Source/UI/GraphEditor/GraphEditor.h"
#include "../../Source/UI/ModuleComponent/ModuleComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Define a dummy component to act as the drag source
class DummyDragSource : public juce::Component {};

class GraphEditorTest : public ::testing::Test {};

// Helper to find and set a bool parameter by ID (mirrors LogicalPortTests.cpp's setPolyParam).
static void setPolyParam(juce::AudioProcessor& proc, bool value) {
    for (auto* param : proc.getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "poly") {
                p->setValueNotifyingHost(value ? 1.0f : 0.0f);
                return;
            }
        }
    }
}

static void setDualIOParam(juce::AudioProcessor& proc, bool value) {
    for (auto* param : proc.getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "dualIO") {
                p->setValueNotifyingHost(value ? 1.0f : 0.0f);
                return;
            }
        }
    }
}

inline int countAudioConnectionsBetween(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID a,
                                        juce::AudioProcessorGraph::NodeID b) {
    int n = 0;
    for (const auto& c : graph.getConnections()) {
        if (c.source.isMIDI() || c.destination.isMIDI())
            continue;
        if ((c.source.nodeID == a && c.destination.nodeID == b) || (c.source.nodeID == b && c.destination.nodeID == a))
            ++n;
    }
    return n;
}

/** Cursor position that puts a library ghost's TOP-LEFT at `topLeft`. A library drag CENTRES the
 *  ghost on the cursor, so a test that wants the ghost at a particular spot has to say so in cursor
 *  terms rather than passing the top-left it used to. */
inline juce::Point<int> libraryCursorForGhostTopLeft(const juce::String& moduleType, juce::Point<int> topLeft) {
    const auto size = GraphEditor::estimateModuleSize(moduleType);
    return topLeft + juce::Point<int>(size.x / 2, size.y / 2);
}

inline void sizeModuleComponents(GraphEditor& editor, int w = 280, int h = 300) {
    auto* content = editor.getChildComponent(0);
    ASSERT_NE(content, nullptr);
    for (auto* child : content->getChildren()) {
        if (auto* mc = dynamic_cast<ModuleComponent*>(child))
            mc->setSize(w, h);
    }
}

/** Adds the graph's terminal audio sink the way AudioEngine does. The channel layout has to be set
 *  BEFORE the node is added: AudioGraphIOProcessor snapshots it once, in setParentGraph, and an
 *  unconfigured graph reports 0 channels — every connection into the node would then be rejected. */
inline juce::AudioProcessorGraph::Node::Ptr addAudioOutputNode(juce::AudioProcessorGraph& graph, int x, int y) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto node = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    node->properties.set("x", x);
    node->properties.set("y", y);
    return node;
}

inline juce::AudioProcessorGraph::NodeID findNodeIdByName(juce::AudioProcessorGraph& graph, const juce::String& name) {
    for (auto* node : graph.getNodes())
        if (node->getProcessor() != nullptr && node->getProcessor()->getName() == name)
            return node->nodeID;
    return {};
}

/** Sink at (760,100) fed on both legs by a collapsed Reverb at (40,100) — the factory-preset shape.
 *  Leaves room for a 280px-wide ghost at x=440 to sit just left of the sink. */
struct WiredSinkFixture {
    juce::AudioProcessorGraph::Node::Ptr outNode, reverbNode;
    juce::AudioProcessorGraph::NodeID outId, reverbId;
};
inline WiredSinkFixture makeWiredSink(AudioEngine& engine, GraphEditor& editor) {
    WiredSinkFixture f;
    auto& graph = engine.getGraph();
    f.outNode = addAudioOutputNode(graph, 760, 100);
    f.reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    f.reverbNode->properties.set("x", 40);
    f.reverbNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);
    f.outId = f.outNode->nodeID;
    f.reverbId = f.reverbNode->nodeID;
    editor.connectPorts(f.reverbId, 0, f.outId, 0, false, false);
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

// Looks up a module's card by the processor it fronts (nullptr if the graph has no such card).
inline ModuleComponent* findModuleComp(GraphEditor& editor, juce::AudioProcessor* proc) {
    auto* content = editor.getChildComponent(0);
    if (content == nullptr)
        return nullptr;
    for (auto* child : content->getChildren())
        if (auto* mc = dynamic_cast<ModuleComponent*>(child))
            if (mc->getModule() == proc)
                return mc;
    return nullptr;
}

inline juce::MouseEvent makeModuleClickWithMods(juce::Component& comp, juce::Point<int> position,
                                                juce::ModifierKeys mods, int clicks = 1) {
    const auto pos = position.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), clicks,
                            false);
}

inline juce::ModifierKeys plainLeftClick() { return juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier); }
