#pragma once

// Shared fixtures for the MacroPortFlow*Tests.cpp split: graph-building and lookup helpers used
// across every topic file.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroMidiOutletModule.h"
#include "Modules/MacroOutletModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Macros/MacroCardComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

juce::String uuidOf(AudioEngine& engine, NodeID id) {
    auto* node = engine.getGraph().getNodeForId(id);
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

NodeID nodeIdForUuid(AudioEngine& engine, const juce::String& uuid) {
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

bool hasConnection(AudioEngine& engine, NodeID srcId, int srcCh, NodeID dstId, int dstCh) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == srcId && c.source.channelIndex == srcCh && c.destination.nodeID == dstId &&
            c.destination.channelIndex == dstCh)
            return true;
    return false;
}

/** Groups two fresh Oscillator/Filter modules into a new collapsed macro (the min-2 rule) and
 *  returns its id. */
juce::String makeTwoMemberMacro(GraphEditor& editor, AudioEngine& engine) {
    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    return editor.groupSelectionIntoMacro();
}

/** The live ModuleComponent for `id`, or nullptr — the same lookup every drag-based test below
 *  needs to resolve a NodeID to something endConnectionDrag's own jack hit-test loop can see. */
ModuleComponent* compFor(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

} // namespace
