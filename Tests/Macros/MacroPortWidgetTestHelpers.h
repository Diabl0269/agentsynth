#pragma once

// Shared fixtures + helpers for the MacroPortWidget*Tests.cpp split: graph-building and
// lookup helpers used across every topic file. Each split TU gets its own anonymous-namespace
// copy (the standard `namespace { ... }` split pattern -- same as MacroPortFlowTestHelpers.h),
// so there is no ODR/linkage concern; the existing MacroPortWidgetTests.cpp keeps its own copy.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent.h"
#include "UI/Macros/MacroPortConfigDialog/MacroPortConfigDialog.h"
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

NodeID nodeIdForUuid(AudioEngine& engine, const juce::String& uuid) {
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

ModuleComponent* findComponent(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

/** Groups two fresh Oscillator/Filter modules into a new collapsed macro (the min-2 rule) and
 *  returns its id. Callers that need a docked widget must expand it first
 *  (editor.getMacroController().setMacroCollapsed(id, false)) -- a port's ModuleComponent is hidden, same as any other
 *  member, while its macro is collapsed. */
juce::String makeTwoMemberMacro(GraphEditor& editor, AudioEngine& engine) {
    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    return editor.getMacroController().groupSelectionIntoMacro();
}

} // namespace
